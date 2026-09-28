// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors

export function downloadCapture(blob, filename) {
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = filename;
    document.body.append(link);
    try {
        link.click();
    } finally {
        link.remove();
        setTimeout(() => URL.revokeObjectURL(url), 60000);
    }
}

export function createCaptureControls(section, { request = fetch, save = downloadCapture } = {}) {
    const button = section.querySelector("[data-capture-button]");
    const availability = section.querySelector("[data-capture-availability]");
    const message = section.querySelector("[data-capture-message]");
    let latest = null;
    let connected = false;
    let pending = false;
    let disposed = false;
    let controller = null;

    function available() {
        return !disposed && connected && latest?.snapshot_capture?.available === true &&
            typeof latest.session_id === "string";
    }
    function render() {
        button.disabled = pending || !available();
        button.textContent = pending ? "Capturing..." : "Capture";
        availability.textContent = !connected ? "Waiting for a current server status." :
            latest?.snapshot_capture?.error || (available()
                ? "Both cameras ready. Downloads one ZIP to this PC."
                : "Paired lossless capture is unavailable on this server.");
    }
    button.addEventListener("click", async () => {
        if (pending || !available()) return;
        const session = latest.session_id;
        pending = true;
        controller = new AbortController();
        const deadline = setTimeout(() => controller.abort(), 60000);
        render();
        message.textContent = "Packaging both native frames without reducing bit depth...";
        try {
            const response = await request("/api/capture", {
                method: "POST", headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ session_id: session }), cache: "no-store",
                signal: controller.signal
            });
            const type = (response.headers.get("Content-Type") || "").split(";")[0].trim();
            if (!response.ok) {
                const result = type === "application/json" ? await response.json() : null;
                throw new Error(result?.error || `Capture failed (HTTP ${response.status}).`);
            }
            if (type !== "application/zip" || response.headers.get("X-Preview-Session") !== session) {
                throw new Error("Invalid capture format or preview session.");
            }
            const filename = /^attachment; filename="(pitrac-capture-[A-Za-z0-9_-]+\.zip)"$/.exec(
                response.headers.get("Content-Disposition") || ""
            )?.[1];
            if (!filename) throw new Error("Capture response has no valid download filename.");
            const blob = await response.blob();
            if (!blob.size) throw new Error("Capture download was empty.");
            if (disposed) return;
            save(blob, filename);
            message.textContent = `Download requested: ${filename}. Check your browser downloads.`;
        } catch (error) {
            message.textContent = error.name === "AbortError"
                ? "Capture download cancelled or timed out. No completed download was requested; try again."
                : `Capture not downloaded: ${error.message}`;
        } finally {
            clearTimeout(deadline);
            controller = null;
            pending = false;
            render();
        }
    });
    render();
    return {
        update(status) { latest = status; render(); },
        setConnected(value) { connected = value; render(); },
        invalidate() { latest = null; render(); },
        resume() { disposed = false; connected = false; render(); },
        dispose() { disposed = true; controller?.abort(); render(); }
    };
}
