"""Bounded PE graph, ordinal/data/delay/forwarder handling and manifest refusals."""
from pathlib import Path
import copy
import json
import struct
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import nt_compatibility as nt


def fixture(path, imports=(), exports=(), delay=(), executable=True):
    data = bytearray(0x3200)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x8664, 1)
    struct.pack_into("<H", data, 0x94, 240)
    opt = 0x98
    struct.pack_into("<H", data, opt, 0x20B)
    struct.pack_into("<Q", data, opt + 24, 0x140000000)
    struct.pack_into("<II", data, opt + 56, 0x5000, 0x200)
    struct.pack_into("<I", data, opt + 108, 16)
    section = opt + 240
    struct.pack_into("<IIII", data, section + 8, 0x3000, 0x1000, 0x3000, 0x200)
    struct.pack_into("<I", data, section + 36, 0x60000020 if executable else 0xC0000040)
    cursor = 0x1500

    def at(rva):
        return rva - 0x1000 + 0x200

    def allocate(content, align=1):
        nonlocal cursor
        cursor = (cursor + align - 1) // align * align
        address = cursor
        cursor += len(content)
        assert cursor < 0x2000
        data[at(address):at(address) + len(content)] = content
        return address

    def text(value):
        return allocate(value.encode("ascii") + b"\0")

    if exports:
        names = [(i, e[0]) for i, e in enumerate(exports) if e[0] is not None]
        addresses = [text(e[1]) if len(e) > 1 and e[1] else 0x2800 + i * 8 for i, e in enumerate(exports)]
        eat = allocate(struct.pack("<" + "I" * len(addresses), *addresses), 4)
        ent = allocate(b"".join(struct.pack("<I", text(s)) for _, s in names), 4)
        ordinals = allocate(b"".join(struct.pack("<H", i) for i, _ in names), 2)
        struct.pack_into("<6I", data, at(0x1000) + 16, 1, len(exports), len(names), eat, ent, ordinals)
        struct.pack_into("<II", data, opt + 112, 0x1000, 0x1000)
    for entries, directory, rva, descriptor_bytes in ((imports, 1, 0x1100, 20), (delay, 13, 0x1200, 32)):
        if not entries:
            continue
        for index, (module, symbols) in enumerate(entries):
            values = [(1 << 63) | int(s[1:]) if s.startswith("#") else allocate(b"\0\0" + s.encode("ascii") + b"\0", 2) for s in symbols]
            ilt = allocate(struct.pack("<" + "Q" * (len(values) + 1), *values, 0), 8)
            module_name = text(module)
            if directory == 1:
                struct.pack_into("<5I", data, at(rva + index * descriptor_bytes), ilt, 0, 0, module_name, ilt)
            else:
                struct.pack_into("<8I", data, at(rva + index * descriptor_bytes), 1, module_name, 0, ilt, ilt, 0, 0, 0)
        struct.pack_into("<II", data, opt + 112 + directory * 8, rva, (len(entries) + 1) * descriptor_bytes)
    path.write_bytes(data)
    return path


class InventoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def make_graph(self):
        a = fixture(self.root / "app.exe", [("b.dll", ["Forward", "#2"])], delay=[("c.dll", ["Data"])])
        b = fixture(self.root / "b.dll", exports=[("Forward", "c.Data"), (None, None)])
        c = fixture(self.root / "c.dll", exports=[("Data", None)], executable=False)
        return a, b, c

    def test_dependency_order_ordinals_delay_data_forwarders(self):
        result = nt.inventory(self.make_graph())
        self.assertEqual(result["importCount"], 3)
        self.assertEqual(result["unresolvedCount"], 0)
        self.assertEqual(result["dependencyOrder"], ["c.dll", "b.dll", "app.exe"])
        self.assertEqual(result["matrix"][0]["forwarders"], [{"module": "c.dll", "symbol": "Data"}])
        self.assertEqual(result["modules"][2]["exports"][0]["type"], "data-candidate")
        self.assertEqual(result["matrix"][2]["kind"], "delay")
        self.assertFalse(result["functionalCompatibilityProven"])

    def test_dynamic_observation_is_explicit_and_incomplete(self):
        paths = self.make_graph()
        observed = [{"source": "app.exe", "module": "b.dll", "symbol": "#2", "evidence": "owned-trace:1"}]
        result = nt.inventory(paths, observations=observed)
        self.assertEqual(result["importCount"], 4)
        self.assertIn("completeness-unproven", result["modules"][0]["dynamicImportCoverage"])
        observed[0]["source"] = "unknown.exe"
        with self.assertRaises(ValueError):
            nt.inventory(paths, observations=observed)

    def test_missing_api_sets_not_fabricated(self):
        app = fixture(self.root / "app.exe", [("api-ms-win-core-file-l1-1-0.dll", ["CreateFileW"])])
        result = nt.inventory([app], "win7")
        self.assertEqual(result["matrix"][0]["resolution"], "api-set-schema-pending")
        self.assertEqual(result["symbolPriority"][0]["phase"], "14.4")

    def test_forwarder_cycle_is_reported(self):
        a = fixture(self.root / "app.exe", [("b.dll", ["Loop"])])
        b = fixture(self.root / "b.dll", exports=[("Loop", "c.Loop")])
        c = fixture(self.root / "c.dll", exports=[("Loop", "b.Loop")])
        result = nt.inventory([a, b, c])
        self.assertEqual(result["matrix"][0]["resolution"], "forwarder-cycle-or-limit")
        self.assertEqual(result["unresolvedCount"], 1)

    def test_malformed_files_and_duplicate_names_refused(self):
        app, b, c = self.make_graph()
        for content in (b"", b"MZ", b"MZ" + bytes(58) + b"\xff\xff\xff\x7f"):
            app.write_bytes(content)
            with self.assertRaises(ValueError):
                nt.inventory([app])
        with self.assertRaises(ValueError):
            nt.inventory([b, b])
        with self.assertRaises(ValueError):
            nt.inventory([c], "xp")

    def test_malformed_exports_and_delay_descriptors(self):
        app, b, _ = self.make_graph()
        bytes_ = bytearray(b.read_bytes())
        struct.pack_into("<I", bytes_, 0x200 + 20, 8193)
        b.write_bytes(bytes_)
        with self.assertRaises(ValueError):
            nt.inventory([b])
        bytes_ = bytearray(app.read_bytes())
        struct.pack_into("<I", bytes_, 0x400, 0)
        app.write_bytes(bytes_)
        with self.assertRaises(ValueError):
            nt.inventory([app])

    def test_package_is_canonical_persistent_and_profile_bound(self):
        app = fixture(self.root / "app.exe", [("user32.dll", ["GetMessageA"])])
        dll = fixture(self.root / "user32.dll", exports=[("CreateWindowExA", None), ("GetMessageA", None)])
        kernel = self.root / "kernel.gk"
        kernel.write_bytes(b"GuardianExternalGui/1/bounded" + dll.read_bytes())
        for profile in nt.PROFILES:
            output = self.root / profile
            manifest = nt.create(profile, kernel, app, dll, output)
            self.assertEqual(nt.verify(output), manifest)
            binding = (output / "binding.gnt").read_bytes()
            self.assertEqual(len(binding), 176)
            self.assertEqual(struct.unpack_from("<4I", binding, 24), nt.PROFILES[profile])
            with self.assertRaises(FileExistsError):
                nt.create(profile, kernel, app, dll, output)
        self.assertNotEqual((self.root / "win10/binding.gnt").read_bytes(), (self.root / "win11/binding.gnt").read_bytes())
        output = self.root / "win10"
        original = (output / "manifest.json").read_bytes()
        for mutate in (lambda m: m["profile"].update(build=22631), lambda m: m["profile"].update(architecture="x86"),
                       lambda m: m["runtime"].update(kind="windows-original"), lambda m: m.update(windowsBinaryCompatibility=True)):
            bad = json.loads(original)
            mutate(bad)
            (output / "manifest.json").write_bytes(nt.canonical(bad))
            with self.assertRaises(ValueError):
                nt.verify(output)
        (output / "manifest.json").write_bytes(original)
        (output / "inventory.json").write_bytes(b"{}")
        with self.assertRaises(ValueError):
            nt.verify(output)

    def test_corpus_is_fixed_before_campaign(self):
        policy = json.loads((nt.ROOT / "config/nt-compatibility/corpus-policy.json").read_bytes())
        self.assertEqual(sum(policy["categories"].values()), policy["minimumIndependentPrograms"])
        self.assertFalse(policy["ownedFixturesCountAsIndependentPrograms"])
        self.assertFalse(policy["replacementOfFailedProgramsDuringCampaign"])


if __name__ == "__main__":
    if len(sys.argv) > 1:
        native = json.loads(subprocess.check_output([sys.argv.pop(1), "--profiles"], text=True))
        assert native == {key: list(value) for key, value in nt.PROFILES.items()}
    unittest.main()
