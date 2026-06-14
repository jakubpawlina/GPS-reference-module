#!/usr/bin/env python3
"""Validate tracked Wokwi assets and an optional generated project."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

import tomllib

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ASSETS_DIR = ROOT / "simulation" / "assets"
DEFAULT_FIRMWARE_DIR = ROOT / "firmware" / "gps_reference_module"
DEFAULT_PROJECT_DIR = ROOT / "simulation" / "wokwi"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"validation failed: {message}")


def load_json(path: Path) -> object:
    try:
        return json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"validation failed: cannot parse {path}: {error}") from error


def load_toml(path: Path) -> dict:
    try:
        with path.open("rb") as file:
            return tomllib.load(file)
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise SystemExit(f"validation failed: cannot parse {path}: {error}") from error


def firmware_pins(firmware_dir: Path) -> dict[str, int]:
    settings_path = firmware_dir / "src" / "firmware_settings.h"
    try:
        settings = settings_path.read_text()
    except OSError as error:
        raise SystemExit(f"validation failed: cannot read {settings_path}: {error}") from error
    names = (
        "GPS_RX",
        "GPS_TX",
        "OLED_SDA",
        "OLED_SCL",
        "LED_ERROR",
        "LED_DATA",
        "LED_WARNING",
        "LED_OK",
    )
    pins: dict[str, int] = {}
    for name in names:
        match = re.search(rf"\b{name}\s*=\s*(\d+)\s*;", settings)
        require(match is not None, f"missing PinConfig::{name}")
        assert match is not None  # narrowing for type checker (require raises on None)
        pins[name] = int(match.group(1))
    require(len(set(pins.values())) == len(pins), "firmware GPIO assignments must be unique")
    return pins


def validate_diagram(assets_dir: Path, firmware_dir: Path) -> None:
    diagram = load_json(assets_dir / "diagram.json")
    require(isinstance(diagram, dict), "diagram.json must contain an object")
    require(diagram.get("version") == 1, "diagram version must be 1")

    parts = diagram.get("parts")
    connections = diagram.get("connections")
    require(isinstance(parts, list) and parts, "diagram must contain parts")
    require(isinstance(connections, list) and connections, "diagram must contain connections")

    part_ids = [part.get("id") for part in parts if isinstance(part, dict)]
    require(
        all(isinstance(part_id, str) and part_id for part_id in part_ids),
        "every part must have an id",
    )
    require(len(part_ids) == len(set(part_ids)), "diagram part ids must be unique")

    part_types = {
        part["id"]: part.get("type")
        for part in parts
        if isinstance(part, dict) and isinstance(part.get("id"), str)
    }
    require(part_types.get("esp") == "board-esp32-devkit-c-v4", "ESP32 part is missing")
    require(part_types.get("oled") == "board-ssd1306", "SSD1306 part is missing")
    require(part_types.get("gps") == "chip-neo-m8n", "custom GPS part is missing")

    connection_pairs = {
        frozenset((connection[0], connection[1]))
        for connection in connections
        if isinstance(connection, list) and len(connection) >= 2
    }

    pins = firmware_pins(firmware_dir)
    required_connections = (
        ("gps:TX", f"esp:{pins['GPS_RX']}"),
        ("gps:RX", f"esp:{pins['GPS_TX']}"),
        ("oled:SDA", f"esp:{pins['OLED_SDA']}"),
        ("oled:SCL", f"esp:{pins['OLED_SCL']}"),
        ("rErr:1", f"esp:{pins['LED_ERROR']}"),
        ("rData:1", f"esp:{pins['LED_DATA']}"),
        ("rWarn:1", f"esp:{pins['LED_WARNING']}"),
        ("rOk:1", f"esp:{pins['LED_OK']}"),
    )
    for left, right in required_connections:
        require(
            frozenset((left, right)) in connection_pairs,
            f"missing diagram connection {left} <-> {right}",
        )


def validate_assets(assets_dir: Path, firmware_dir: Path) -> None:
    required_files = (
        "diagram.json",
        "libraries.txt",
        "neo-m8n.chip.c",
        "neo-m8n.chip.json",
        "wokwi-api.h",
        "wokwi.toml",
        "TESTING.md",
        ".vscode/extensions.json",
        ".vscode/tasks.json",
    )
    for relative_path in required_files:
        require((assets_dir / relative_path).is_file(), f"missing asset {relative_path}")

    validate_diagram(assets_dir, firmware_dir)

    chip = load_json(assets_dir / "neo-m8n.chip.json")
    require(isinstance(chip, dict), "chip definition must contain an object")
    require(
        chip.get("pins") == ["VCC", "GND", "TX", "RX"], "GPS chip pins do not match the diagram"
    )
    controls = chip.get("controls")
    require(
        isinstance(controls, list) and len(controls) == 1,
        "GPS chip must expose one scenario control",
    )
    scenario = controls[0]
    require(isinstance(scenario, dict), "GPS chip scenario control must be an object")
    require(scenario.get("id") == "scenario", "GPS chip scenario control id mismatch")
    require(
        (scenario.get("min"), scenario.get("max"), scenario.get("step")) == (0, 5, 1),
        "scenario range must be 0..5",
    )

    libraries = {
        line.strip()
        for line in (assets_dir / "libraries.txt").read_text().splitlines()
        if line.strip()
    }
    require("Adafruit GFX Library" in libraries, "Adafruit GFX dependency is missing")
    require("Adafruit SSD1306" in libraries, "Adafruit SSD1306 dependency is missing")

    extensions = load_json(assets_dir / ".vscode" / "extensions.json")
    require(isinstance(extensions, dict), "extensions.json must contain an object")
    require(
        "wokwi.wokwi-vscode" in extensions.get("recommendations", []),
        "Wokwi VS Code extension is not recommended",
    )

    tasks = load_json(assets_dir / ".vscode" / "tasks.json")
    require(isinstance(tasks, dict), "tasks.json must contain an object")
    task_items = tasks.get("tasks", [])
    require(isinstance(task_items, list), "tasks.json tasks must contain a list")
    require(
        any(
            isinstance(task, dict) and task.get("label") == "Wokwi: Build Simulation"
            for task in task_items
        ),
        "VS Code build task is missing",
    )

    config = load_toml(assets_dir / "wokwi.toml")
    require(config.get("wokwi", {}).get("version") == 1, "wokwi.toml version must be 1")
    require(
        config.get("wokwi", {}).get("firmware") == "build/firmware.bin",
        "firmware artifact path mismatch",
    )
    require(
        config.get("wokwi", {}).get("elf") == "build/firmware.elf", "ELF artifact path mismatch"
    )
    chips = config.get("chip", [])
    require(isinstance(chips, list) and len(chips) == 1, "wokwi.toml must define one custom chip")
    require(isinstance(chips[0], dict), "wokwi.toml custom chip must be a table")
    require(chips[0].get("name") == "neo-m8n", "custom chip name mismatch")
    require(chips[0].get("binary") == "neo-m8n.chip.wasm", "custom chip binary path mismatch")


def validate_generated_project(
    project_dir: Path,
    firmware_dir: Path,
    assets_dir: Path,
    require_build: bool,
) -> None:
    require(project_dir.is_dir(), f"generated project not found: {project_dir}")
    for relative_path in ("sketch.ino", "src", "diagram.json", "wokwi.toml", "neo-m8n.chip.c"):
        require(
            (project_dir / relative_path).exists(), f"generated project is missing {relative_path}"
        )
    require(
        not (project_dir / "build" / "doxygen").exists(),
        "ignored firmware documentation was copied",
    )

    entrypoints = list(firmware_dir.glob("*.ino"))
    require(len(entrypoints) == 1, "firmware source must contain one entrypoint")
    require(
        (project_dir / "sketch.ino").read_bytes() == entrypoints[0].read_bytes(),
        "generated sketch does not match the firmware entrypoint",
    )

    for source_path in (firmware_dir / "src").rglob("*"):
        if source_path.is_file():
            relative_path = source_path.relative_to(firmware_dir)
            generated_path = project_dir / relative_path
            require(generated_path.is_file(), f"generated project is missing {relative_path}")
            require(
                generated_path.read_bytes() == source_path.read_bytes(),
                f"generated {relative_path} is stale",
            )

    for asset_path in assets_dir.rglob("*"):
        if asset_path.is_file():
            relative_path = asset_path.relative_to(assets_dir)
            generated_path = project_dir / relative_path
            require(generated_path.is_file(), f"generated project is missing asset {relative_path}")
            require(
                generated_path.read_bytes() == asset_path.read_bytes(),
                f"generated asset {relative_path} is stale",
            )

    if require_build:
        for relative_path in ("build/firmware.bin", "build/firmware.elf", "neo-m8n.chip.wasm"):
            path = project_dir / relative_path
            require(
                path.is_file() and path.stat().st_size > 0,
                f"missing build artifact {relative_path}",
            )

        require(
            (project_dir / "build" / "firmware.bin").read_bytes()[:1] == b"\xe9",
            "firmware.bin is not an ESP32 image",
        )
        require(
            (project_dir / "build" / "firmware.elf").read_bytes()[:4] == b"\x7fELF",
            "firmware.elf is not an ELF file",
        )
        require(
            (project_dir / "neo-m8n.chip.wasm").read_bytes()[:4] == b"\0asm",
            "custom chip is not WebAssembly",
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets-dir", type=Path, default=DEFAULT_ASSETS_DIR)
    parser.add_argument("--firmware-dir", type=Path, default=DEFAULT_FIRMWARE_DIR)
    parser.add_argument("--project-dir", type=Path)
    parser.add_argument("--require-build", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    assets_dir = args.assets_dir.resolve()
    firmware_dir = args.firmware_dir.resolve()
    validate_assets(assets_dir, firmware_dir)

    if args.project_dir:
        validate_generated_project(
            args.project_dir.resolve(),
            firmware_dir,
            assets_dir,
            args.require_build,
        )
    elif args.require_build:
        validate_generated_project(
            DEFAULT_PROJECT_DIR.resolve(),
            firmware_dir,
            assets_dir,
            True,
        )

    print("Wokwi project validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
