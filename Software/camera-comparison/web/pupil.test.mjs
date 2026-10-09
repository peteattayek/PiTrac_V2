// SPDX-License-Identifier: GPL-3.0-or-later
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { webcrypto } from "node:crypto";

globalThis.crypto ??= webcrypto;
const archiveSource = await readFile(new URL("./archive.js", import.meta.url), "utf8");
const archiveUrl = `data:text/javascript;base64,${Buffer.from(archiveSource).toString("base64")}`;
const { buildArchive, capturePng } = await import(archiveUrl);
const source = (await readFile(new URL("./pupil.js", import.meta.url), "utf8"))
    .replace('import { downloadCapture } from "./capture.js";', "const downloadCapture = () => { throw new Error('Inject save in tests'); };")
    .replace('"./archive.js"', JSON.stringify(archiveUrl));
const { createPupilControls, measurementSettings, tripletArchive } = await import(
    `data:text/javascript;base64,${Buffer.from(source).toString("base64")}`
);
const png = new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10, 42]);
const values = { camera: "mira220", lens: "Test lens", position: "0", angle: "5", rotation: "yaw", notes: "fixed focus" };

async function archiveFiles(blob) {
    const bytes = new Uint8Array(await blob.arrayBuffer()), view = new DataView(bytes.buffer);
    const files = {};
    for (let offset = 0; view.getUint32(offset, true) === 0x04034b50;) {
        const size = view.getUint32(offset + 18, true);
        const nameLength = view.getUint16(offset + 26, true), extraLength = view.getUint16(offset + 28, true);
        const name = new TextDecoder().decode(bytes.subarray(offset + 30, offset + 30 + nameLength));
        const start = offset + 30 + nameLength + extraLength;
        files[name] = bytes.slice(start, start + size);
        offset = start + size;
    }
    return files;
}

function element(value = "") {
    return { value, textContent: "", disabled: false, events: {},
        addEventListener(name, callback) { this.events[name] = callback; } };
}

async function fixture() {
    const nodes = Object.fromEntries(Object.entries(values).map(
        ([name, value]) => [`[data-pupil-${name}]`, element(value)]
    ));
    for (const name of ["capture", "download", "next", "discard", "progress", "message"]) {
        nodes[`[data-pupil-${name}]`] = element();
    }
    const options = Object.fromEntries(["negative", "positive", "zero"].map(name => [name, element(name)]));
    nodes["[data-pupil-capture-angle]"] = { ...element("zero"),
        querySelector: selector => options[/value="([^"]+)"/.exec(selector)[1]] };
    const blob = await buildArchive([
        { name: "mira220.png", data: png }, { name: "imx296.png", data: png },
        { name: "metadata.json", data: '{"session_id":"first"}' }
    ]);
    const headers = { "Content-Type": "application/zip", "X-Preview-Session": "first",
        "Content-Disposition": 'attachment; filename="pitrac-capture-first.zip"' };
    const state = { nodes, options, saved: [], requests: [], blob, headers };
    state.response = { ok: true, status: 200, headers: { get: name => headers[name] },
        blob: async () => blob, json: async () => ({ error: "Camera not live" }) };
    state.controls = createPupilControls({ querySelector: name => nodes[name] }, {
        async request(url, options) { state.requests.push({ url, ...options }); return state.response; },
        save(blob, filename) { state.saved.push({ blob, filename }); }
    });
    state.status = { session_id: "first", snapshot_capture: { available: false,
        by_camera: { mira220: { available: true }, imx296: { available: false, error: "Not configured" } } } };
    state.controls.update(state.status);
    state.controls.setConnected(true);
    state.node = name => nodes[`[data-pupil-${name}]`];
    state.click = name => state.node(name).events.click();
    state.select = value => {
        state.node("capture-angle").value = value;
        state.node("capture-angle").events.change();
    };
    return state;
}

test("zero-degree capture can start a triplet without changing the side-angle magnitude", async () => {
    const f = await fixture();
    assert.match(f.node("capture").textContent, /^Capture 0 degrees$/);
    await f.click("capture");
    assert.match(f.node("progress").textContent, /^1\/3/);
    assert.equal(f.node("angle").value, "5");
    assert.equal(f.options.zero.disabled, true);
    assert.equal(f.node("capture-angle").value, "negative");
});

test("all six capture orders export canonical angle labels with matching pixels and actual acquisition order", async () => {
    const orders = [
        ["zero", "negative", "positive"], ["zero", "positive", "negative"],
        ["negative", "zero", "positive"], ["negative", "positive", "zero"],
        ["positive", "zero", "negative"], ["positive", "negative", "zero"]
    ];
    const angles = { negative: -5, positive: 5, zero: 0 };
    const markers = { negative: 11, positive: 12, zero: 10 };
    for (const order of orders) {
        const f = await fixture();
        for (const choice of order) {
            f.select(choice);
            const pixels = png.slice();
            pixels[pixels.length - 1] = markers[choice];
            f.response.blob = () => buildArchive([{ name: "mira220.png", data: pixels }]);
            await f.click("capture");
            assert.equal(f.options[choice].disabled, true);
        }
        await f.click("download");
        const files = await archiveFiles(f.saved[0].blob);
        const manifest = JSON.parse(new TextDecoder().decode(files["measurement.json"]));
        assert.deepEqual(manifest.capture_order_degrees, order.map(choice => angles[choice]));
        assert.deepEqual(manifest.images.map(image => image.angle_degrees), [-5, 5, 0]);
        assert.equal(files["minus-5deg.png"].at(-1), 11);
        assert.equal(files["plus-5deg.png"].at(-1), 12);
        assert.equal(files["zero-0deg.png"].at(-1), 10);
        assert.equal(f.node("capture-angle").disabled, true);
        assert.equal(f.node("capture").disabled, true);
    }
});

test("custom side-angle magnitude updates choices while centered capture remains zero", async () => {
    const f = await fixture();
    f.node("angle").value = "7.5";
    f.node("angle").events.input();
    assert.equal(f.options.negative.textContent, "-7.5 degrees");
    assert.equal(f.options.positive.textContent, "+7.5 degrees");
    assert.match(f.node("capture").textContent, /^Capture 0 degrees$/);
    await f.click("capture");
    assert.equal(f.node("angle").disabled, true);
    assert.equal(f.node("capture-angle").disabled, false);
    f.select("positive");
    assert.match(f.node("capture").textContent, /^Capture \+7.5 degrees$/);
});

test("zero side-angle magnitude explains how to select the centered photo instead", async () => {
    const f = await fixture();
    f.node("angle").value = "0";
    f.node("angle").events.input();
    await f.click("capture");
    assert.equal(f.requests.length, 0);
    assert.match(f.node("message").textContent, /choose 0 in Capture angle/);
});

test("measurement settings accept signed positions and configurable angles; reject invalid input", () => {
    assert.equal(measurementSettings({ ...values, position: "-1", angle: "7.5" }).position_mm, -1);
    for (const invalid of [{ lens: "" }, { position: "" }, { position: "Infinity" }, { angle: "0" },
        { angle: "90" }, { angle: "NaN" }, { camera: "unknown" }, { rotation: "roll" }]) {
        assert.throws(() => measurementSettings({ ...values, ...invalid }));
    }
});

test("manual -5, +5, 0 sequence locks parameters, exports three PNGs and advances by 1 mm only after download", async () => {
    const f = await fixture();
    f.select("negative");
    assert.match(f.node("capture").textContent, /-5/);
    await f.click("capture");
    assert.equal(f.node("lens").disabled, true);
    assert.match(f.node("capture").textContent, /\+5/);
    await f.click("capture");
    assert.match(f.node("capture").textContent, /0 degrees/);
    await f.click("capture");
    assert.equal(f.node("capture").disabled, true);
    assert.equal(f.node("next").disabled, true);
    await f.click("next");
    assert.equal(f.node("position").value, "0");
    await f.click("download");
    assert.equal(f.saved.length, 1);
    assert.match(f.saved[0].filename, /^pitrac-pupil-mira220-0mm-/);
    for (const name of ["minus-5deg.png", "plus-5deg.png", "zero-0deg.png"]) {
        assert.deepEqual(await capturePng(f.saved[0].blob, name), png);
    }
    assert.equal(f.controls.hasUnsavedSet(), false);
    await f.click("next");
    assert.equal(f.node("position").value, "1");
    assert.match(f.node("message").textContent, /manually/);
    assert.equal(f.node("lens").disabled, false);
    assert.equal(f.node("download").disabled, true);
    assert.deepEqual(f.requests.map(req => JSON.parse(req.body)), Array(3).fill({ session_id: "first", camera: "mira220" }));
    f.node("position").value = "0";
    f.node("position").events.input();
    assert.match(f.node("message").textContent, /Draft changed/);
    assert.doesNotMatch(f.node("message").textContent, /Next position is 1/);
});

test("errors retry the same angle; unavailable or disconnected cameras disable capture", async () => {
    const f = await fixture();
    f.select("negative");
    f.response.ok = false;
    f.headers["Content-Type"] = "application/json";
    await f.click("capture");
    assert.match(f.node("message").textContent, /not captured: Camera not live/);
    assert.match(f.node("capture").textContent, /-5/);
    assert.equal(f.node("download").disabled, true);
    f.controls.setConnected(false);
    assert.equal(f.node("capture").disabled, true);
    f.controls.setConnected(true);
    f.controls.update({ ...f.status, snapshot_capture: { by_camera: { mira220: { available: false, error: "Not live" } } } });
    assert.equal(f.node("capture").disabled, true);
});

test("double clicks do not duplicate captures and discard prevents a late response from joining a new set", async () => {
    const f = await fixture();
    let finish;
    f.response.blob = () => new Promise(resolve => { finish = resolve; });
    const pending = f.click("capture");
    await Promise.resolve();
    await f.click("capture");
    assert.equal(f.requests.length, 1);
    await f.click("discard");
    assert.equal(f.requests[0].signal.aborted, true);
    finish(f.blob);
    await pending;
    assert.match(f.node("progress").textContent, /^0\/3/);
});

test("exposure or preview session changes invalidate a partial set, while transient disconnects retain it", async () => {
    const f = await fixture();
    await f.click("capture");
    assert.equal(f.controls.hasUnsavedSet(), true);
    f.controls.setConnected(false);
    f.controls.setConnected(true);
    assert.match(f.node("progress").textContent, /^1\/3/);
    f.controls.update({ ...f.status, session_id: "second" });
    assert.match(f.node("progress").textContent, /^0\/3/);
    assert.match(f.node("message").textContent, /changed/);
    f.controls.invalidate();
    assert.equal(f.node("capture").disabled, true);
    f.controls.dispose();
    f.controls.resume();
    assert.equal(f.node("capture").disabled, true);
});

test("incorrect capture type, session, filename or ZIP cannot advance the sequence", async () => {
    for (const fault of ["type", "session", "filename", "zip"]) {
        const f = await fixture();
        if (fault === "type") f.headers["Content-Type"] = "image/jpeg";
        if (fault === "session") f.headers["X-Preview-Session"] = "old";
        if (fault === "filename") f.headers["Content-Disposition"] = 'attachment; filename="../bad.zip"';
        if (fault === "zip") f.response.blob = async () => new Blob([]);
        await f.click("capture");
        assert.match(f.node("progress").textContent, /^0\/3/);
        assert.match(f.node("message").textContent, /not captured/);
    }
});

test("triplet export rejects incomplete, duplicate and incorrect-angle sets", async () => {
    const settings = measurementSettings(values);
    await assert.rejects(tripletArchive(settings, [], "session", "id"), /complete/);
    await assert.rejects(tripletArchive(settings, [{ angle: -5 }, { angle: 0 }, { angle: 0 }], "session", "id"), /complete/);
    await assert.rejects(tripletArchive(settings, [{ angle: -5 }, { angle: 0 }, { angle: 10 }], "session", "id"), /complete/);
});

test("selecting an unavailable camera disables capture and surfaces the reason", async () => {
    const f = await fixture();
    f.node("camera").value = "imx296";
    f.node("camera").events.input();
    assert.equal(f.node("capture").disabled, true);
    assert.match(f.node("progress").textContent, /Not configured/);
});
