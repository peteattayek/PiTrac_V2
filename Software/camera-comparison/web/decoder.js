// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors

function validate(config, bytes) {
    if (!config || typeof config !== "object") {
        throw new TypeError("A raw frame configuration is required.");
    }
    for (const key of ["width", "height", "stride", "sizeimage"]) {
        if (!Number.isSafeInteger(config[key]) || config[key] <= 0) {
            throw new RangeError(`${key} must be a positive safe integer.`);
        }
    }
    const { width, height, stride, sizeimage, fourcc } = config;
    if (fourcc !== "GREY" && fourcc !== "Y10P") {
        throw new RangeError(`Unsupported raw format: ${fourcc}.`);
    }
    if (fourcc === "Y10P" && width % 4 !== 0) {
        throw new RangeError("Y10P width must contain complete groups of four pixels.");
    }
    const rowBytes = fourcc === "GREY" ? width : (width / 4) * 5;
    if (stride < rowBytes) {
        throw new RangeError("Stride is smaller than a packed pixel row.");
    }
    if (!Number.isSafeInteger(stride * height) || sizeimage < stride * height) {
        throw new RangeError("sizeimage is smaller than stride times height.");
    }
    if (!Number.isSafeInteger(width * height * 4) || width * height * 4 > 0x7fffffff) {
        throw new RangeError("Frame dimensions exceed the RGBA buffer limit.");
    }
    let raw;
    if (bytes instanceof ArrayBuffer) {
        raw = new Uint8Array(bytes);
    } else if (bytes instanceof Uint8Array || bytes instanceof Uint8ClampedArray) {
        raw = new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    } else {
        throw new TypeError("Raw bytes must be an ArrayBuffer or unsigned byte array.");
    }
    if (raw.byteLength !== sizeimage) {
        throw new RangeError(`Raw body length ${raw.byteLength} does not match sizeimage ${sizeimage}.`);
    }
    return raw;
}

/**
 * Return native-sized, opaque RGBA bytes without normalization or enhancement.
 * GREY maps directly to RGB; Y10P maps each native 10-bit value using value >> 2.
 * Row padding and any trailing allocation bytes are not image pixels.
 */
export function decodeRawToRgba(config, bytes) {
    const raw = validate(config, bytes);
    const { width, height, stride, fourcc } = config;
    const rgba = new Uint8ClampedArray(width * height * 4);
    let out = 0;
    for (let y = 0; y < height; y += 1) {
        const row = y * stride;
        if (fourcc === "GREY") {
            for (let x = 0; x < width; x += 1) {
                const value = raw[row + x];
                rgba[out++] = value;
                rgba[out++] = value;
                rgba[out++] = value;
                rgba[out++] = 255;
            }
        } else {
            for (let group = 0; group < width / 4; group += 1) {
                const offset = row + group * 5;
                // The four high bytes are exactly the unpacked native values >> 2.
                for (let pixel = 0; pixel < 4; pixel += 1) {
                    const value = raw[offset + pixel];
                    rgba[out++] = value;
                    rgba[out++] = value;
                    rgba[out++] = value;
                    rgba[out++] = 255;
                }
            }
        }
    }
    return rgba;
}

/** Return the unmodified native 8- or 10-bit pixel value at integer x, y. */
export function rawPixelAt(config, bytes, x, y) {
    const raw = validate(config, bytes);
    if (!Number.isSafeInteger(x) || !Number.isSafeInteger(y) ||
        x < 0 || y < 0 || x >= config.width || y >= config.height) {
        throw new RangeError("Pixel coordinates must be integers inside the frame.");
    }
    const row = y * config.stride;
    if (config.fourcc === "GREY") {
        return raw[row + x];
    }
    const offset = row + Math.floor(x / 4) * 5;
    const pixel = x % 4;
    return (raw[offset + pixel] << 2) | ((raw[offset + 4] >> (pixel * 2)) & 3);
}
