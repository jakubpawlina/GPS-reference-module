#!/usr/bin/env python3
"""Generate the ignored Wokwi Arduino simulation project from the tracked firmware source."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
FIRMWARE = ROOT / "firmware" / "gps_reference_module" / "gps_reference_module.ino"
ASSETS_DIR = ROOT / "simulation" / "assets"
DEFAULT_OUTPUT_DIR = ROOT / "simulation" / "wokwi"

SHARED_ASSETS = (
    "diagram.json",
    "libraries.txt",
    "neo-m8n.chip.c",
    "neo-m8n.chip.json",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a Wokwi Arduino simulation project."
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help=f"Directory that will receive the generated project (default: {DEFAULT_OUTPUT_DIR})",
    )
    return parser.parse_args()


def ensure_inputs() -> None:
    if not FIRMWARE.exists():
        raise SystemExit(f"Firmware source not found: {FIRMWARE}")

    missing = [name for name in SHARED_ASSETS if not (ASSETS_DIR / name).exists()]
    if missing:
        raise SystemExit(f"Missing simulation assets: {', '.join(missing)}")

def reset_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def copy_file(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def generate_wokwi(output_dir: Path) -> Path:
    target_dir = output_dir.resolve()
    reset_dir(target_dir)

    copy_file(FIRMWARE, target_dir / "sketch.ino")

    for name in SHARED_ASSETS:
        copy_file(ASSETS_DIR / name, target_dir / name)

    return target_dir


def main() -> int:
    args = parse_args()
    ensure_inputs()

    generated_path = generate_wokwi(args.output_dir)
    try:
        display_path = generated_path.relative_to(ROOT)
    except ValueError:
        display_path = generated_path

    print(f"generated {display_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
