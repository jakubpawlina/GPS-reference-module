#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "tools" / "generate-wokwi-project.py"
VALIDATOR = ROOT / "tools" / "validate-wokwi-project.py"


class WokwiGeneratorTests(unittest.TestCase):
    def run_generator(
        self, firmware: Path, assets: Path, output: Path
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                "python3",
                str(GENERATOR),
                "--firmware-dir",
                str(firmware),
                "--assets-dir",
                str(assets),
                "--output-dir",
                str(output),
            ],
            capture_output=True,
            text=True,
            check=False,
        )

    def test_tracked_assets_validate(self) -> None:
        """
        Purpose: Validate the committed Wokwi project contract.
        Setup: Run the project validator against the repository's tracked assets.
        Verifies: Wiring, pin mapping, libraries, chip controls, VS Code files, and TOML.
        """
        result = subprocess.run(
            ["python3", str(VALIDATOR)],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("validation passed", result.stdout)

    def test_generator_copies_nested_sources_and_assets(self) -> None:
        """
        Purpose: Ensure generation preserves the complete project source tree.
        Setup: Create nested firmware and asset fixtures with one Arduino entrypoint.
        Verifies: Files are copied recursively and the entrypoint is renamed to sketch.ino.
        """
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            firmware = root / "firmware"
            assets = root / "assets"
            output = root / "output"
            (firmware / "src").mkdir(parents=True)
            (assets / "nested").mkdir(parents=True)
            (firmware / "module.ino").write_text("void setup() {}\nvoid loop() {}\n")
            (firmware / "src" / "module.cpp").write_text("int value = 1;\n")
            (assets / "diagram.json").write_text("{}\n")
            (assets / "nested" / "asset.txt").write_text("asset\n")

            result = self.run_generator(firmware, assets, output)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                (output / "sketch.ino").read_text(), (firmware / "module.ino").read_text()
            )
            self.assertEqual((output / "src" / "module.cpp").read_text(), "int value = 1;\n")
            self.assertEqual((output / "nested" / "asset.txt").read_text(), "asset\n")
            self.assertFalse((output / "module.ino").exists())

    def test_generator_honors_gitignore(self) -> None:
        """
        Purpose: Prevent local build artifacts from leaking into generated projects.
        Setup: Create a temporary Git repository with an ignored firmware build directory.
        Verifies: Ignored files and their directory are absent from generated output.
        """
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            firmware = root / "firmware"
            assets = root / "assets"
            output = root / "output"
            (firmware / "build").mkdir(parents=True)
            assets.mkdir()
            (root / ".gitignore").write_text("firmware/build/\n")
            (firmware / "module.ino").write_text("void setup() {}\nvoid loop() {}\n")
            (firmware / "build" / "artifact.bin").write_bytes(b"ignored")
            (assets / "diagram.json").write_text("{}\n")

            result = self.run_generator(firmware, assets, output)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((output / "build").exists())

    def test_generator_rejects_ambiguous_entrypoints(self) -> None:
        """
        Purpose: Reject firmware projects that Wokwi cannot map to one sketch.
        Setup: Create a firmware fixture containing two top-level .ino files.
        Verifies: Generation fails with an explicit single-entrypoint diagnostic.
        """
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            firmware = root / "firmware"
            assets = root / "assets"
            firmware.mkdir()
            assets.mkdir()
            (firmware / "first.ino").write_text("void setup() {}\n")
            (firmware / "second.ino").write_text("void loop() {}\n")

            result = self.run_generator(firmware, assets, root / "output")

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Expected exactly one .ino file", result.stderr)

    def test_generator_rejects_overlapping_output(self) -> None:
        """
        Purpose: Prevent destructive generation into or around source directories.
        Setup: Place the requested output directory inside the firmware fixture.
        Verifies: Generation fails before resetting or copying any overlapping path.
        """
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            firmware = root / "firmware"
            assets = root / "assets"
            firmware.mkdir()
            assets.mkdir()
            (firmware / "module.ino").write_text("void setup() {}\nvoid loop() {}\n")

            result = self.run_generator(firmware, assets, firmware / "generated")

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("cannot overlap", result.stderr)


class ListedTestResult(unittest.TextTestResult):
    DESCRIPTIONS = {
        "test_generator_copies_nested_sources_and_assets": "Copies the complete firmware tree and nested simulation assets.",
        "test_generator_honors_gitignore": "Excludes ignored build output from the generated Wokwi project.",
        "test_generator_rejects_ambiguous_entrypoints": "Fails when the firmware contains more than one Arduino entrypoint.",
        "test_generator_rejects_overlapping_output": "Prevents generation from deleting or nesting inside source directories.",
        "test_tracked_assets_validate": "Checks diagram wiring, pin mapping, libraries, VS Code files, and TOML.",
    }

    def addSuccess(self, test: unittest.TestCase) -> None:
        super().addSuccess(test)
        description = test._testMethodName.removeprefix("test_").replace("_", " ").capitalize()
        check = self.DESCRIPTIONS.get(test._testMethodName, "")
        print(f"PASS\t{description}\t{check}")


if __name__ == "__main__":
    runner = unittest.TextTestRunner(
        stream=sys.stderr,
        verbosity=0,
        resultclass=ListedTestResult,  # type: ignore[arg-type]  # typeshed variance
    )
    unittest.main(testRunner=runner)
