"""Phase 14.1: bounded static dependency inventory and immutable owned-runtime manifests.

Never loads or executes an input DLL/EXE. A manifest selects a Guardian target
contract; it does not assert that Windows DLLs or their native ABI are supported.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import sys
import inspect_gui_compatibility as gui

ROOT = Path(__file__).resolve().parents[1]
PROFILES = {"win10": (1, 10, 0, 19045), "win7": (2, 6, 1, 7601), "win11": (3, 10, 0, 22631)}
MAGIC = 0x3143544E564147
MAX_MODULES, MAX_TOTAL = 128, 128 << 20
NAME = re.compile(r"[a-zA-Z0-9_+.-]{1,128}\.(dll|exe|gpe|gk)", re.I)
FAMILIES = {
    "ntdll": "14.3", "kernel32": "14.4", "kernelbase": "14.4",
    "msvcrt": "14.5", "ucrtbase": "14.5", "vcruntime140": "14.5", "msvcp140": "14.5",
    "advapi32": "14.6", "sechost": "14.6", "userenv": "14.6",
    "user32": "14.7", "gdi32": "14.7", "gdi32full": "14.7", "win32u": "14.7", "dwmapi": "14.7",
    "ole32": "14.8", "oleaut32": "14.8", "combase": "14.8", "rpcrt4": "14.8",
    "shell32": "14.8", "shlwapi": "14.8", "comctl32": "14.8", "comdlg32": "14.8", "uxtheme": "14.8",
    "ws2_32": "14.9", "mswsock": "14.9", "dnsapi": "14.9", "iphlpapi": "14.9",
    "winhttp": "14.9", "wininet": "14.9", "bcrypt": "14.9", "ncrypt": "14.9", "crypt32": "14.9", "secur32": "14.9",
    "dxgi": "14.10", "d3d9": "14.10", "d3d11": "14.10", "d2d1": "14.10", "dwrite": "14.10",
    "windowscodecs": "14.10", "winmm": "14.10", "mmdevapi": "14.10",
    "mscoree": "14.11", "clr": "14.11", "coreclr": "14.11", "msi": "14.11", "setupapi": "14.11", "version": "14.11"
}


def require(ok, why):
    if not ok:
        raise ValueError(why)


def canonical(value):
    return (json.dumps(value, sort_keys=True, indent=2, ensure_ascii=True) + "\n").encode("ascii")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def regular(path, limit):
    path = Path(path)
    require(path.is_file() and not path.is_symlink() and not getattr(path.lstat(), "st_file_attributes", 0) & 0x400,
            "Only ordinary input files are accepted")
    require(path.stat().st_size <= limit, "Input exceeds its size budget")
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    require(len(data) <= limit, "Input grew beyond its budget")
    return data


def name(value):
    require(isinstance(value, str) and NAME.fullmatch(value), "Module name must be a bounded basename")
    return value.lower()


def phase(module):
    if module.startswith(("api-ms-win-crt-", "vcruntime", "msvcp")):
        return "14.5"
    if module.startswith(("api-ms-win-core-", "ext-ms-")):
        return "14.4"
    return FAMILIES.get(module[:-4] if module.endswith(".dll") else module, "unassigned")


class InventoryPe(gui.Pe):
    def __init__(self, path):
        # Reuse the existing bounded parser; take a private byte snapshot so later
        # dependency inspection and hashing refer to the same bytes.
        super().__init__(path, regular(path, 32 << 20))
        nt = self.u32(60)
        self.base = struct.unpack("<Q", self.slice(nt + 48, 8))[0]
        optional, count = self.u16(nt + 20), self.u16(nt + 6)
        self.flags = [self.u32(nt + 24 + optional + i * 40 + 36) for i in range(count)]
        require(self.machine == 0x8664 and self.headers <= len(self.data), "Only x64 PE32+ inventory is accepted in 14.1")
        for i, (va, count, _, virtual) in enumerate(self.sections):
            require(va >= self.headers and va + max(count, virtual) <= self.image, "Section exceeds image or overlaps headers")
            for other in self.sections[:i]:
                require(va + max(count, virtual) <= other[0] or other[0] + max(other[1], other[3]) <= va,
                        "Overlapping virtual sections rejected")

    def import_records(self):
        records = [{"module": name(dll), "symbol": symbol, "kind": "static", "importType": "not-encoded-in-PE"}
                   for dll, symbols in self.imports().items() for symbol in symbols]
        rva, size = self.directory(13)
        if not rva and not size:
            return records
        require(32 <= size <= 65536, "Delay import directory size rejected")
        for i in range(size // 32):
            values = struct.unpack("<8I", self.slice(self.at(rva + i * 32, 32), 32))
            if not any(values):
                return records
            attrs, module, _, iat, ilt, _, _, _ = values
            require(attrs == 1 and module and iat and ilt, "Only bounded RVA delay imports are accepted")
            dll = name(self.string(module))
            for j in range(4096):
                value = struct.unpack("<Q", self.slice(self.at(ilt + j * 8, 8), 8))[0]
                if not value:
                    break
                records.append({"module": dll, "symbol": "#" + str(value & 65535) if value >> 63 else self.string(value + 2),
                                "kind": "delay", "importType": "not-encoded-in-PE"})
            else:
                raise ValueError("Delay import count rejected")
        raise ValueError("Delay import terminator missing")

    def export_records(self):
        rva, size = self.directory(0)
        if not rva and not size:
            return []
        require(size >= 40, "Export header truncated")
        at = self.at(rva, 40)
        base, functions, names, eat, ent, ordinals = [self.u32(at + i) for i in (16, 20, 24, 28, 32, 36)]
        require(names <= functions <= 8192 and base + functions <= 65536, "Export count/ordinal rejected")
        by_index = {}
        for i in range(names):
            index = self.u16(self.at(ordinals + i * 2, 2))
            require(index < functions, "Export name ordinal outside function table")
            by_index.setdefault(index, []).append(self.string(self.u32(self.at(ent + i * 4, 4))))
        result = []
        for i in range(functions):
            address = self.u32(self.at(eat + i * 4, 4))
            if not address:
                continue
            forwarded = self.string(address) if rva <= address < rva + size else None
            executable = any(va <= address < va + max(raw_size, virtual) and flags & 0x20000000
                             for (va, raw_size, _, virtual), flags in zip(self.sections, self.flags))
            require(forwarded or address < self.image, "Export address outside image")
            if forwarded:
                require("." in forwarded, "Malformed forwarder")
            result.append({"ordinal": base + i, "names": sorted(by_index.get(i, [])), "forwarder": forwarded,
                           "type": "forwarder" if forwarded else "code" if executable else "data-candidate"})
        return result


def inventory(paths, profile="win10", observations=()):
    require(profile in PROFILES, "Unsupported NT profile or architecture")
    require(0 < len(paths) <= MAX_MODULES, "Module count rejected")
    require(isinstance(observations, (list, tuple)) and len(observations) <= 4096, "Dynamic observation budget rejected")
    modules, total, symbols_total = {}, 0, 0
    for path in paths:
        module = name(Path(path).name)
        require(module not in modules, "Duplicate module basename")
        pe = InventoryPe(Path(path))
        total += len(pe.data)
        require(total <= MAX_TOTAL, "Aggregate PE budget exceeded")
        imports, exports_ = pe.import_records(), pe.export_records()
        symbols_total += len(imports) + len(exports_)
        require(symbols_total <= 16384, "Aggregate import/export budget exceeded")
        modules[module] = {"name": module, "sha256": sha(pe.data), "fileBytes": len(pe.data), "imageBytes": pe.image,
                           "imports": imports, "exports": exports_, "dynamicImports": [],
                           "dynamicImportCoverage": "unobserved", "fileVersion": None, "referenceRevision": None}
    for observation in observations:
        require(set(observation) == {"source", "module", "symbol", "evidence"}, "Dynamic observation fields rejected")
        source = name(observation["source"])
        require(source in modules and isinstance(observation["evidence"], str) and
                re.fullmatch(r"[A-Za-z0-9_.:-]{1,128}", observation["evidence"]), "Dynamic observation source/evidence rejected")
        symbol = observation["symbol"]
        require(isinstance(symbol, str) and 0 < len(symbol) <= 256 and all(32 <= ord(c) <= 126 for c in symbol), "Dynamic symbol rejected")
        modules[source]["dynamicImports"].append({"module": name(observation["module"]), "symbol": symbol,
                                                  "kind": "dynamic-observed", "evidence": observation["evidence"]})
        modules[source]["dynamicImportCoverage"] = "observations-only; completeness-unproven"
    exports = {}
    for module, info in modules.items():
        exports[module] = {}
        for export in info["exports"]:
            for symbol in ["#" + str(export["ordinal"])] + export["names"]:
                require(symbol not in exports[module], "Duplicate export symbol")
                exports[module][symbol] = export

    def resolve(module, symbol, seen=()):
        key = (module, symbol)
        if key in seen or len(seen) >= 32:
            return "forwarder-cycle-or-limit", []
        if module.startswith(("api-ms-", "ext-ms-")):
            return "api-set-schema-pending", []
        entry = exports.get(module, {}).get(symbol)
        if entry is None:
            return "module-missing" if module not in modules else "symbol-missing", []
        if entry["forwarder"]:
            dll, forwarded = entry["forwarder"].rsplit(".", 1)
            target = name(dll if dll.lower().endswith(".dll") else dll + ".dll")
            state, chain = resolve(target, forwarded, seen + (key,))
            return state, [{"module": target, "symbol": forwarded}] + chain
        return "export-present; behavior-unvalidated", []

    matrix, edges = [], {m: set() for m in modules}
    for module, info in modules.items():
        for exported in info["exports"]:
            if exported["forwarder"]:
                dll = exported["forwarder"].rsplit(".", 1)[0]
                target = name(dll if dll.lower().endswith(".dll") else dll + ".dll")
                if target in modules:
                    edges[module].add(target)
    for module, info in modules.items():
        for imported in info["imports"] + info["dynamicImports"]:
            require(len(matrix) < 16384, "Aggregate symbol budget exceeded")
            state, chain = resolve(imported["module"], imported["symbol"])
            edges[module].update(target for target in [imported["module"]] + [item["module"] for item in chain] if target in modules)
            matrix.append({"source": module, **imported, "resolution": state, "forwarders": chain,
                           "behavior": "unvalidated", "phase": phase(imported["module"])})
    order, cycles, active, visited = [], [], set(), set()

    def visit(module):
        if module in active:
            cycles.append(module)
            return
        if module in visited:
            return
        active.add(module)
        for dependency in sorted(edges[module]):
            visit(dependency)
        active.remove(module)
        visited.add(module)
        order.append(module)

    for module in sorted(modules):
        visit(module)
    priorities = sorted({(row["phase"], row["module"], row["symbol"]) for row in matrix},
                        key=lambda row: (tuple(map(int, row[0].split("."))) if row[0] != "unassigned" else (99,), row[1], row[2]))
    return {"schema": 1, "profile": profile, "executed": False, "functionalCompatibilityProven": False,
            "reference": "pending-exact-build-native-observations", "modules": list(modules.values()),
            "matrix": matrix, "dependencyOrder": order, "dependencyCycles": sorted(set(cycles)),
            "symbolPriority": [{"phase": p, "module": m, "symbol": s} for p, m, s in priorities],
            "importCount": len(matrix), "unresolvedCount": sum(not r["resolution"].startswith("export-present") for r in matrix),
            "limits": {"modules": MAX_MODULES, "fileBytes": 32 << 20, "totalBytes": MAX_TOTAL},
            "limitations": ["static imports do not encode function-versus-data requirements", "data exports classified from section permissions",
                            "GetProcAddress names cannot all be inferred statically", "API set mapping and Windows binary execution pending 14.2+",
                            "file version/revision extraction and original Windows runtime execution pending"]}


def create(profile, kernel, application, runtime, output, modules=(), observations=()):
    require(profile in PROFILES, "Unsupported NT profile or architecture")
    runtime = Path(runtime)
    require(runtime.name.lower() == "user32.dll", "14.1 executable binding supports only the owned GUI runtime")
    data = inventory([application, runtime, *modules], profile, observations)
    runtime_pe = InventoryPe(runtime)
    require({"CreateWindowExA", "GetMessageA"} <= {s for e in runtime_pe.export_records() for s in e["names"]}, "Owned GUI runtime contract missing")
    # The kernel hash is the execution authority for its embedded runtime. This
    # package is diagnostic, never an instruction to load a supplied DLL on host.
    kernel_data = regular(kernel, 32 << 20)
    require(b"GuardianExternalGui/1/bounded" in kernel_data, "External GUI kernel contract missing")
    app_info = next(m for m in data["modules"] if m["name"] == name(Path(application).name))
    runtime_info = next(m for m in data["modules"] if m["name"] == name(runtime.name))
    require(runtime_pe.data in kernel_data, "Selected runtime bytes are not embedded in this kernel")
    identity, major, minor, build = PROFILES[profile]
    manifest = {"schema": 1, "contract": "guardian-nt-contract/1",
                "profile": {"id": identity, "name": profile, "architecture": "x64", "major": major, "minor": minor, "build": build, "revision": None},
                "runtime": {"kind": "guardian-owned-subset", "sha256": runtime_info["sha256"], "originalWindowsDllsLoaded": False},
                "kernelSha256": sha(kernel_data), "applicationSha256": app_info["sha256"], "modules": [
                    {k: m[k] for k in ("name", "sha256", "fileVersion", "referenceRevision")} for m in data["modules"]],
                "inventorySha256": sha(canonical(data)), "corpusPolicySha256": sha(regular(ROOT / "config/nt-compatibility/corpus-policy.json", 65536)),
                "reference": {"state": "pending", "nativeAbiConformance": False},
                "behaviorPolicies": {k: f"{profile}/{k}/pending-native-conformance" for k in (
                    "api-availability", "api-sets", "syscalls", "layouts", "information-classes", "errors", "time", "synchronization", "memory", "files", "graphics")},
                "implementedContract": ["immutable-profile-binding", "raw-target-version-query", "manifest-sensitive-version-policy-model"],
                "windowsBinaryCompatibility": False, "phase": "14.1", "nextPhaseStarted": False}
    encoded = canonical(manifest)
    binding = struct.pack("<QQ8I", MAGIC, 0, 1, 176, identity, major, minor, build, 0, 1) + b"".join(
        bytes.fromhex(value) for value in (sha(encoded), manifest["kernelSha256"], manifest["applicationSha256"], runtime_info["sha256"]))
    require(len(canonical(data)) <= 4 << 20, "Inventory output budget exceeded")
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    for filename, content in (("manifest.json", encoded), ("inventory.json", canonical(data)), ("binding.gnt", binding)):
        with (output / filename).open("xb") as stream:
            stream.write(content)
    return manifest


def pairs_unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "Duplicate JSON key")
        result[key] = value
    return result


def verify(bundle):
    bundle = Path(bundle)
    raw = regular(bundle / "manifest.json", 4 << 20)
    manifest = json.loads(raw, object_pairs_hook=pairs_unique)
    require(raw == canonical(manifest), "Manifest is not canonical")
    require(manifest["schema"] == 1 and manifest["contract"] == "guardian-nt-contract/1" and
            manifest["phase"] == "14.1" and manifest["nextPhaseStarted"] is False and manifest["windowsBinaryCompatibility"] is False, "Manifest contract rejected")
    profile = manifest["profile"]
    require(profile["name"] in PROFILES and profile["architecture"] == "x64" and profile["revision"] is None, "Profile/revision rejected")
    require(tuple(profile[k] for k in ("id", "major", "minor", "build")) == PROFILES[profile["name"]], "Incoherent NT profile")
    require(manifest["runtime"]["kind"] == "guardian-owned-subset" and manifest["runtime"]["originalWindowsDllsLoaded"] is False,
            "Unsupported runtime kind")
    hashes = [sha(raw), manifest["kernelSha256"], manifest["applicationSha256"], manifest["runtime"]["sha256"]]
    require(all(isinstance(h, str) and re.fullmatch(r"[a-f0-9]{64}", h) and int(h, 16) for h in hashes), "SHA-256 field rejected")
    expected = struct.pack("<QQ8I", MAGIC, 0, 1, 176, *PROFILES[profile["name"]], 0, 1) + b"".join(bytes.fromhex(h) for h in hashes)
    require(regular(bundle / "binding.gnt", 176) == expected, "Manifest/binding mismatch")
    require(sha(regular(bundle / "inventory.json", 4 << 20)) == manifest["inventorySha256"], "Inventory digest mismatch")
    require(sha(regular(ROOT / "config/nt-compatibility/corpus-policy.json", 65536)) == manifest["corpusPolicySha256"], "Corpus policy changed")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("inventory", "create"):
        p = sub.add_parser(command)
        p.add_argument("--profile", choices=PROFILES, default="win10")
        p.add_argument("--application", type=Path, required=True)
        p.add_argument("--runtime", type=Path, required=True)
        p.add_argument("--module", type=Path, action="append", default=[])
        p.add_argument("--observations", type=Path)
        p.add_argument("--output", type=Path, required=True)
        if command == "create":
            p.add_argument("--kernel", type=Path, required=True)
    sub.add_parser("verify").add_argument("bundle", type=Path)
    args = parser.parse_args()
    try:
        observations = json.loads(regular(args.observations, 1 << 20), object_pairs_hook=pairs_unique) if getattr(args, "observations", None) else []
        if args.command == "verify":
            result = verify(args.bundle)
        elif args.command == "create":
            result = create(args.profile, args.kernel, args.application, args.runtime, args.output, args.module, observations)
        else:
            result = inventory([args.application, args.runtime, *args.module], args.profile, observations)
            with args.output.open("xb") as stream:
                stream.write(canonical(result))
        print(json.dumps({k: result[k] for k in ("schema", "profile", "importCount", "unresolvedCount", "windowsBinaryCompatibility") if k in result}))
        return 0
    except (ValueError, KeyError, TypeError, OSError, struct.error) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
