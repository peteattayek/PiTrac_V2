// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// Run with: node --test Software\camera-comparison\web\decoder.test.mjs

import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

// Import as an ES module without needing a package.json or changing project tooling.
const source = await readFile(new URL("./decoder.js", import.meta.url), "utf8");
const { decodeRawToRgba, rawPixelAt } = await import(
    `data:text/javascript;base64,${Buffer.from(source).toString("base64")}`
);

function config(fourcc, width, height = 1, padding = 0) {
    const stride = (fourcc === "GREY" ? width : fourcc === "Y10P" ? width / 4 * 5 : width / 2 * 3) + padding;
    return { fourcc, width, height, stride, sizeimage: stride * height };
}

function pack(values, destination, offset) {
    let low = 0;
    values.forEach((value, index) => {
        destination[offset + index] = value >> 2;
        low |= (value & 3) << (index * 2);
    });
    destination[offset + 4] = low;
}

function assertPixel(rgba, width, x, y, value) {
    const index = (y * width + x) * 4;
    assert.deepEqual([...rgba.subarray(index, index + 4)], [value, value, value, 255]);
}

function pack12(values, destination, offset) {
    destination[offset] = values[0] >> 4;
    destination[offset + 1] = values[1] >> 4;
    destination[offset + 2] = (values[0] & 15) | ((values[1] & 15) << 4);
}

test("Y12P preserves every native 12-bit value and maps display using only value >> 4", () => {
    const layout = config("Y12P", 4096, 2, 16);
    const bytes = new Uint8Array(layout.sizeimage).fill(199);
    for (let y = 0; y < 2; y++) {
        for (let x = 0; x < 4096; x += 2) {
            pack12([0, 1].map(i => y === 0 ? x + i : 4095 - x - i),
                bytes, y * layout.stride + x / 2 * 3);
        }
    }
    const rgba = decodeRawToRgba(layout, bytes);
    for (let y = 0; y < 2; y++) {
        for (let x = 0; x < 4096; x++) {
            const value = y === 0 ? x : 4095 - x;
            assert.equal(rawPixelAt(layout, bytes, x, y), value);
            assertPixel(rgba, layout.width, x, y, value >> 4);
        }
    }
});

test("full Mira220 RAW12 dimensions retain row boundaries and low nibbles", () => {
    const layout = config("Y12P", 1600, 1400);
    assert.equal(layout.stride, 2400);
    const bytes = new Uint8Array(layout.sizeimage).fill(199);
    for (let y = 0; y < layout.height; y++) {
        pack12([(y % 256) * 16 + 1, 0], bytes, y * layout.stride);
        pack12([0, ((y + 71) % 256) * 16 + 15], bytes, y * layout.stride + 2397);
    }
    const rgba = decodeRawToRgba(layout, bytes);
    for (let y = 0; y < layout.height; y++) {
        assertPixel(rgba, layout.width, 0, y, y % 256);
        assertPixel(rgba, layout.width, 1599, y, (y + 71) % 256);
        assert.equal(rawPixelAt(layout, bytes, 0, y), (y % 256) * 16 + 1);
        assert.equal(rawPixelAt(layout, bytes, 1599, y), ((y + 71) % 256) * 16 + 15);
    }
});

test("GREY is direct linear RGB with opaque alpha and ignores row padding", () => {
    const layout = config("GREY", 3, 2, 2);
    const bytes = new Uint8Array([0, 127, 255, 222, 223, 1, 128, 254, 224, 225]);
    const rgba = decodeRawToRgba(layout, bytes);
    assert.ok(rgba instanceof Uint8ClampedArray);
    assert.equal(rgba.length, 3 * 2 * 4);
    for (const [x, y, value] of [[0, 0, 0], [1, 0, 127], [2, 0, 255], [0, 1, 1], [1, 1, 128], [2, 1, 254]]) {
        assertPixel(rgba, 3, x, y, value);
        assert.equal(rawPixelAt(layout, bytes, x, y), value);
    }
});

test("Y10P decodes every 10-bit value and preserves each low-bit position in hover readout", () => {
    const layout = config("Y10P", 1024, 2, 4);
    const bytes = new Uint8Array(layout.sizeimage).fill(255);
    for (let y = 0; y < 2; y += 1) {
        for (let x = 0; x < 1024; x += 4) {
            pack([0, 1, 2, 3].map((i) => y === 0 ? x + i : 1023 - x - i),
                bytes, y * layout.stride + x / 4 * 5);
        }
    }
    const rgba = decodeRawToRgba(layout, bytes);
    for (let y = 0; y < 2; y += 1) {
        for (let x = 0; x < 1024; x += 1) {
            const value = y === 0 ? x : 1023 - x;
            assert.equal(rawPixelAt(layout, bytes, x, y), value);
            assertPixel(rgba, layout.width, x, y, value >> 2);
        }
    }
});

test("actual Mira220 1600 x 1400 and IMX296 1456 x 1088 strides preserve row boundaries", () => {
    for (const layout of [config("GREY", 1600, 1400), config("Y10P", 1456, 1088, 4)]) {
        assert.equal(layout.stride, layout.fourcc === "GREY" ? 1600 : 1824);
        const bytes = new Uint8Array(layout.sizeimage).fill(199);
        for (let y = 0; y < layout.height; y += 1) {
            const first = y % 256;
            const last = (y + 71) % 256;
            if (layout.fourcc === "GREY") {
                bytes[y * layout.stride] = first;
                bytes[y * layout.stride + layout.width - 1] = last;
            } else {
                pack([first * 4 + 1, 0, 0, 0], bytes, y * layout.stride);
                pack([0, 0, 0, last * 4 + 3], bytes, y * layout.stride + (layout.width / 4 - 1) * 5);
            }
        }
        const rgba = decodeRawToRgba(layout, bytes);
        assert.equal(rgba.length, layout.width * layout.height * 4);
        for (let y = 0; y < layout.height; y += 1) {
            const first = y % 256;
            const last = (y + 71) % 256;
            assertPixel(rgba, layout.width, 0, y, first);
            assertPixel(rgba, layout.width, layout.width - 1, y, last);
            assert.equal(rawPixelAt(layout, bytes, 0, y), layout.fourcc === "GREY" ? first : first * 4 + 1);
            assert.equal(rawPixelAt(layout, bytes, layout.width - 1, y),
                layout.fourcc === "GREY" ? last : last * 4 + 3);
        }
    }
});

test("ArrayBuffers, offset byte views, and trailing sizeimage bytes are handled exactly", () => {
    const layout = { ...config("GREY", 2), sizeimage: 4 };
    const storage = new Uint8Array([91, 92, 12, 240, 88, 89, 93]);
    const view = storage.subarray(2, 6);
    assert.deepEqual([...decodeRawToRgba(layout, view)], [12, 12, 12, 255, 240, 240, 240, 255]);
    assert.equal(rawPixelAt(layout, view, 1, 0), 240);
    assert.deepEqual(decodeRawToRgba(layout, view.slice().buffer), decodeRawToRgba(layout, view));
    assert.deepEqual(decodeRawToRgba(layout, new Uint8ClampedArray(view)), decodeRawToRgba(layout, view));
});

test("no brightness normalization occurs between dark and bright frames", () => {
    const layout = config("GREY", 2);
    assertPixel(decodeRawToRgba(layout, new Uint8Array([10, 11])), 2, 0, 0, 10);
    assertPixel(decodeRawToRgba(layout, new Uint8Array([10, 255])), 2, 0, 0, 10);
    const tenBit = config("Y10P", 4);
    const raw = new Uint8Array(5);
    pack([0, 1, 2, 3], raw, 0);
    assert.deepEqual([...decodeRawToRgba(tenBit, raw)], [0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255]);
});

test("both exports reject invalid raw configurations and body lengths", () => {
    const good = config("GREY", 4);
    const cases = [
        [null, new Uint8Array(4)],
        [{ ...good, fourcc: "Y10" }, new Uint8Array(4)],
        [{ ...good, width: 0 }, new Uint8Array(4)],
        [{ ...good, width: 1.5 }, new Uint8Array(4)],
        [{ ...good, height: -1 }, new Uint8Array(4)],
        [{ ...good, height: NaN }, new Uint8Array(4)],
        [{ ...good, stride: 3 }, new Uint8Array(4)],
        [{ ...good, stride: undefined }, new Uint8Array(4)],
        [{ ...good, sizeimage: 3 }, new Uint8Array(3)],
        [{ ...good, sizeimage: "4" }, new Uint8Array(4)],
        [{ ...good, width: Number.MAX_SAFE_INTEGER, stride: Number.MAX_SAFE_INTEGER,
            height: 2, sizeimage: Number.MAX_SAFE_INTEGER }, new Uint8Array(4)],
        [{ ...good, width: 0x40000000, stride: 0x40000000, sizeimage: 0x40000000 }, new Uint8Array(4)],
        [{ fourcc: "Y10P", width: 3, height: 1, stride: 5, sizeimage: 5 }, new Uint8Array(5)],
        [{ fourcc: "Y10P", width: 4, height: 1, stride: 4, sizeimage: 4 }, new Uint8Array(4)],
        [{ fourcc: "Y12P", width: 3, height: 1, stride: 6, sizeimage: 6 }, new Uint8Array(6)],
        [{ fourcc: "Y12P", width: 4, height: 1, stride: 5, sizeimage: 5 }, new Uint8Array(5)],
        [good, new Uint8Array(3)],
        [good, new Uint8Array(5)],
        [good, new Uint16Array(2)],
        [good, [0, 1, 2, 3]],
        [good, null]
    ];
    for (const [layout, bytes] of cases) {
        assert.throws(() => decodeRawToRgba(layout, bytes));
        assert.throws(() => rawPixelAt(layout, bytes, 0, 0));
    }
});

test("native pixel lookup rejects fractional, nonnumeric, and out-of-bounds coordinates", () => {
    const layout = config("GREY", 4, 2);
    const bytes = new Uint8Array(layout.sizeimage);
    for (const [x, y] of [[-1, 0], [4, 0], [0, -1], [0, 2], [0.5, 0], [0, 0.5], [NaN, 0], [0, Infinity], ["0", 0]]) {
        assert.throws(() => rawPixelAt(layout, bytes, x, y), RangeError);
    }
});
