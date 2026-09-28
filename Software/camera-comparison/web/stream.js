// SPDX-License-Identifier: GPL-3.0-or-later
// Strict, bounded parser for the preview server's multipart JPEG stream.
const MAX_JPEG_BYTES = 8 * 1024 * 1024;
const MAX_HEADER_BYTES = 512;

export class JpegStreamParser {
    constructor() {
        this.pending = new Uint8Array();
        this.header = null;
        this.previousId = 0;
    }

    push(chunk) {
        if (!(chunk instanceof Uint8Array) ||
            this.pending.length + chunk.length > 2 * MAX_JPEG_BYTES + 2 * MAX_HEADER_BYTES) {
            throw new Error("Invalid or oversized JPEG stream chunk.");
        }
        const joined = new Uint8Array(this.pending.length + chunk.length);
        joined.set(this.pending); joined.set(chunk, this.pending.length);
        this.pending = joined;
        let latest = null;
        while (true) {
            if (!this.header) {
                let end = -1;
                for (let i = 0; i + 3 < this.pending.length && i < MAX_HEADER_BYTES; i++) {
                    if (this.pending[i] === 13 && this.pending[i + 1] === 10 &&
                        this.pending[i + 2] === 13 && this.pending[i + 3] === 10) {
                        end = i; break;
                    }
                }
                if (end < 0) {
                    if (this.pending.length > MAX_HEADER_BYTES) throw new Error("Oversized JPEG stream header.");
                    break;
                }
                const text = new TextDecoder("ascii", { fatal: true }).decode(this.pending.subarray(0, end));
                const match = /^--pitrac-frame\r\nContent-Type: image\/jpeg\r\nContent-Length: (\d+)\r\nX-Frame-Id: (\d+)\r\nX-Capture-Monotonic: (\d+\.\d+)$/.exec(text);
                if (!match) throw new Error("Invalid JPEG stream header.");
                const [length, frameId, capturedAt] = match.slice(1).map(Number);
                if (!Number.isSafeInteger(length) || length < 4 || length > MAX_JPEG_BYTES ||
                    !Number.isSafeInteger(frameId) || frameId <= this.previousId ||
                    !Number.isFinite(capturedAt) || capturedAt < 0) {
                    throw new Error("Invalid JPEG stream metadata.");
                }
                this.header = { length, frameId, capturedAt };
                this.pending = this.pending.subarray(end + 4);
            }
            const { length, frameId, capturedAt } = this.header;
            if (this.pending.length < length + 2) break;
            if (this.pending[0] !== 255 || this.pending[1] !== 216 ||
                this.pending[length - 2] !== 255 || this.pending[length - 1] !== 217 ||
                this.pending[length] !== 13 || this.pending[length + 1] !== 10) {
                throw new Error("Invalid JPEG stream frame boundary.");
            }
            latest = { frameId, capturedAt, body: this.pending.slice(0, length) };
            this.previousId = frameId;
            this.pending = this.pending.subarray(length + 2);
            this.header = null;
        }
        return latest; // Skip older complete frames delivered together after a network stall.
    }

    finish() {
        if (this.pending.length || this.header) throw new Error("JPEG stream ended in a partial frame.");
    }
}
