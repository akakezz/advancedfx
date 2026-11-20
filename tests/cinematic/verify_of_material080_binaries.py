#!/usr/bin/env python3
"""Verify exact Open Fortress 2021 Source SDK MP DLLs without loading/executing them.

Usage:
  python tests/cinematic/verify_of_material080_binaries.py MaterialSystem.dll tier0.dll vstdlib.dll

This checks the specific binary build used to research OF category materials.
Passing does NOT establish shader compatibility, ownership safety, or game stability.
"""
import argparse
import hashlib
import struct
from pathlib import Path


class PE:
    def __init__(self, path):
        self.path = path
        self.data = Path(path).read_bytes()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        if self.data[:2] != b"MZ":
            raise ValueError(f"{path}: missing MZ signature")
        pe = self.u32(0x3c)
        if self.data[pe:pe + 4] != b"PE\x00\x00":
            raise ValueError(f"{path}: invalid PE signature")
        if self.u16(pe + 4) != 0x14c or self.u16(pe + 24) != 0x10b:
            raise ValueError(f"{path}: not PE32/i386")
        self.stamp = self.u32(pe + 8)
        self.base = self.u32(pe + 24 + 28)
        self.image_size = self.u32(pe + 24 + 56)
        section = pe + 24 + self.u16(pe + 20)
        self.sections = []
        for i in range(self.u16(pe + 6)):
            at = section + i * 40
            va, raw, offset = (
                self.u32(at + 12), self.u32(at + 16), self.u32(at + 20)
            )
            self.sections.append((va, raw, offset))
        self.exports_rva = self.u32(pe + 24 + 96)

    def u16(self, at):
        return struct.unpack_from("<H", self.data, at)[0]

    def u32(self, at):
        return struct.unpack_from("<I", self.data, at)[0]

    def offset(self, rva):
        for va, size, at in self.sections:
            if va <= rva < va + size:
                return at + rva - va
        if rva < 0x400:
            return rva
        raise ValueError(f"{self.path}: unmapped file RVA 0x{rva:x}")

    def bytes(self, rva, count):
        at = self.offset(rva)
        return self.data[at:at + count]

    def ptr(self, rva):
        return self.u32(self.offset(rva))

    def check(self, sha, stamp, image_size):
        assert self.sha256 == sha, f"{self.path}: SHA256 mismatch"
        assert (self.stamp, self.image_size) == (stamp, image_size), (
            f"{self.path}: PE timestamp/image size mismatch"
        )

    def slot(self, table_rva, index, method_rva):
        assert self.ptr(table_rva + 4 * index) == self.base + method_rva, (
            f"{self.path}: vtable slot {index} differs"
        )

    def export(self, name):
        at = self.offset(self.exports_rva)
        count = self.u32(at + 24)
        addresses = self.u32(at + 28)
        names = self.u32(at + 32)
        ordinals = self.u32(at + 36)
        for i in range(count):
            key = self.bytes(self.ptr(names + 4 * i), 200).split(b"\0")[0]
            if key.decode("ascii") == name:
                ordinal = self.u16(self.offset(ordinals + 2 * i))
                return self.ptr(addresses + 4 * ordinal)
        raise AssertionError(f"{self.path}: export {name} not found")


def verify(material, tier0, vstdlib):
    material.check("ecf494be08c8f380959ecbaa615a2a9aa3d90e3ad2321d0f917af4d6e1d0b95f",
                   0x60d42f70, 0x157000)
    tier0.check("0728278672172b4e2cb0ef43941b10f902d48e6c03eb28a8c45a22700e463949",
                0x60d3e2a2, 0x50000)
    vstdlib.check("05d9ac2ed935c9d7ad054f55a3092e9b3a5950a88f422172b718f7f5f00db541",
                  0x60d42f5b, 0x9b000)
    assert b"VMaterialSystem080\0" in material.data
    assert tier0.export("g_pMemAlloc") > 0
    assert vstdlib.export("KeyValuesSystem") == 0x9d60
    assert vstdlib.bytes(0x9d60, 6) == b"\xb8\x98\x2b\x09\x10\xc3"
    for slot, target in enumerate([0x8bc0, 0x7a40, 0x7d60, 0x7d90]):
        vstdlib.slot(0x1f0ac, slot, target)
    material.slot(0xb7b54, 70, 0x45b00)
    assert material.bytes(0x45b00, 11) == (
        b"\x55\x8b\xec\x56\x57\xff\x75\x0c\x8b\xf9\x68"
    )
    for slot, target in [(12, 0x3a2e0), (13, 0x391e0), (42, 0x3aa40)]:
        material.slot(0xb67f0, slot, target)
    print("PASS: matching PE32 material system, tier0 and vstdlib builds")
    print("PASS: material creation and reference/error virtual methods")
    print("PASS: vstdlib KeyValuesSystem export/singleton and required methods")
    print("NOTE: Shader results and Windows runtime stability remain unverified")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("materialsystem")
    parser.add_argument("tier0")
    parser.add_argument("vstdlib")
    args = parser.parse_args()
    verify(PE(args.materialsystem), PE(args.tier0), PE(args.vstdlib))
