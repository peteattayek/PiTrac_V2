// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors

import { decodeRawToRgba, rawPixelAt } from "./decoder.js";
import { createExposureControls } from "./exposure.js";
import { createCaptureControls } from "./capture.js";
import { createPupilControls } from "./pupil.js";

const STATUS_INTERVAL_MS = 1000;
const STATUS_TIMEOUT_MS = 3000;
const FRAME_TIMEOUT_MS = 4000;
const STATUS_STALE_MS = 3500;
const RATE_WINDOW_MS = 4000;
const connection = document.querySelector("#connection-state");
const refreshBudget = document.querySelector("#refresh-budget");
const transportSelect = document.querySelector("#transport-mode");
const transportNote = document.querySelector("#transport-note");
let previewFps = 10;
let transport = null;
let sessionId = null;
let lastStatusAt = null;
let lastStatusRequestStarted = -Infinity;
let statusError = null;
let statusInFlight = false;
let statusController = null;
let statusTimer = null;
let healthTimer = null;
let disposed = false;

function setText(element, text) {
    if (element.textContent !== text) {
        element.textContent = text;
    }
}

function numberText(value, digits = 1, suffix = "") {
    return Number.isFinite(value) ? `${value.toFixed(digits)}${suffix}` : "--";
}

function errorText(error) {
    return String(error instanceof Error ? error.message : error).slice(0, 400);
}

function frameInterval() {
    return 1000 / previewFps;
}

function staleInterval() {
    return Math.max(1500, frameInterval() * 3);
}

const cameras = [...document.querySelectorAll("[data-camera]")].map((panel) => {
    const canvas = panel.querySelector("canvas");
    return {
        name: panel.dataset.camera,
        panel,
        canvas,
        context: canvas.getContext("2d", { alpha: false, colorSpace: "srgb" }),
        viewport: panel.querySelector(".viewport"),
        overlay: panel.querySelector(".frame-overlay"),
        fields: Object.fromEntries(
            [...panel.querySelectorAll("[data-field]")].map((element) => [element.dataset.field, element])
        ),
        zoom: "native",
        config: null,
        rawConfig: null,
        missing: false,
        raw: null,
        hasFrame: false,
        frameId: null,
        afterId: null,
        frameReceivedAt: null,
        frameRequestStarted: null,
        frameError: null,
        renderTimes: [],
        rateStartedAt: null,
        lastRequestStarted: -Infinity,
        generation: 0,
        inFlight: false,
        controller: null,
        timer: null,
        drag: null,
        pointer: null
    };
});

const captureSection = document.querySelector("#capture-controls");
const captureControls = captureSection ? createCaptureControls(captureSection) : null;
const pupilSection = document.querySelector("#pupil-controls");
const pupilControls = pupilSection ? createPupilControls(pupilSection) : null;
const exposureForm = document.querySelector("#exposure-controls");
const exposureControls = exposureForm ? createExposureControls(exposureForm, {
    onApply() {
        captureControls?.invalidate();
        pupilControls?.invalidate();
        for (const camera of cameras) {
            camera.generation += 1;
            if (camera.controller) camera.controller.abort();
            camera.afterId = null;
            camera.frameError = "Exposure is changing; waiting for fresh capture.";
        }
        refreshHealth();
    }
}) : null;

function centerView(camera) {
    const viewport = camera.viewport;
    viewport.scrollLeft = (viewport.scrollWidth - viewport.clientWidth) / 2;
    viewport.scrollTop = (viewport.scrollHeight - viewport.clientHeight) / 2;
}

function changeZoom(camera, zoom) {
    const viewport = camera.viewport;
    const before = camera.canvas.getBoundingClientRect();
    const bounds = viewport.getBoundingClientRect();
    const middleX = bounds.left + viewport.clientLeft + viewport.clientWidth / 2;
    const middleY = bounds.top + viewport.clientTop + viewport.clientHeight / 2;
    const x = before.width ? (middleX - before.left) / before.width : 0.5;
    const y = before.height ? (middleY - before.top) / before.height : 0.5;
    viewport.classList.replace(`view-${camera.zoom}`, `view-${zoom}`);
    camera.zoom = zoom;
    for (const button of camera.panel.querySelectorAll("[data-zoom]")) {
        button.setAttribute("aria-pressed", String(button.dataset.zoom === zoom));
    }
    setText(camera.fields.scale, {
        fit: "Fit: framing only",
        native: "Native pixels",
        double: "2x nearest-neighbor"
    }[zoom]);
    const after = camera.canvas.getBoundingClientRect();
    viewport.scrollLeft += after.left + x * after.width - middleX;
    viewport.scrollTop += after.top + y * after.height - middleY;
    updatePixel(camera);
}

function updatePixel(camera) {
    if (transport === "jpeg") {
        setText(camera.fields.pixel, "JPEG display: switch to Raw pixels for exact native pixel values.");
        return;
    }
    if (!camera.pointer || !camera.raw) {
        setText(camera.fields.pixel, "Hover over the image to inspect native pixel values.");
        return;
    }
    const rect = camera.canvas.getBoundingClientRect();
    const x = Math.floor((camera.pointer.x - rect.left) * camera.rawConfig.width / rect.width);
    const y = Math.floor((camera.pointer.y - rect.top) * camera.rawConfig.height / rect.height);
    if (x < 0 || y < 0 || x >= camera.rawConfig.width || y >= camera.rawConfig.height) {
        setText(camera.fields.pixel, "Hover over the image to inspect native pixel values.");
        return;
    }
    const value = rawPixelAt(camera.rawConfig, camera.raw, x, y);
    const depth = { GREY: 8, Y10P: 10, Y12P: 12 }[camera.rawConfig.fourcc];
    const displayed = value >> (depth - 8);
    setText(camera.fields.pixel,
        `x ${x}, y ${y} | native ${value}/${(1 << depth) - 1} | display ${displayed}/255`);
}

for (const camera of cameras) {
    for (const button of camera.panel.querySelectorAll("[data-zoom]")) {
        button.addEventListener("click", () => changeZoom(camera, button.dataset.zoom));
    }
    camera.panel.querySelector('[data-action="center"]').addEventListener("click", () => {
        centerView(camera);
        updatePixel(camera);
    });
    camera.viewport.addEventListener("pointerdown", (event) => {
        if (event.button !== 0 || camera.zoom === "fit") {
            return;
        }
        camera.viewport.focus({ preventScroll: true });
        camera.drag = {
            id: event.pointerId,
            x: event.clientX,
            y: event.clientY,
            left: camera.viewport.scrollLeft,
            top: camera.viewport.scrollTop
        };
        camera.viewport.setPointerCapture(event.pointerId);
        camera.viewport.classList.add("is-dragging");
        event.preventDefault();
    });
    camera.viewport.addEventListener("pointermove", (event) => {
        if (camera.drag && camera.drag.id === event.pointerId) {
            camera.viewport.scrollLeft = camera.drag.left + camera.drag.x - event.clientX;
            camera.viewport.scrollTop = camera.drag.top + camera.drag.y - event.clientY;
        }
        camera.pointer = { x: event.clientX, y: event.clientY };
        updatePixel(camera);
    });
    const endDrag = (event) => {
        if (camera.drag && camera.drag.id === event.pointerId) {
            camera.drag = null;
            camera.viewport.classList.remove("is-dragging");
            if (camera.viewport.hasPointerCapture(event.pointerId)) {
                camera.viewport.releasePointerCapture(event.pointerId);
            }
        }
    };
    camera.viewport.addEventListener("pointerup", endDrag);
    camera.viewport.addEventListener("pointercancel", endDrag);
    camera.viewport.addEventListener("lostpointercapture", endDrag);
    camera.viewport.addEventListener("pointerleave", () => {
        if (!camera.drag) {
            camera.pointer = null;
            updatePixel(camera);
        }
    });
    camera.viewport.addEventListener("scroll", () => updatePixel(camera), { passive: true });
}

function showState(camera, title, detail, tone = "waiting") {
    setText(camera.fields.badge, title);
    camera.fields.badge.dataset.tone = tone;
    camera.overlay.hidden = tone === "live";
    setText(camera.fields["overlay-title"], title);
    const retained = camera.hasFrame ? " Last image retained; not live." : "";
    setText(camera.fields["overlay-detail"], detail + (tone === "live" ? "" : retained));
}

function refreshHealth() {
    const now = performance.now();
    const statusExpired = lastStatusAt !== null && now - lastStatusAt > STATUS_STALE_MS;
    captureControls?.setConnected(!disposed && !document.hidden &&
        lastStatusAt !== null && !statusError && !statusExpired);
    pupilControls?.setConnected(!disposed && !document.hidden &&
        lastStatusAt !== null && !statusError && !statusExpired);
    if (document.hidden) {
        setText(connection, "Tab hidden: browser requests paused; capture continues.");
        connection.dataset.tone = "waiting";
    } else if (statusError || statusExpired) {
        setText(connection, `Status unavailable: ${statusError || "No recent status response."}`);
        connection.dataset.tone = "error";
    } else if (lastStatusAt !== null) {
        setText(connection, "Status connected; individual capture states shown below.");
        connection.dataset.tone = "connected";
    }
    for (const camera of cameras) {
        camera.renderTimes = camera.renderTimes.filter((time) => now - time < RATE_WINDOW_MS);
        const duration = camera.rateStartedAt === null
            ? 1000 : Math.max(1000, Math.min(RATE_WINDOW_MS, now - camera.rateStartedAt));
        setText(camera.fields["display-fps"],
            `${(camera.renderTimes.length * 1000 / duration).toFixed(1)} fps`);
        const age = camera.frameReceivedAt === null ? null : now - camera.frameReceivedAt;
        setText(camera.fields.age, age === null ? "--" : `${(age / 1000).toFixed(1)} s`);
        if (document.hidden) {
            showState(camera, "PAUSED", "Browser tab is hidden. Camera capture is not paused.");
        } else if (statusError || statusExpired) {
            showState(camera, "OFFLINE", statusError || "No recent status response.", "error");
        } else if (camera.missing) {
            showState(camera, "UNAVAILABLE", "This camera is not present in the server status.", "error");
        } else if (!camera.config) {
            showState(camera, "STARTING", "Waiting for camera configuration.");
        } else if (camera.config.state === "error") {
            showState(camera, "ERROR", errorText(camera.config.error || "Capture failed."), "error");
        } else if (camera.config.state === "stopped") {
            showState(camera, "STOPPED", "Capture has stopped.");
        } else if (camera.config.state === "starting") {
            showState(camera, "STARTING", "The camera is starting; waiting for confirmed live capture.");
        } else if (camera.config.state !== "live") {
            showState(camera, "ERROR", "Unrecognized camera state.", "error");
        } else if (camera.frameError) {
            showState(camera, "UNAVAILABLE", camera.frameError, "error");
        } else if (!camera.hasFrame) {
            showState(camera, "WAITING", "Capture is running; waiting for the first image.");
        } else {
            const captureSampleOld = Number.isFinite(camera.config.frame_age_ms) &&
                camera.config.frame_id !== null && camera.config.frame_id >= camera.frameId &&
                camera.config.frame_age_ms + now - lastStatusAt > staleInterval();
            // Include delivery time in the conservative stale check, not the age readout.
            if (now - camera.frameRequestStarted > staleInterval() || captureSampleOld) {
                showState(camera, "STALE", "No sufficiently recent frame. Check capture and the SSH tunnel.");
            } else {
                showState(camera, "LIVE", "", "live");
            }
        }
    }
}

function rawConfigKey(config) {
    return config
        ? [config.width, config.height, config.fourcc, config.stride, config.sizeimage].join(":")
        : "";
}

function updateCameraStatus(camera, config, sessionChanged = false) {
    const previous = camera.config;
    const restarted = previous && config && (
        config.frames_received < previous.frames_received ||
        (previous.frame_id !== null && (
            config.frame_id === null || config.frame_id < previous.frame_id
        ))
    );
    if (rawConfigKey(previous) !== rawConfigKey(config) || restarted || sessionChanged) {
        camera.generation += 1;
        if (camera.controller) {
            camera.controller.abort();
        }
        camera.afterId = null;
        camera.frameError = camera.hasFrame ? "Waiting for a new frame after capture changed." : null;
        camera.renderTimes = [];
        camera.rateStartedAt = null;
    }
    camera.config = config;
    camera.missing = !config;
    if (!config) {
        return;
    }
    setText(camera.fields.format,
        `${config.width} x ${config.height} | ${config.fourcc} | stride ${config.stride} B`);
    setText(camera.fields["native-fps"], numberText(config.target_fps, 2, " fps"));
    setText(camera.fields["capture-fps"], numberText(config.capture_fps, 2, " fps"));
    setText(camera.fields["requested-exposure"], numberText(config.requested_exposure_us, 1, " us"));
    setText(camera.fields["estimated-exposure"], numberText(config.estimated_exposure_us, 1, " us"));
    setText(camera.fields.gain, numberText(config.analogue_gain, 2, "x"));
    setText(camera.fields.frames, Number.isSafeInteger(config.frames_received)
        ? config.frames_received.toLocaleString("en-US") : "--");
    const warnings = Number.isSafeInteger(config.timing_warnings) ? config.timing_warnings : null;
    setText(camera.fields.warnings,
        `Timing warnings: ${warnings === null ? "--" : warnings}${warnings > 0 ? " - investigate before recording" : ""}`);
    camera.fields.warnings.classList.toggle("has-warning", warnings > 0);
}

function updateTransportNote() {
    setText(transportNote, transport === "jpeg"
        ? "Smooth: full-resolution, lossy grayscale JPEG for focusing. No sharpening or brightness adjustment."
        : "Raw pixels: lossless native data; full frames may update slowly over Wi-Fi.");
    for (const camera of cameras) updatePixel(camera);
}

transportSelect.addEventListener("change", () => {
    transport = transportSelect.value;
    for (const camera of cameras) {
        camera.generation += 1;
        camera.controller?.abort();
        camera.afterId = null;
        camera.raw = null;
        camera.frameError = "Switching preview mode; waiting for a new image.";
        camera.renderTimes = [];
        camera.rateStartedAt = null;
        if (!camera.inFlight) scheduleFrame(camera, 0);
    }
    updateTransportNote();
    refreshHealth();
});

function scheduleStatus(delay) {
    clearTimeout(statusTimer);
    if (!disposed) {
        statusTimer = setTimeout(pollStatus, delay);
    }
}

async function pollStatus() {
    if (disposed || statusInFlight) {
        return;
    }
    if (document.hidden) {
        scheduleStatus(STATUS_INTERVAL_MS);
        return;
    }
    const remaining = STATUS_INTERVAL_MS - (performance.now() - lastStatusRequestStarted);
    if (remaining > 0) {
        scheduleStatus(remaining);
        return;
    }
    const startedAt = performance.now();
    lastStatusRequestStarted = startedAt;
    statusInFlight = true;
    const controller = new AbortController();
    statusController = controller;
    let timedOut = false;
    const deadline = setTimeout(() => {
        timedOut = true;
        controller.abort();
    }, STATUS_TIMEOUT_MS);
    try {
        const response = await fetch("/api/status", { cache: "no-store", signal: controller.signal });
        if (!response.ok) {
            throw new Error(`Status request returned HTTP ${response.status}.`);
        }
        const status = await response.json();
        if (!status || !Array.isArray(status.cameras) ||
            !status.cameras.every((camera) => camera && typeof camera.name === "string") ||
            new Set(status.cameras.map((camera) => camera.name)).size !== status.cameras.length ||
            !Number.isFinite(status.preview_fps) || status.preview_fps < 0.1 || status.preview_fps > 60) {
            throw new Error("Invalid preview status response.");
        }
        if (status.saves_frames !== false || status.isp !== false) {
            throw new Error("Server did not confirm no-save, no-ISP preview mode.");
        }
        if (disposed) {
            return;
        }
        previewFps = status.preview_fps;
        const sessionChanged = sessionId !== status.session_id;
        sessionId = status.session_id;
        if (transport === null) {
            transport = status.preview_transport === "jpeg" ? "jpeg" : "raw";
            transportSelect.value = transport;
        }
        const jpegAvailable = status.transports?.includes("jpeg") === true;
        transportSelect.querySelector('[value="jpeg"]').disabled = !jpegAvailable;
        transportSelect.disabled = false;
        updateTransportNote();
        lastStatusAt = performance.now();
        statusError = null;
        exposureControls?.update(status);
        captureControls?.update(status);
        pupilControls?.update(status);
        setText(refreshBudget, `Browser request cap: ${previewFps} fps per camera (latest only)`);
        for (const camera of cameras) {
            updateCameraStatus(camera, status.cameras.find((item) => item.name === camera.name) || null, sessionChanged);
            if (!camera.inFlight) {
                scheduleFrame(camera, 0);
            }
        }
    } catch (error) {
        if (!disposed) {
            statusError = timedOut ? "Status request timed out; check the SSH tunnel." : errorText(error);
        }
    } finally {
        clearTimeout(deadline);
        statusInFlight = false;
        statusController = null;
        refreshHealth();
        scheduleStatus(Math.max(0, STATUS_INTERVAL_MS - (performance.now() - startedAt)));
    }
}

function scheduleFrame(camera, delay) {
    clearTimeout(camera.timer);
    if (!disposed) {
        camera.timer = setTimeout(() => pollFrame(camera), delay);
    }
}

async function pollFrame(camera) {
    if (disposed || camera.inFlight) {
        return;
    }
    if (document.hidden || !camera.config) {
        scheduleFrame(camera, STATUS_INTERVAL_MS);
        return;
    }
    const remaining = frameInterval() - (performance.now() - camera.lastRequestStarted);
    if (remaining > 0) {
        scheduleFrame(camera, remaining);
        return;
    }
    const startedAt = performance.now();
    const generation = camera.generation;
    const config = { ...camera.config };
    const requestTransport = transport;
    const requestSession = sessionId;
    const afterId = camera.afterId;
    camera.lastRequestStarted = startedAt;
    camera.inFlight = true;
    if (camera.rateStartedAt === null) {
        camera.rateStartedAt = startedAt;
    }
    const controller = new AbortController();
    camera.controller = controller;
    let timedOut = false;
    const deadline = setTimeout(() => {
        timedOut = true;
        controller.abort();
    }, FRAME_TIMEOUT_MS);
    try {
        const after = afterId === null ? "" : `&after=${afterId}`;
        const response = await fetch(`/api/frame/${camera.name}?format=${requestTransport}${after}`, {
            cache: "no-store",
            signal: controller.signal
        });
        if (response.status === 204) {
            return;
        }
        if (!response.ok) {
            let detail = "";
            if ((response.headers.get("Content-Type") || "").includes("application/json")) {
                const body = await response.json();
                detail = typeof body.error === "string" ? ` ${body.error}` : "";
            }
            throw new Error(`Frame unavailable (HTTP ${response.status}).${detail}`);
        }
        const expectedType = requestTransport === "jpeg" ? "image/jpeg" : "application/octet-stream";
        if ((response.headers.get("Content-Type") || "").split(";")[0].trim() !== expectedType) {
            throw new Error("Frame response has an unexpected format.");
        }
        if (requestSession && response.headers.get("X-Preview-Session") !== requestSession) {
            throw new Error("Preview restarted; waiting for its new status.");
        }
        const idHeader = response.headers.get("X-Frame-Id");
        const frameId = Number(idHeader);
        if (!idHeader || !/^\d+$/.test(idHeader) || !Number.isSafeInteger(frameId) ||
            (afterId !== null && frameId <= afterId)) {
            throw new Error("Missing, invalid, or non-newer raw frame ID.");
        }
        // fetch transparently removes lossless HTTP Content-Encoding, including gzip.
        const body = await response.arrayBuffer();
        const receivedAt = performance.now();
        if (disposed || document.hidden || generation !== camera.generation) {
            return;
        }
        if (!camera.context) {
            throw new Error("This browser cannot create a 2D canvas.");
        }
        const resize = camera.canvas.width !== config.width || camera.canvas.height !== config.height;
        if (resize) {
            camera.canvas.width = config.width;
            camera.canvas.height = config.height;
        }
        camera.context.imageSmoothingEnabled = false;
        if (requestTransport === "jpeg") {
            const bitmap = await createImageBitmap(new Blob([body], { type: "image/jpeg" }));
            try {
                if (disposed || document.hidden || generation !== camera.generation) return;
                if (bitmap.width !== config.width || bitmap.height !== config.height) {
                    throw new Error("JPEG dimensions differ from the native image dimensions.");
                }
                camera.context.drawImage(bitmap, 0, 0);
            } finally {
                bitmap.close();
            }
            camera.raw = null;
        } else {
            const raw = new Uint8Array(body);
            const rgba = decodeRawToRgba(config, raw);
            camera.context.putImageData(new ImageData(rgba, config.width, config.height), 0, 0);
            camera.raw = raw;
        }
        camera.hasFrame = true;
        camera.rawConfig = config;
        camera.frameId = frameId;
        camera.afterId = frameId;
        camera.frameReceivedAt = receivedAt;
        camera.frameRequestStarted = startedAt;
        camera.frameError = null;
        camera.renderTimes.push(performance.now());
        setText(camera.fields["frame-id"], String(frameId));
        if (resize) {
            centerView(camera);
        }
        updatePixel(camera);
    } catch (error) {
        if (!disposed && generation === camera.generation) {
            camera.frameError = timedOut ? "Frame request timed out." : errorText(error);
        }
    } finally {
        clearTimeout(deadline);
        camera.inFlight = false;
        camera.controller = null;
        refreshHealth();
        scheduleFrame(camera, Math.max(0, frameInterval() - (performance.now() - startedAt)));
    }
}

function startPolling() {
    disposed = false;
    scheduleStatus(0);
    for (const camera of cameras) {
        scheduleFrame(camera, 0);
    }
    clearInterval(healthTimer);
    healthTimer = setInterval(refreshHealth, 250);
    refreshHealth();
}

document.addEventListener("visibilitychange", () => {
    refreshHealth();
    if (!document.hidden && !disposed) {
        if (!statusInFlight) {
            scheduleStatus(0);
        }
        for (const camera of cameras) {
            if (!camera.inFlight) {
                scheduleFrame(camera, 0);
            }
        }
    }
});

window.addEventListener("pagehide", () => {
    exposureControls?.dispose();
    captureControls?.dispose();
    pupilControls?.dispose();
    disposed = true;
    clearTimeout(statusTimer);
    clearInterval(healthTimer);
    if (statusController) {
        statusController.abort();
    }
    for (const camera of cameras) {
        clearTimeout(camera.timer);
        if (camera.controller) {
            camera.controller.abort();
        }
    }
});

window.addEventListener("pageshow", (event) => {
    if (event.persisted) {
        exposureControls?.resume();
        captureControls?.resume();
        pupilControls?.resume();
        window.addEventListener("beforeunload", event => {
            if (pupilControls?.hasUnsavedSet()) {
                event.preventDefault();
                event.returnValue = "";
            }
        });

        startPolling();
    }
});

startPolling();
