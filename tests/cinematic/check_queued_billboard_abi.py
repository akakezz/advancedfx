#!/usr/bin/env python3
"""Offline verification of exact queued billboard instruction anchors.

No DLL execution, networking or external dependencies. Pass the directory
containing the exact x64 client/materialsystem/shaderapidx9 binaries.
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct

MODULES = {
    "client": ("client.dll", "b6f45c74805edacf0cf61d965f658158fc22960f66d583ef3a6549eb56696b95"),
    "ms": ("materialsystem.dll", "a58d9afd625cb3b3fe9a3588a8d8192325897c803272075e97600b36383cbd7d"),
    "shader": ("shaderapidx9.dll", "42550472d89798cf18c577763a2fd193576da4bdce48b5f725ffec8991a182da"),
}

class PE:
    def __init__(self, data):
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("not a DOS/PE image")
        off, = struct.unpack_from("<I", data, 0x3c)
        if data[off:off+4] != b"PE\0\0":
            raise ValueError("invalid PE signature")
        machine, count = struct.unpack_from("<HH", data, off + 4)
        optional_size, = struct.unpack_from("<H", data, off + 20)
        magic, = struct.unpack_from("<H", data, off + 24)
        if machine != 0x8664 or magic != 0x20b:
            raise ValueError("not an x64 PE32+ image")
        self.sections = []
        for i in range(count):
            _, virtual_size, rva, size, start = struct.unpack_from(
                "<8sIIII", data, off + 24 + optional_size + 40*i)
            if start + size > len(data):
                raise ValueError("section outside file")
            self.sections.append((rva, size, start))

    def read(self, rva, size):
        for va, count, start in self.sections:
            if va <= rva and rva + size <= va + count:
                pos = start + rva - va
                return self.data[pos:pos+size]
        raise ValueError(f"RVA 0x{rva:x} outside file-backed sections")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binaries", type=Path)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2] / "AfxHookSource/MirvCommands.cpp")
    args = parser.parse_args()
    images = {}
    for key, (name, digest) in MODULES.items():
        data = (args.binaries / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit(f"{name}: SHA-256 mismatch; not the audited exact build")
        images[key] = PE(data)
    source = args.source.read_text(encoding="utf-8-sig")
    body = source.split("Mirv_Tf2_RenderQueuedBillboards(", 1)[1].split("Mirv_Tf2_RenderNativeParticles(", 1)[0]
    pattern = r"(?:guardedMatch|SurfaceMatch)\((client|ms|shader),\s*(0x[0-9a-fA-F]+),\s*\{([^}]+)\}\)"
    matches = list(re.finditer(pattern, body))
    if len(matches) < 30:
        raise SystemExit("Too few anchors recognized; update parser for changed source")
    failures = 0
    for match in matches:
        module, address, values = match.groups()
        address = int(address, 16)
        expected = bytes(int(value.strip(), 0) for value in values.split(",") if value.strip())
        actual = images[module].read(address, len(expected))
        if actual != expected:
            failures += 1
            print(f"{MODULES[module][0]}+0x{address:x}: expected {expected.hex(' ')}, found {actual.hex(' ')}")
    print(f"{len(matches)-failures}/{len(matches)} instruction anchors match exact DLLs.")
    return int(bool(failures))

if __name__ == "__main__":
    raise SystemExit(main())
