// SPDX-License-Identifier: GPL-3.0-or-later
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const source = await readFile(new URL("./capture.js", import.meta.url), "utf8");
const { createCaptureControls } = await import(
    `data:text/javascript;base64,${Buffer.from(source).toString("base64")}`
);

function element() {
    return { textContent: "", disabled: false, events: {},
        addEventListener(name, callback) { this.events[name] = callback; } };
}

function fixture() {
    const button = element(), availability = element(), message = element();
    const nodes = { "[data-capture-button]": button, "[data-capture-availability]": availability,
        "[data-capture-message]": message };
    const state = { requests: [], saved: [], response: null };
    const filename = "pitrac-capture-20260921T160000000000Z-abcdef.zip";
    const headers = { "Content-Type": "application/zip", "X-Preview-Session": "first",
        "Content-Disposition": `attachment; filename="${filename}"` };
    state.response = { ok: true, status: 200, headers: { get: key => headers[key] },
        blob: async () => new Blob(["zip bytes"]), json: async () => ({ error: "Camera not live." }) };
    const controls = createCaptureControls({ querySelector: key => nodes[key] }, {
        async request(url, options) {
            state.requests.push({ url, ...options });
            return state.response;
        },
        save(blob, name) { state.saved.push({ blob, name }); }
    });
    const status = { session_id: "first", snapshot_capture: { available: true, error: null } };
    return { state, button, availability, message, controls, status, headers, filename,
        click: () => button.events.click(),
        ready() { controls.update(status); controls.setConnected(true); } };
}

test("capture waits for current status and both live cameras, with explicit unavailable reason", async () => {
    const f = fixture();
    assert.equal(f.button.disabled, true);
    await f.click();
    f.ready();
    assert.equal(f.button.disabled, false);
    f.status.snapshot_capture = { available: false, error: "Waiting for recent raw frames from both cameras." };
    f.controls.update(f.status);
    assert.equal(f.button.disabled, true);
    assert.match(f.availability.textContent, /both cameras/);
    await f.click();
    assert.equal(f.state.requests.length, 0);
});

test("one click posts session and downloads one ZIP, independently of preview transport", async () => {
    for (const mode of ["jpeg", "raw"]) {
        const f = fixture();
        f.status.preview_transport = mode;
        f.ready();
        await f.click();
        assert.equal(f.state.requests.length, 1);
        const req = f.state.requests[0];
        assert.equal(req.url, "/api/capture");
        assert.equal(req.method, "POST");
        assert.equal(req.headers["Content-Type"], "application/json");
        assert.deepEqual(JSON.parse(req.body), { session_id: "first" });
        assert.equal(f.state.saved.length, 1);
        assert.equal(f.state.saved[0].name, f.filename);
        assert.match(f.message.textContent, /Download requested/);
        assert.equal(f.button.disabled, false);
    }
});

test("double clicks do not duplicate requests and status polling does not enable a pending capture", async () => {
    const f = fixture();
    f.ready();
    let finish;
    f.state.response.blob = () => new Promise(resolve => { finish = resolve; });
    const pending = f.click();
    await Promise.resolve();
    assert.equal(f.button.disabled, true);
    f.controls.update(f.status);
    await f.click();
    assert.equal(f.state.requests.length, 1);
    finish(new Blob(["zip bytes"]));
    await pending;
    assert.equal(f.state.saved.length, 1);
});

test("HTTP failures, invalid type, wrong session, bad filename and empty data never claim success", async () => {
    for (const failure of ["http", "type", "session", "filename", "empty"]) {
        const f = fixture();
        f.ready();
        if (failure === "http") {
            f.state.response.ok = false;
            f.state.response.status = 503;
            f.headers["Content-Type"] = "application/json";
        } else if (failure === "type") f.headers["Content-Type"] = "image/jpeg";
        else if (failure === "session") f.headers["X-Preview-Session"] = "old";
        else if (failure === "filename") f.headers["Content-Disposition"] = 'attachment; filename="../bad.zip"';
        else f.state.response.blob = async () => new Blob([]);
        await f.click();
        assert.equal(f.state.saved.length, 0);
        assert.match(f.message.textContent, /not downloaded/);
        assert.equal(f.button.disabled, false);
    }
});

test("stale connection and exposure invalidation disable capture until a fresh status", () => {
    const f = fixture();
    f.ready();
    f.controls.setConnected(false);
    assert.equal(f.button.disabled, true);
    f.controls.setConnected(true);
    f.controls.invalidate();
    assert.equal(f.button.disabled, true);
    f.controls.update(f.status);
    assert.equal(f.button.disabled, false);
});

test("page hide aborts the request without downloading; a restored page needs current status", async () => {
    const f = fixture();
    f.ready();
    let finish;
    f.state.response.blob = () => new Promise(resolve => { finish = resolve; });
    const pending = f.click();
    await Promise.resolve();
    f.controls.dispose();
    assert.equal(f.state.requests[0].signal.aborted, true);
    finish(new Blob(["zip bytes"]));
    await pending;
    assert.equal(f.state.saved.length, 0);
    f.controls.resume();
    assert.equal(f.button.disabled, true);
    f.ready();
    assert.equal(f.button.disabled, false);
});
