// SPDX-License-Identifier: GPL-3.0-or-later
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const source = await readFile(new URL("./exposure.js", import.meta.url), "utf8");
const { createExposureControls, exposurePayload } = await import(
    `data:text/javascript;base64,${Buffer.from(source).toString("base64")}`
);

function element(extra = {}) {
    return { value: "", checked: false, disabled: false, textContent: "", events: {},
        addEventListener(name, callback) { this.events[name] = callback; }, ...extra };
}

function fixture() {
    const link = element({ checked: true });
    const inputs = ["mira220", "imx296"].map(name => element({ dataset: { exposureCamera: name } }));
    const apply = element(), reload = element(), message = element(), feedback = element();
    const nodes = { "[data-exposure-link]": link, "[data-exposure-apply]": apply,
        "[data-exposure-reload]": reload, "[data-exposure-message]": message,
        "[data-exposure-feedback]": feedback };
    const form = element({ querySelector: selector => nodes[selector], querySelectorAll: () => inputs });
    const status = {
        session_id: "session-1", exposure_control: { linked: true, available: true, revision: 0,
            requested_us: { mira220: 1000, imx296: 1000 } },
        cameras: ["mira220", "imx296"].map(name => ({ name, requested_exposure_us: 1000,
            estimated_exposure_us: name === "mira220" ? 999.8 : 1006.9, target_fps: 60 }))
    };
    const state = { requests: [], fail: false, applied: 0 };
    const controls = createExposureControls(form, {
        onApply() { state.applied++; },
        async request(url, options) {
            const payload = JSON.parse(options.body);
            state.requests.push({ url, ...options, payload });
            if (state.fail) return { ok: false, json: async () => ({ error: "Hardware readback failed; rolled back." }) };
            const result = structuredClone(status);
            result.exposure_control = { ...result.exposure_control, linked: payload.linked,
                revision: payload.revision + 1, requested_us: payload.exposures_us };
            return { ok: true, json: async () => result };
        }
    });
    controls.update(status);
    return { ...state, state, controls, status, link, inputs, apply, reload, message, form,
        submit: () => form.events.submit({ preventDefault() {} }) };
}

test("starts linked and editing either input mirrors both without writing yet", () => {
    const f = fixture();
    assert.equal(f.link.checked, true);
    f.inputs[1].value = "20000";
    f.inputs[1].events.input();
    assert.equal(f.inputs[0].value, "20000");
    assert.equal(f.state.requests.length, 0);
    assert.equal(f.apply.disabled, false);
});

test("unlinking allows independent values; relinking copies Mira value into both drafts", async () => {
    const f = fixture();
    f.link.checked = false; f.link.events.change();
    f.inputs[1].value = "500000"; f.inputs[1].events.input();
    assert.equal(f.inputs[0].value, "1000");
    await f.submit();
    assert.deepEqual(f.state.requests[0].payload.exposures_us, { mira220: 1000, imx296: 500000 });
    assert.equal(f.state.requests[0].payload.linked, false);
    f.link.checked = true; f.link.events.change();
    assert.equal(f.inputs[1].value, "1000");
    assert.match(f.message.textContent, /Mira220/);
});

test("linked submit includes optimistic revision, session and identical requested values", async () => {
    const f = fixture();
    f.inputs[0].value = "30000"; f.inputs[0].events.input();
    await f.submit();
    const req = f.state.requests[0];
    assert.equal(req.method, "POST");
    assert.equal(req.headers["Content-Type"], "application/json");
    assert.deepEqual(req.payload, { linked: true, exposures_us: { mira220: 30000, imx296: 30000 },
        revision: 0, session_id: "session-1" });
    assert.equal(f.state.applied, 1);
    assert.equal(f.apply.disabled, true);
});

test("status polling preserves an edited draft and failed apply does not claim success", async () => {
    const f = fixture();
    f.inputs[0].value = "25000"; f.inputs[0].events.input();
    f.controls.update(f.status);
    assert.equal(f.inputs[0].value, "25000");
    f.state.fail = true;
    await f.submit();
    assert.match(f.message.textContent, /readback failed/);
    assert.equal(f.apply.disabled, false);
    f.reload.events.click();
    assert.equal(f.inputs[0].value, "1000");
});

test("invalid and mismatched inputs are rejected before any request", async () => {
    const f = fixture();
    for (const invalid of ["", "-1", "NaN", "Infinity", "500001"]) {
        f.inputs[0].value = invalid; f.inputs[0].events.input();
        await f.submit();
    }
    assert.equal(f.state.requests.length, 0);
    assert.throws(() => exposurePayload(true, { mira220: "1000", imx296: "2000" }, 0, "s"), /same requested/);
});

test("restoring a cached browser page re-enables exposure edits", async () => {
    const f = fixture();
    f.controls.dispose();
    f.controls.resume();
    f.controls.update(f.status);
    f.inputs[0].value = "12000"; f.inputs[0].events.input();
    await f.submit();
    assert.equal(f.state.requests.length, 1);
});
