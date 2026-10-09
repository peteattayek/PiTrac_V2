// SPDX-License-Identifier: GPL-3.0-or-later
// Browser-controller tests with deterministic DOM, network and clock fixtures.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

const source = (await readFile(new URL("./preview.js", import.meta.url), "utf8"))
    .replace(/^import .*from "\.\/(?:decoder|exposure|capture|pupil)\.js";$/gm, "");
const decoderSource = await readFile(new URL("./decoder.js", import.meta.url), "utf8");
const decoder = await import(`data:text/javascript;base64,${Buffer.from(decoderSource).toString("base64")}`);

function element() {
    return {
        textContent: "", dataset: {}, events: {}, hidden: false, disabled: false,
        classList: { add() {}, remove() {}, replace() {}, toggle() {} },
        addEventListener(name, callback) { this.events[name] = callback; },
        setAttribute() {},
        getBoundingClientRect() { return { left: 0, top: 0, width: 3, height: 2 }; },
        scrollWidth: 3, scrollHeight: 2, clientWidth: 3, clientHeight: 2,
        clientLeft: 0, clientTop: 0, scrollLeft: 0, scrollTop: 0
    };
}

function fixture() {
    const fields = Object.fromEntries(["scale", "pixel", "badge", "overlay-title", "overlay-detail",
        "display-fps", "age", "format", "native-fps", "capture-fps", "requested-exposure",
        "estimated-exposure", "gain", "frames", "warnings", "frame-id"].map(name => {
        const e = element(); e.dataset.field = name; return [name, e];
    }));
    const draws = [];
    const context2d = { drawImage: image => draws.push(["jpeg", image]),
        putImageData: image => draws.push(["raw", image]) };
    const canvas = { ...element(), width: 1, height: 1, getContext: () => context2d };
    const children = { canvas, ".viewport": element(), ".frame-overlay": element(),
        '[data-action="center"]': element() };
    const panel = { ...element(), dataset: { camera: "mira220" },
        querySelector: key => children[key],
        querySelectorAll: key => key === "[data-field]" ? Object.values(fields) : [] };
    const jpegOption = element();
    const select = { ...element(), value: "jpeg", querySelector: () => jpegOption };
    const page = { "#connection-state": element(), "#refresh-budget": element(),
        "#transport-mode": select, "#transport-note": element() };
    const config = { name: "mira220", width: 3, height: 2, stride: 4, sizeimage: 8,
        fourcc: "GREY", state: "live", frames_received: 10, frame_id: 10, frame_age_ms: 1,
        target_fps: 89.08, capture_fps: 89.08, requested_exposure_us: 10000,
        estimated_exposure_us: 10001, analogue_gain: 1, timing_warnings: 0 };
    const status = { cameras: [config], preview_fps: 10, preview_transport: "jpeg",
        transports: ["jpeg", "raw"], session_id: "first", saves_frames: false, isp: false };
    const state = { now: 1000, frameId: 11, responseSession: "first", bitmapWidth: 3,
        bitmapClosed: 0, requests: [], status, config, select, fields, canvas, draws };
    const document = { hidden: false, querySelector: key => page[key],
        querySelectorAll: () => [panel], addEventListener() {} };
    const ctx = vm.createContext({
        document, window: { addEventListener() {} }, performance: { now: () => state.now },
        setTimeout: () => 1, clearTimeout() {}, setInterval: () => 1, clearInterval() {},
        AbortController, Blob, ArrayBuffer, Uint8Array, Uint8ClampedArray,
        ImageData: class { constructor(data, width, height) { Object.assign(this, { data, width, height }); } },
        ...decoder,
        createImageBitmap: async () => ({ width: state.bitmapWidth, height: 2,
            close() { state.bitmapClosed++; } }),
        fetch: async url => {
            state.requests.push(url);
            if (url === "/api/status") return { ok: true, json: async () => structuredClone(status) };
            const jpeg = url.includes("format=jpeg");
            return { ok: true, status: 200, headers: { get: name => ({
                "Content-Type": jpeg ? "image/jpeg" : "application/octet-stream",
                "X-Frame-Id": String(state.frameId), "X-Preview-Session": state.responseSession
            })[name] }, arrayBuffer: async () => new Uint8Array([0, 127, 255, 88, 1, 2, 3, 99]).buffer };
        }
    });
    vm.runInContext(source, ctx);
    state.run = code => vm.runInContext(code, ctx);
    return state;
}

test("JPEG paints native dimensions, reports live, and does not claim native pixel values", async () => {
    const f = fixture();
    await f.run("pollStatus()");
    await f.run("pollFrame(cameras[0])");
    assert.equal(f.draws[0][0], "jpeg");
    assert.deepEqual([f.canvas.width, f.canvas.height], [3, 2]);
    assert.equal(f.bitmapClosed, 1);
    assert.equal(f.fields.badge.textContent, "LIVE");
    assert.match(f.fields.pixel.textContent, /switch to Raw pixels/);
    assert.equal(f.run("cameras[0].raw"), null);
    assert.ok(f.requests.includes("/api/frame/mira220?format=jpeg"));
});

test("switching raw/JPEG clears cursor and paints the requested mode with no retained native readout", async () => {
    const f = fixture();
    await f.run("pollStatus()");
    await f.run("pollFrame(cameras[0])");
    f.select.value = "raw"; f.select.events.change(); f.now += 1000;
    assert.equal(f.fields.badge.textContent, "UNAVAILABLE");
    await f.run("pollFrame(cameras[0])");
    assert.equal(f.draws.at(-1)[0], "raw");
    assert.equal(f.run("cameras[0].raw.length"), 8);
    assert.deepEqual([...f.draws.at(-1)[1].data.slice(0, 8)], [0, 0, 0, 255, 127, 127, 127, 255]);
    f.select.value = "jpeg"; f.select.events.change(); f.now += 1000;
    await f.run("pollFrame(cameras[0])");
    assert.equal(f.draws.at(-1)[0], "jpeg");
    assert.equal(f.run("cameras[0].raw"), null);
    assert.equal(f.fields.badge.textContent, "LIVE");
});

test("session change resets the cursor even when a restarted server has a larger counter", async () => {
    const f = fixture();
    await f.run("pollStatus()"); await f.run("pollFrame(cameras[0])");
    f.status.session_id = "second"; f.config.frames_received = 100; f.config.frame_id = 100;
    f.responseSession = "second"; f.frameId = 101; f.now += 1000;
    await f.run("pollStatus()");
    assert.equal(f.run("cameras[0].afterId"), null);
    await f.run("pollFrame(cameras[0])");
    assert.equal(f.fields.badge.textContent, "LIVE");
    assert.equal(f.run("cameras[0].afterId"), 101);
});

test("wrong session or JPEG dimensions are rejected without replacing the displayed image", async () => {
    for (const fault of ["session", "dimensions"]) {
        const f = fixture(); await f.run("pollStatus()");
        if (fault === "session") f.responseSession = "wrong";
        else f.bitmapWidth = 99;
        await f.run("pollFrame(cameras[0])");
        assert.equal(f.draws.length, 0);
        assert.equal(f.fields.badge.textContent, "UNAVAILABLE");
        assert.equal(f.bitmapClosed, fault === "dimensions" ? 1 : 0);
    }
});

test("fresh status cannot hide a stale displayed frame", async () => {
    const f = fixture(); await f.run("pollStatus()"); await f.run("pollFrame(cameras[0])");
    f.now += 2000; f.config.frame_id = 200; f.config.frames_received = 200;
    await f.run("pollStatus()");
    assert.equal(f.fields.badge.textContent, "STALE");
});
