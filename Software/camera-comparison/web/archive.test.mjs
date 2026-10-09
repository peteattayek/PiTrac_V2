// SPDX-License-Identifier: GPL-3.0-or-later
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const source = await readFile(new URL("./archive.js", import.meta.url), "utf8");
const { buildArchive, capturePng, crc32 } = await import(
    `data:text/javascript;base64,${Buffer.from(source).toString("base64")}`
);
const png = new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10, 1, 2, 3]);

test("ZIP CRC uses the standard polynomial", () => {
    assert.equal(crc32(new TextEncoder().encode("123456789")), 0xcbf43926);
});

test("stored ZIP round-trips bytes and emits a matching central directory", async () => {
    const zip = await buildArchive([{ name: "metadata.json", data: "{}" }, { name: "mira220.png", data: png }]);
    assert.deepEqual(await capturePng(zip, "mira220.png"), png);
    const bytes = new Uint8Array(await zip.arrayBuffer()), view = new DataView(bytes.buffer);
    const end = bytes.length - 22;
    assert.equal(view.getUint32(end, true), 0x06054b50);
    assert.equal(view.getUint16(end + 10, true), 2);
    const directory = view.getUint32(end + 16, true);
    assert.equal(view.getUint32(directory, true), 0x02014b50);
    assert.equal(view.getUint32(end + 12, true) + directory, end);
    assert.equal(view.getUint32(directory + 42, true), 0);
});

test("missing, corrupt, truncated and compressed PNGs produce explicit errors", async () => {
    const zip = await buildArchive([{ name: "mira220.png", data: png }]);
    await assert.rejects(capturePng(zip, "imx296.png"), /does not contain/);
    const original = new Uint8Array(await zip.arrayBuffer());
    for (const fault of ["checksum", "truncated", "compressed", "flags"]) {
        const bytes = original.slice(), view = new DataView(bytes.buffer);
        if (fault === "checksum") bytes[30 + "mira220.png".length + 8] ^= 1;
        if (fault === "compressed") view.setUint16(8, 8, true);
        if (fault === "flags") view.setUint16(6, 8, true);
        const blob = new Blob([fault === "truncated" ? bytes.slice(0, 35) : bytes]);
        await assert.rejects(capturePng(blob, "mira220.png"), /checksum|Truncated|compression|flags/);
    }
    const invalid = await buildArchive([{ name: "mira220.png", data: "not a PNG" }]);
    await assert.rejects(capturePng(invalid, "mira220.png"), /not a PNG/);
});

test("unsafe, duplicate and empty archives are rejected", async () => {
    for (const name of ["../escape", "/absolute", "a//b", "a/./b", "bad name"]) {
        await assert.rejects(buildArchive([{ name, data: "x" }]), /filename/);
    }
    await assert.rejects(buildArchive([]), /count/);
    await assert.rejects(buildArchive([{ name: "a", data: "x" }, { name: "a", data: "y" }]), /duplicate/);
});
