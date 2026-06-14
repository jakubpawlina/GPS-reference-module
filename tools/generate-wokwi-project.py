#!/usr/bin/env python3
"""Generate a Wokwi project from an Arduino sketch and simulation assets."""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_FIRMWARE_DIR = ROOT / "firmware" / "gps_reference_module"
DEFAULT_ASSETS_DIR = ROOT / "simulation" / "assets"
DEFAULT_OUTPUT_DIR = ROOT / "simulation" / "wokwi"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a Wokwi project from an Arduino sketch directory."
    )
    parser.add_argument(
        "--firmware-dir",
        type=Path,
        default=DEFAULT_FIRMWARE_DIR,
        help=f"Arduino sketch directory (default: {DEFAULT_FIRMWARE_DIR})",
    )
    parser.add_argument(
        "--assets-dir",
        type=Path,
        default=DEFAULT_ASSETS_DIR,
        help=f"Wokwi assets directory (default: {DEFAULT_ASSETS_DIR})",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help=f"Directory that will receive the generated project (default: {DEFAULT_OUTPUT_DIR})",
    )
    return parser.parse_args()


def resolve_inputs(args: argparse.Namespace) -> tuple[Path, Path, Path, Path]:
    firmware_dir = args.firmware_dir.resolve()
    assets_dir = args.assets_dir.resolve()
    output_dir = args.output_dir.resolve()

    if not firmware_dir.is_dir():
        raise SystemExit(f"Firmware directory not found: {firmware_dir}")
    if not assets_dir.is_dir():
        raise SystemExit(f"Simulation assets directory not found: {assets_dir}")

    entrypoints = sorted(firmware_dir.glob("*.ino"))
    if len(entrypoints) != 1:
        raise SystemExit(
            f"Expected exactly one .ino file in {firmware_dir}, found {len(entrypoints)}"
        )

    for input_dir in (firmware_dir, assets_dir):
        if output_dir.is_relative_to(input_dir) or input_dir.is_relative_to(output_dir):
            raise SystemExit(
                f"Output and input directories cannot overlap: {output_dir}, {input_dir}"
            )

    return firmware_dir, entrypoints[0], assets_dir, output_dir


def reset_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def git_ignored_paths(paths: list[Path]) -> set[Path]:
    if not paths:
        return set()

    source_dir = paths[0].parent
    repository = subprocess.run(
        ["git", "-C", str(source_dir), "rev-parse", "--show-toplevel"],
        capture_output=True,
        text=True,
        check=False,
    )
    if repository.returncode != 0:
        return set()

    repository_root = Path(repository.stdout.strip()).resolve()
    repository_paths = [
        str(path.relative_to(repository_root))
        for path in paths
        if path.is_relative_to(repository_root)
    ]
    if not repository_paths:
        return set()

    ignored = subprocess.run(
        ["git", "-C", str(repository_root), "check-ignore", "--stdin", "-z"],
        input="\0".join(repository_paths) + "\0",
        capture_output=True,
        text=True,
        check=False,
    )
    if ignored.returncode not in (0, 1):
        raise SystemExit(ignored.stderr.strip() or "Unable to evaluate Git ignore rules")

    return {repository_root / path for path in ignored.stdout.rstrip("\0").split("\0") if path}


def copy_directory(source: Path, target: Path, excluded: set[Path] | None = None) -> None:
    excluded = excluded or set()
    source_files = [path.resolve() for path in source.rglob("*") if path.is_file()]
    ignored = git_ignored_paths(source_files)

    for source_path in source_files:
        if source_path in excluded or source_path in ignored:
            continue

        target_path = target / source_path.relative_to(source)
        target_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_path, target_path)


def generate_wokwi(
    firmware_dir: Path,
    entrypoint: Path,
    assets_dir: Path,
    output_dir: Path,
) -> Path:
    reset_dir(output_dir)

    copy_directory(firmware_dir, output_dir, excluded={entrypoint})
    shutil.copy2(entrypoint, output_dir / "sketch.ino")
    copy_directory(assets_dir, output_dir)

    return output_dir


def main() -> int:
    args = parse_args()
    firmware_dir, entrypoint, assets_dir, output_dir = resolve_inputs(args)

    generated_path = generate_wokwi(
        firmware_dir,
        entrypoint,
        assets_dir,
        output_dir,
    )
    try:
        display_path = generated_path.relative_to(ROOT)
    except ValueError:
        display_path = generated_path

    print(f"generated {display_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
