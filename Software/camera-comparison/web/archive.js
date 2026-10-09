// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors

const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true });
const crcTable = Uint32Array.from({ length: 256 }, (_, value) => {
    for (let bit = 0; bit < 8; bit++) value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0);
    return value >>> 0;
});

export function crc32(bytes) {
    let crc = 0xffffffff;
    for (const byte of bytes) crc = (crc >>> 8) ^ crcTable[(crc ^ byte) & 255];
    return (crc ^ 0xffffffff) >>> 0;
}

// The preview's PNG entries are ZIP_STORED; compressed RAW/metadata entries are skipped.
export async function capturePng(blob, filename) {
    const bytes = new Uint8Array(await blob.arrayBuffer());
    const view = new DataView(bytes.buffer);
    let offset = 0;
    while (offset + 4 <= bytes.length && view.getUint32(offset, true) === 0x04034b50) {
        if (offset + 30 > bytes.length) throw new Error("Truncated capture ZIP header.");
        const flags = view.getUint16(offset + 6, true);
        if (flags & ~0x800) throw new Error("Unsupported capture ZIP flags.");
        const size = view.getUint32(offset + 18, true);
        const nameLength = view.getUint16(offset + 26, true);
        const extraLength = view.getUint16(offset + 28, true);
        const start = offset + 30 + nameLength + extraLength;
        const end = start + size;
        if (end > bytes.length) throw new Error("Truncated capture ZIP entry.");
        const name = decoder.decode(bytes.subarray(offset + 30, offset + 30 + nameLength));
        if (name === filename) {
            if (view.getUint16(offset + 8, true) !== 0 ||
                view.getUint32(offset + 22, true) !== size) {
                throw new Error("The capture PNG must be stored losslessly without ZIP compression.");
            }
            const png = bytes.slice(start, end);
            if (crc32(png) !== view.getUint32(offset + 14, true)) {
                throw new Error("Capture PNG failed its ZIP checksum.");
            }
            if (png.length < 8 || ![137, 80, 78, 71, 13, 10, 26, 10].every((byte, i) => png[i] === byte)) {
                throw new Error("Capture entry is not a PNG.");
            }
            return png;
        }
        offset = end;
    }
    throw new Error(`Capture ZIP does not contain ${filename}.`);
}

export async function buildArchive(entries) {
    if (!entries.length || entries.length > 65535) throw new Error("Invalid ZIP entry count.");
    const locals = [], directory = [], names = new Set();
    let offset = 0;
    for (const { name, data } of entries) {
        if (!/^[A-Za-z0-9_./+-]+$/.test(name) ||
            name.split("/").some(part => !part || part === "." || part === "..") || names.has(name)) {
            throw new Error("Invalid or duplicate ZIP filename.");
        }
        names.add(name);
        const encodedName = encoder.encode(name);
        const bytes = typeof data === "string" ? encoder.encode(data) :
            data instanceof Blob ? new Uint8Array(await data.arrayBuffer()) : data;
        if (!(bytes instanceof Uint8Array) || encodedName.length > 65535 ||
            bytes.length > 0xffffffff || offset + 30 + encodedName.length + bytes.length > 0xffffffff) {
            throw new Error("Triplet exceeds the supported ZIP size.");
        }
        const crc = crc32(bytes);
        const header = new Uint8Array(30);
        const view = new DataView(header.buffer);
        view.setUint32(0, 0x04034b50, true);
        view.setUint16(4, 20, true);
        view.setUint16(12, 33, true); // Valid DOS date: 1980-01-01. Capture times are in metadata.
        view.setUint32(14, crc, true);
        view.setUint32(18, bytes.length, true);
        view.setUint32(22, bytes.length, true);
        view.setUint16(26, encodedName.length, true);
        locals.push(header, encodedName, bytes);
        const central = new Uint8Array(46);
        const centralView = new DataView(central.buffer);
        centralView.setUint32(0, 0x02014b50, true);
        centralView.setUint16(4, 20, true);
        centralView.setUint16(6, 20, true);
        centralView.setUint16(14, 33, true);
        centralView.setUint32(16, crc, true);
        centralView.setUint32(20, bytes.length, true);
        centralView.setUint32(24, bytes.length, true);
        centralView.setUint16(28, encodedName.length, true);
        centralView.setUint32(42, offset, true);
        directory.push(central, encodedName);
        offset += header.length + encodedName.length + bytes.length;
    }
    const directorySize = directory.reduce((sum, bytes) => sum + bytes.length, 0);
    if (offset + directorySize > 0xffffffff) throw new Error("Triplet exceeds the supported ZIP size.");
    const end = new Uint8Array(22);
    const endView = new DataView(end.buffer);
    endView.setUint32(0, 0x06054b50, true);
    endView.setUint16(8, entries.length, true);
    endView.setUint16(10, entries.length, true);
    endView.setUint32(12, directorySize, true);
    endView.setUint32(16, offset, true);
    return new Blob([...locals, ...directory, end], { type: "application/zip" });
}
