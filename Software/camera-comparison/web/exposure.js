// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors

export function exposurePayload(linked, values, revision, sessionId) {
    const exposures = {};
    for (const [name, value] of Object.entries(values)) {
        if (typeof value !== "string" || !value.trim()) throw new Error("Enter an exposure for each camera.");
        const number = Number(value);
        if (!Number.isFinite(number) || number < 30 || number > 500000) {
            throw new Error("Exposure must be between 30 and 500,000 microseconds.");
        }
        exposures[name] = number;
    }
    if (!Object.keys(exposures).length) throw new Error("No cameras are available.");
    if (linked && new Set(Object.values(exposures)).size !== 1) {
        throw new Error("Linked cameras must have the same requested exposure.");
    }
    return { linked, exposures_us: exposures, revision, session_id: sessionId };
}

export function createExposureControls(form, { request = fetch, onApply = () => {} } = {}) {
    const link = form.querySelector("[data-exposure-link]");
    const inputs = [...form.querySelectorAll("[data-exposure-camera]")];
    const apply = form.querySelector("[data-exposure-apply]");
    const reload = form.querySelector("[data-exposure-reload]");
    const message = form.querySelector("[data-exposure-message]");
    const feedback = form.querySelector("[data-exposure-feedback]");
    let latest = null;
    let revision = null;
    let session = null;
    let dirty = false;
    let pending = false;
    let disposed = false;
    let controller = null;

    function availableInputs() {
        const names = Object.keys(latest?.exposure_control?.requested_us || {});
        return inputs.filter(input => names.includes(input.dataset.exposureCamera));
    }
    function render() {
        const allowed = latest?.exposure_control?.available === true;
        for (const input of inputs) input.disabled = pending || !allowed || !availableInputs().includes(input);
        link.disabled = pending || !allowed || availableInputs().length < 2;
        apply.disabled = pending || !allowed || !dirty;
        reload.disabled = pending || !latest?.exposure_control;
        apply.textContent = pending ? "Applying..." : "Apply exposure";
    }
    function loadValues() {
        if (!latest?.exposure_control) return;
        link.checked = latest.exposure_control.linked;
        for (const input of availableInputs()) {
            input.value = String(latest.exposure_control.requested_us[input.dataset.exposureCamera]);
        }
        revision = latest.exposure_control.revision;
        session = latest.session_id;
        dirty = false;
        message.textContent = "Values match the camera control readback.";
        render();
    }
    function edit(input) {
        if (pending) return;
        if (link.checked) for (const other of availableInputs()) other.value = input.value;
        dirty = true;
        message.textContent = "Pending changes. Apply to update the sensors; longer exposures reduce acquisition FPS.";
        render();
    }
    for (const input of inputs) input.addEventListener("input", () => edit(input));
    link.addEventListener("change", () => {
        if (pending) return;
        if (link.checked) {
            const source = availableInputs()[0];
            if (source) for (const input of availableInputs()) input.value = source.value;
            message.textContent = "Linked: the Mira220 value is copied to both inputs. Apply to confirm.";
        } else {
            message.textContent = "Unlinked: edit either camera independently, then apply.";
        }
        dirty = true;
        render();
    });
    reload.addEventListener("click", loadValues);
    form.addEventListener("submit", async event => {
        event.preventDefault();
        if (pending || disposed || !latest?.exposure_control?.available) return;
        let payload;
        try {
            payload = exposurePayload(link.checked, Object.fromEntries(
                availableInputs().map(input => [input.dataset.exposureCamera, input.value])
            ), revision, session);
        } catch (error) {
            message.textContent = error.message;
            return;
        }
        pending = true;
        controller = new AbortController();
        const deadline = setTimeout(() => controller.abort(), 25000);
        render();
        message.textContent = "Applying sensor exposure and frame timing. Capture will restart briefly.";
        onApply();
        try {
            const response = await request("/api/exposure", {
                method: "POST", headers: { "Content-Type": "application/json" },
                body: JSON.stringify(payload), signal: controller.signal, cache: "no-store"
            });
            const result = await response.json();
            if (!response.ok) throw new Error(result.error || `Exposure request failed (HTTP ${response.status}).`);
            if (!result.exposure_control || !Array.isArray(result.cameras)) throw new Error("Invalid exposure response.");
            latest = result;
            loadValues();
            message.textContent = "Applied and read back. Waiting for fresh frames after the warm-up.";
        } catch (error) {
            message.textContent = error.name === "AbortError"
                ? "Request timed out. The outcome is uncertain; reload current values before retrying."
                : `Not applied: ${error.message}`;
        } finally {
            clearTimeout(deadline);
            controller = null;
            pending = false;
            render();
        }
    });
    function update(status) {
        if (disposed) return;
        latest = status;
        if (!status.exposure_control) {
            message.textContent = "Live exposure controls are not available on this server.";
            render();
            return;
        }
        if (!pending && !dirty) loadValues();
        else if (!pending && (session !== status.session_id || revision !== status.exposure_control.revision)) {
            message.textContent = "The active settings changed. Reload current values before applying this draft.";
        }
        feedback.textContent = status.cameras.map(camera =>
            `${camera.name}: requested ${camera.requested_exposure_us.toLocaleString()} us, ` +
            `estimated ${camera.estimated_exposure_us.toFixed(1)} us, target ${camera.target_fps.toFixed(2)} fps`
        ).join(" | ");
        if (status.exposure_control.error) message.textContent = status.exposure_control.error;
        render();
    }
    render();
    return {
        update,
        resume() { disposed = false; render(); },
        dispose() { disposed = true; if (controller) controller.abort(); }
    };
}
