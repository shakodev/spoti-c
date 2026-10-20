"""Deterministic evidence-tool checks; no account, desktop or GPU access."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / (name + ".py"))
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


compare, reference = module("parity_compare"), module("parity_reference")


class ParityTools(unittest.TestCase):
    def run_comparison(self, root, native, reference, output):
        metadata = root / "capture.json"
        metadata.write_text(json.dumps({"width": 1, "height": 1, "dpi": 96}))
        return subprocess.run([
            sys.executable, str(ROOT / "tools/parity_compare.py"), str(native),
            "--metadata", str(metadata), "--reference", str(reference),
            "--reference-metadata", str(metadata), "--output", str(output)],
            capture_output=True, text=True)

    def test_rejects_output_that_overwrites_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            native = root / "capture.rgba"
            native.write_bytes(bytes([255, 0, 0, 255]))
            # Both output filenames may already be source captures.
            for name in ("native.png", "reference.png", "difference.png", "comparison.png"):
                with self.subTest(name=name):
                    reference = root / name
                    compare.write_png(reference, 1, 1, bytes([0, 255, 0, 255]))
                    original = reference.read_bytes()
                    result = self.run_comparison(root, native, reference, root)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("overwrite an input", result.stderr)
                    self.assertEqual(reference.read_bytes(), original)

    def test_alpha_only_changes_are_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            native, reference = root / "capture.rgba", root / "capture.png"
            native.write_bytes(bytes([255, 0, 0, 0]))
            compare.write_png(reference, 1, 1, bytes([255, 0, 0, 255]))
            result = self.run_comparison(root, native, reference, root / "result")
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads((root / "result/comparison.json").read_text())
            self.assertEqual(report["changed_pixels"], 1)
            self.assertEqual(report["changed_alpha_pixels"], 1)
            self.assertEqual(report["mean_absolute_rgb_error"], 0)
            self.assertEqual(report["mean_absolute_alpha_error"], 255)
            self.assertEqual(compare.read_png(root / "result/difference.png")[2],
                             bytes([255, 255, 255, 255]))

    def test_preserves_native_capture_on_in_place_export(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            native = root / "native.png"
            compare.write_png(native, 1, 1, bytes([255, 0, 0, 255]))
            original = native.read_bytes()
            metadata = root / "capture.json"
            metadata.write_text('{"width":1,"height":1,"dpi":96}')
            result = subprocess.run([
                sys.executable, str(ROOT / "tools/parity_compare.py"), str(native),
                "--metadata", str(metadata), "--output", str(root)],
                capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("overwrite an input", result.stderr)
            self.assertEqual(native.read_bytes(), original)

    def test_preserves_metadata_and_hardlinked_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            native, reference = root / "capture.rgba", root / "capture.png"
            native.write_bytes(bytes([255, 0, 0, 255]))
            compare.write_png(reference, 1, 1, bytes([0, 255, 0, 255]))
            metadata = root / "comparison.json"
            original = '{"width":1,"height":1,"dpi":96}'
            metadata.write_text(original)
            result = subprocess.run([
                sys.executable, str(ROOT / "tools/parity_compare.py"), str(native),
                "--metadata", str(metadata), "--reference", str(reference),
                "--reference-metadata", str(metadata), "--output", str(root)],
                capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("overwrite an input", result.stderr)
            self.assertEqual(metadata.read_text(), original)
            # Distinct paths may still identify the same file on Windows.
            os.link(reference, root / "native.png")
            original = reference.read_bytes()
            result = self.run_comparison(root, native, reference, root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("overwrite an input", result.stderr)
            self.assertEqual(reference.read_bytes(), original)

    def test_lossless_capture_and_comparison(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pixels = bytes([255, 0, 0, 255, 0, 255, 0, 255])
            compare.write_png(root / "reference.png", 2, 1, pixels)
            self.assertEqual(compare.read_png(root / "reference.png"), (2, 1, pixels))
            (root / "native.rgba").write_bytes(bytes([254, 0, 0, 255, 0, 255, 0, 255]))
            metadata = root / "capture.json"
            metadata.write_text(json.dumps({"width": 2, "height": 1, "dpi": 96}))
            subprocess.run([sys.executable, str(ROOT / "tools/parity_compare.py"),
                            str(root / "native.rgba"), "--metadata", str(metadata),
                            "--reference", str(root / "reference.png"),
                            "--reference-metadata", str(metadata), "--output", str(root / "result")],
                           check=True, capture_output=True)
            report = json.loads((root / "result/comparison.json").read_text())
            self.assertEqual(report["changed_pixels"], 1)
            self.assertEqual(report["mean_absolute_rgb_error"], 1 / 6)
            self.assertFalse(report["resampled"])
            self.assertEqual(compare.read_png(root / "result/comparison.png")[:2], (4, 1))

    def test_rejects_wrong_size_and_corrupt_png(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            compare.write_png(root / "image.png", 1, 1, bytes([0, 0, 0, 255]))
            bad = bytearray((root / "image.png").read_bytes())
            bad[20] ^= 1
            (root / "bad.png").write_bytes(bad)
            with self.assertRaisesRegex(ValueError, "CRC"):
                compare.read_png(root / "bad.png")
            (root / "meta.json").write_text('{"width":2,"height":1,"dpi":96}')
            with self.assertRaisesRegex(ValueError, "dimensions"):
                compare.load(root / "image.png", root / "meta.json")

    def test_reference_root_directory_and_traversal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with zipfile.ZipFile(root / "safe.spa", "w") as archive:
                archive.writestr("/", b"")
                archive.writestr("xpui-snapshot.css", ".example{font-size:16px}")
            reference.collect(root / "safe.spa", root / "report.json", root / "extracted")
            report = json.loads((root / "report.json").read_text())
            self.assertEqual(report["skipped_directory_entries"], ["/"])
            self.assertEqual(len(report["files"]), 1)
            self.assertEqual(report["rules"][0]["offset_utf8"], 0)
            with zipfile.ZipFile(root / "unsafe.spa", "w") as archive:
                archive.writestr("../escape.css", b"")
            with self.assertRaisesRegex(ValueError, "Unsafe"):
                reference.collect(root / "unsafe.spa", root / "bad.json", root / "other")
            self.assertFalse((root / "escape.css").exists())


if __name__ == "__main__":
    unittest.main()
