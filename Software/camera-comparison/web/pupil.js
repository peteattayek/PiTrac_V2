// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors

import { downloadCapture } from "./capture.js";
import { buildArchive, capturePng } from "./archive.js";

export function measurementSettings(values) {
    const lens = values.lens.trim();
    if (!lens || lens.length > 120) throw new Error("Enter a lens identifier (up to 120 characters).");
    if (!["mira220", "imx296"].includes(values.camera)) throw new Error("Choose a camera.");
    if (!["yaw", "pitch"].includes(values.rotation)) throw new Error("Choose the rotation plane.");
    if (!values.position.trim() || !Number.isFinite(Number(values.position))) {
        throw new Error("Enter a finite rail position in millimeters.");
    }
    const angle = Number(values.angle);
    if (!values.angle.trim() || !Number.isFinite(angle) || angle <= 0 || angle >= 90) {
        throw new Error("Side-angle magnitude must be greater than 0 and less than 90 degrees. " +
            "Keep it at 5 for +/-5 degrees; choose 0 in Capture angle for the centered image.");
    }
    return { lens, camera: values.camera, position_mm: Number(values.position),
        angle_degrees: angle, rotation: values.rotation, setup_notes: values.notes.trim() };
}

function angleName(angle) {
    return `${angle < 0 ? "minus" : angle > 0 ? "plus" : "zero"}-${Math.abs(angle)}deg`;
}

function angleChoices(magnitude) {
    return { negative: -magnitude, positive: magnitude, zero: 0 };
}

export async function tripletArchive(settings, captures, session, id) {
    const angles = Object.values(angleChoices(settings.angle_degrees));
    if (captures.length !== 3 || angles.some(angle => captures.filter(capture => capture.angle === angle).length !== 1)) {
        throw new Error("A complete triplet with one -angle, +angle and 0-degree image is required.");
    }
    const ordered = angles.map(angle => captures.find(capture => capture.angle === angle));
    const images = ordered.map(capture => ({
        angle_degrees: capture.angle,
        png_file: `${angleName(capture.angle)}.png`,
        original_capture_file: `captures/${angleName(capture.angle)}.zip`,
        original_download_filename: capture.filename,
        browser_request_utc: capture.requestedUtc
    }));
    const manifest = {
        schema: 1, measurement: "entrance-pupil-no-parallax", set_id: id,
        ...settings, preview_session_id: session,
        position_reference: {
            zero: "Rotation axis at the camera PCB surface.",
            positive: "Rotation axis toward the lens/front of the camera.",
            interpretation: "Axis position relative to the PCB, not target distance."
        },
        angle_reference: "User-labelled negative/positive directions on the rig; 0 is the centered view.",
        acquisition: "Manual selection of -angle, +angle and 0 at one rail position, in any order.",
        capture_order_degrees: captures.map(capture => capture.angle),
        images,
        camera_metadata: "Each original capture ZIP contains metadata.json with actual exposure, gain, frame IDs, receive times and SHA-256 hashes."
    };
    const entries = [{ name: "measurement.json", data: JSON.stringify(manifest, null, 2) + "\n" }];
    for (let i = 0; i < ordered.length; i++) {
        entries.push({ name: images[i].png_file, data: ordered[i].png },
            { name: images[i].original_capture_file, data: ordered[i].blob });
    }
    return buildArchive(entries);
}

export function createPupilControls(section, { request = fetch, save = downloadCapture } = {}) {
    const fields = Object.fromEntries(["camera", "lens", "position", "angle", "rotation", "notes"].map(
        name => [name, section.querySelector(`[data-pupil-${name}]`)]
    ));
    const capture = section.querySelector("[data-pupil-capture]");
    const angleSelect = section.querySelector("[data-pupil-capture-angle]");
    const download = section.querySelector("[data-pupil-download]");
    const discard = section.querySelector("[data-pupil-discard]");
    const next = section.querySelector("[data-pupil-next]");
    const progress = section.querySelector("[data-pupil-progress]");
    const message = section.querySelector("[data-pupil-message]");
    let latest = null, connected = false, disposed = false, pending = false;
    let active = null, captures = [], saved = false, generation = 0, controller = null;

    function available() {
        return !disposed && connected &&
            latest?.snapshot_capture?.by_camera?.[fields.camera.value]?.available === true &&
            typeof latest.session_id === "string";
    }
    function render() {
        const angle = active?.settings.angle_degrees ?? Number(fields.angle.value);
        const choices = angleChoices(angle);
        const remaining = Object.keys(choices).filter(
            name => !captures.some(capture => capture.angle === choices[name])
        );
        if (remaining.length && !remaining.includes(angleSelect.value)) angleSelect.value = remaining[0];
        for (const [name, value] of Object.entries(choices)) {
            const option = angleSelect.querySelector(`option[value="${name}"]`);
            option.disabled = !remaining.includes(name);
            option.textContent = `${value > 0 ? "+" : ""}${value} degrees${option.disabled ? " (captured)" : ""}`;
        }
        angleSelect.disabled = disposed || pending || captures.length === 3;
        const target = choices[angleSelect.value];
        capture.disabled = pending || !available() || captures.length === 3;
        capture.textContent = pending ? "Working..." :
            captures.length === 3 ? "Triplet complete" : `Capture ${target > 0 ? "+" : ""}${target} degrees`;
        download.disabled = disposed || pending || captures.length !== 3;
        next.disabled = disposed || pending || !saved;
        discard.disabled = !active && !pending;
        for (const field of Object.values(fields)) field.disabled = pending || active !== null;
        progress.textContent = captures.length === 3
            ? `3/3 captured at ${active.settings.position_mm} mm. Download before advancing.`
            : `${captures.length}/3 captured. Move the rig manually to the prompted angle; keep the rail position fixed.`;
        if (!available() && captures.length < 3) {
            progress.textContent += !connected ? " Waiting for a current server status." :
                ` ${latest?.snapshot_capture?.by_camera?.[fields.camera.value]?.error || "Selected-camera capture is unavailable; update the preview server."}`;
        }
    }
    function clear(reason) {
        generation++;
        controller?.abort();
        active = null;
        captures = [];
        saved = false;
        pending = false;
        angleSelect.value = "zero";
        message.textContent = reason;
        render();
    }
    for (const field of Object.values(fields)) field.addEventListener("input", () => {
        if (!active && !pending) {
            message.textContent = "Draft changed. Confirm the actual rig position and lens settings before capturing.";
        }
        render();
    });
    angleSelect.addEventListener("change", () => {
        render();
        message.textContent = "Move the rig to the selected capture angle and let it settle; keep the rail position fixed.";
    });
    discard.addEventListener("click", () => clear("Triplet discarded. Set the rail position and start again."));
    next.addEventListener("click", () => {
        if (!saved || pending || disposed) return;
        const position = active.settings.position_mm + 1;
        fields.position.value = String(position);
        clear(`Next position is ${position} mm. Move the rail manually by +1 mm before capturing.`);
    });
    capture.addEventListener("click", async () => {
        if (pending || !available() || captures.length === 3) return;
        try {
            if (!active) {
                const settings = measurementSettings(Object.fromEntries(
                    Object.entries(fields).map(([name, field]) => [name, field.value])
                ));
                active = { settings, session: latest.session_id, id: crypto.randomUUID() };
            }
        } catch (error) {
            message.textContent = error.message;
            return;
        }
        const set = active, token = generation;
        const angle = angleChoices(set.settings.angle_degrees)[angleSelect.value];
        if (!Number.isFinite(angle) || captures.some(capture => capture.angle === angle)) {
            message.textContent = "Choose an uncaptured angle before taking another image.";
            return;
        }
        const requestedUtc = new Date().toISOString();
        const operationController = new AbortController();
        controller = operationController;
        const deadline = setTimeout(() => operationController.abort(), 60000);
        pending = true;
        render();
        message.textContent = `Capturing ${angle} degrees at ${set.settings.position_mm} mm...`;
        try {
            const response = await request("/api/capture", {
                method: "POST", headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ session_id: set.session, camera: set.settings.camera }), cache: "no-store",
                signal: operationController.signal
            });
            const type = (response.headers.get("Content-Type") || "").split(";")[0].trim();
            if (!response.ok) {
                const result = type === "application/json" ? await response.json() : null;
                throw new Error(result?.error || `Capture failed (HTTP ${response.status}).`);
            }
            if (type !== "application/zip" || response.headers.get("X-Preview-Session") !== set.session) {
                throw new Error("Invalid capture format or preview session.");
            }
            const filename = /^attachment; filename="(pitrac-capture-[A-Za-z0-9_-]+\.zip)"$/.exec(
                response.headers.get("Content-Disposition") || ""
            )?.[1];
            if (!filename) throw new Error("Capture response has no valid download filename.");
            const blob = await response.blob();
            const png = await capturePng(blob, `${set.settings.camera}.png`);
            if (disposed || token !== generation) return;
            captures.push({ angle, blob, png, filename, requestedUtc });
            message.textContent = captures.length === 3
                ? "Triplet ready. Download it, then check your browser downloads."
                : "Image retained in this tab. Choose any remaining capture angle; do not move the rail.";
        } catch (error) {
            if (token === generation && !disposed) {
                message.textContent = error.name === "AbortError"
                    ? "Capture cancelled or timed out. This angle was not added; retry at the same angle."
                    : `Angle not captured: ${error.message}`;
            }
        } finally {
            clearTimeout(deadline);
            if (token === generation) { controller = null; pending = false; render(); }
        }
    });
    download.addEventListener("click", async () => {
        if (disposed || pending || captures.length !== 3) return;
        const set = active, token = generation;
        pending = true;
        render();
        try {
            const blob = await tripletArchive(set.settings, captures, set.session, set.id);
            if (disposed || token !== generation) return;
            const filename = `pitrac-pupil-${set.settings.camera}-${set.settings.position_mm}mm-${set.id}.zip`;
            save(blob, filename);
            saved = true;
            message.textContent = `Download requested: ${filename}. Check it before moving to the next position.`;
        } catch (error) {
            if (token === generation) message.textContent = `Triplet not downloaded: ${error.message}`;
        } finally {
            if (token === generation) { pending = false; render(); }
        }
    });
    render();
    return {
        update(status) {
            latest = status;
            if (active && status.session_id !== active.session) {
                clear("Preview/exposure changed. Triplet discarded; recapture all three angles with unchanged settings.");
            }
            render();
        },
        setConnected(value) { connected = value; render(); },
        invalidate() {
            latest = null;
            clear("Exposure is changing. Start a new triplet after fresh frames are available.");
        },
        hasUnsavedSet() { return active !== null && !saved; },
        dispose() { disposed = true; connected = false; clear("Tab left. Any undownloaded triplet was discarded."); },
        resume() { disposed = false; connected = false; render(); }
    };
}
