"""Static PE inventory against the actual guest USER32 DLL; never executes input."""
from pathlib import Path
import argparse
import hashlib
import json
import struct


class Pe:
    def __init__(self, path, data=None):
        if data is None and path.stat().st_size > 32 * 1024 * 1024:
            raise ValueError("PE inventory input exceeds 32 MiB")
        if data is None:
            with path.open('rb') as stream:
                data = stream.read(32 * 1024 * 1024 + 1)
        if len(data) > 32 * 1024 * 1024:
            raise ValueError("PE inventory input exceeds 32 MiB")
        self.data = data
        self.sections = []
        if self.data[:2] != b"MZ":
            raise ValueError("MZ signature missing")
        nt = self.u32(60)
        if self.slice(nt, 4) != b"PE\0\0":
            raise ValueError("PE signature missing")
        self.machine = self.u16(nt+4)
        count, optional = self.u16(nt+6), self.u16(nt+20)
        if not 1 <= count <= 96 or optional < 112 or self.u16(nt+24) != 0x20B:
            raise ValueError("Only bounded PE32+ inventories are supported")
        self.slice(nt+24, optional)
        self.image = self.u32(nt+80)
        self.headers = self.u32(nt+84)
        directories = self.u32(nt+132)
        if directories > 16 or 112+directories*8 > optional:
            raise ValueError("PE directory table outside header")
        self.directories = [(self.u32(nt+136+i*8), self.u32(nt+140+i*8)) for i in range(directories)]
        for i in range(count):
            at = nt+24+optional+i*40
            self.slice(at, 40)
            virtual, rva, size, raw = struct.unpack_from("<IIII", self.data, at+8)
            self.slice(raw, size)
            self.sections.append((rva, size, raw, virtual))

    def slice(self, at, size):
        if at < 0 or size < 0 or at > len(self.data) or size > len(self.data)-at:
            raise ValueError("PE range outside file")
        return self.data[at:at+size]

    def u16(self, at):
        return struct.unpack("<H", self.slice(at, 2))[0]

    def u32(self, at):
        return struct.unpack("<I", self.slice(at, 4))[0]

    def at(self, rva, size=1):
        if rva < self.headers and size <= self.headers-rva:
            self.slice(rva, size)
            return rva
        matches = [raw+rva-va for va, count, raw, _ in self.sections if va <= rva and size <= count and rva-va <= count-size]
        if len(matches) != 1:
            raise ValueError("PE RVA has no unique file backing")
        self.slice(matches[0], size)
        return matches[0]

    def string(self, rva):
        out = bytearray()
        for i in range(256):
            value = self.data[self.at(rva+i)]
            if not value:
                return out.decode("ascii")
            if value < 32 or value > 126:
                raise ValueError("PE name is not bounded ASCII")
            out.append(value)
        raise ValueError("PE name exceeds inventory limit")

    def directory(self, index):
        return self.directories[index] if index < len(self.directories) else (0, 0)

    def imports(self):
        rva, size = self.directory(1)
        if not rva and not size:
            return {}
        if size < 20 or size > 65536:
            raise ValueError("PE import directory size rejected")
        result = {}
        for i in range(size//20):
            descriptor = self.slice(self.at(rva+i*20, 20), 20)
            ilt, stamp, forward, name, iat = struct.unpack("<IIIII", descriptor)
            if not any((ilt, stamp, forward, name, iat)):
                return result
            if not name or not iat:
                raise ValueError("PE import descriptor is incomplete")
            dll = self.string(name).lower()
            symbols = result.setdefault(dll, [])
            for j in range(4096):
                value = struct.unpack("<Q", self.slice(self.at((ilt or iat)+j*8, 8), 8))[0]
                if not value:
                    break
                symbols.append("#"+str(value & 65535) if value >> 63 else self.string(value+2))
            else:
                raise ValueError("PE import count rejected")
        raise ValueError("PE import descriptor terminator missing")

    def exports(self):
        rva, size = self.directory(0)
        if not rva or size < 40:
            raise ValueError("Runtime export directory missing")
        at = self.at(rva, 40)
        count, table = self.u32(at+24), self.u32(at+32)
        if count > 256:
            raise ValueError("Runtime export count rejected")
        return {self.string(self.u32(self.at(table+i*4, 4))) for i in range(count)}


def inspect(application, user32):
    app, dll = Pe(application), Pe(user32)
    exports = dll.exports()
    imports = app.imports()
    missing = {name: [symbol for symbol in symbols if name != "user32.dll" or symbol not in exports]
               for name, symbols in imports.items()}
    missing = {name: symbols for name, symbols in missing.items() if symbols}
    reasons = []
    if len(app.data) > 0x20000:
        reasons.append("file exceeds 128 KiB guest source limit")
    if app.image > 0x20000:
        reasons.append("image exceeds 128 KiB guest loader limit")
    if app.machine != 0x8664:
        reasons.append("guest contract supports x64 only")
    if missing:
        reasons.append("required imports are not implemented by this guest runtime")
    return {"schema": 1, "executed": False, "applicationSha256": hashlib.sha256(app.data).hexdigest(),
            "runtimeSha256": hashlib.sha256(dll.data).hexdigest(), "fileBytes": len(app.data),
            "imageBytes": app.image, "imports": imports, "importCount": sum(map(len, imports.values())),
            "runtimeExports": sorted(exports), "missingImports": missing,
            "candidateForGuestLoader": not reasons, "functionalCompatibilityProven": False,
            "reasons": reasons, "scope": "static prerequisite inventory; guest loader and execution remain authoritative"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("application", type=Path)
    parser.add_argument("user32", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.application, args.user32)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2, ensure_ascii=False)
        stream.write("\n")
    print(json.dumps({k: result[k] for k in ("executed", "importCount", "candidateForGuestLoader", "reasons")}))


if __name__ == "__main__":
    main()
