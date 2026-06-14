#!/usr/bin/env python3
"""Local development server for the GPS Reference Service.

Starts the FastAPI service on localhost with a virtual serial port that feeds
realistic fake GPS data — no ESP32 hardware or Raspberry Pi required.

How it works
------------
1. A pty (pseudo-terminal) pair is created.  The service reads from one end;
   this script writes fake parsed_state JSON to the other end at 1 Hz.
2. A temporary SQLite database is used so dev data never pollutes anything.
3. Environment variables are set before any service module is imported, so
   config.py picks them up at module load time.

Usage
-----
    mise run service:dev                   # http://127.0.0.1:8000
    python tools/run-service-dev.py        # same, any Python - auto-picks venv
    python tools/run-service-dev.py --port 9000

Stopping
--------
    Ctrl-C  — uvicorn shuts down cleanly; the temp database is removed.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# ── Venv bootstrap (must be first, before any resource allocation) ─────────────
#
# Re-exec under the project venv if we are not already running inside it.
# This makes  `python tools/run-service-dev.py`  work even when the active
# Python is the system interpreter or a different venv.

_venv_python = ROOT / ".venv" / "bin" / "python"

if sys.prefix != str(ROOT / ".venv"):
    if not _venv_python.exists():
        print(
            "ERROR: .venv not found.  Run  mise run service:bootstrap  first.",
            file=sys.stderr,
        )
        sys.exit(1)
    # os.execv replaces the current process — all env vars are inherited and
    # no resources have been allocated yet, so there is nothing to clean up.
    os.execv(str(_venv_python), [str(_venv_python)] + sys.argv)

# ── Remaining imports (safe: we are now inside the venv) ──────────────────────

import asyncio
import contextlib
import json
import logging
import math
import pty
import signal
import tempfile
import threading
import time

# When launched via `trap '' INT; exec ...`, SIGINT is inherited as SIG_IGN
# (preserved across the os.execv venv re-exec above).  Re-enable it here
# so KeyboardInterrupt works normally for uvicorn shutdown.
signal.signal(signal.SIGINT, signal.default_int_handler)

# ── Argument parsing ───────────────────────────────────────────────────────────

_args = sys.argv[1:]
_http_port = 8000
if "--port" in _args:
    try:
        _http_port = int(_args[_args.index("--port") + 1])
    except (IndexError, ValueError):
        print("Usage: run-service-dev.py [--port PORT]", file=sys.stderr)
        sys.exit(1)

# ── Virtual serial port ────────────────────────────────────────────────────────

# openpty() returns (master_fd, slave_fd).  The service opens the slave end as
# a serial port; this script writes fake JSON to the master end.
_master_fd, _slave_fd = pty.openpty()
_serial_port = os.ttyname(_slave_fd)  # e.g. /dev/pts/7

# ── Temporary database ─────────────────────────────────────────────────────────

_tmp_db = tempfile.NamedTemporaryFile(suffix=".db", prefix="gps-dev-", delete=False)
_tmp_db.close()

# ── Environment — must be set before importing any service module ──────────────

os.environ["GPS_SERIAL_PORT"] = _serial_port
os.environ["GPS_BAUD_RATE"] = "115200"
os.environ["GPS_DB_PATH"] = _tmp_db.name
os.environ["GPS_HTTP_HOST"] = "127.0.0.1"
os.environ["GPS_HTTP_PORT"] = str(_http_port)

# ── Service imports (after env vars) ──────────────────────────────────────────

sys.path.insert(0, str(ROOT / "service"))

import uvicorn  # noqa: E402
from api import app  # noqa: E402

# ── Fake GPS data generator ────────────────────────────────────────────────────

# Krakow, Poland — change to your preferred development location.
_CENTER_LAT = 50.026651
_CENTER_LON = 19.953602
_CENTER_ALT = 263.0


def _utc_strings() -> tuple[str, str]:
    t = time.gmtime()
    return (
        f"{t.tm_hour:02d}{t.tm_min:02d}{t.tm_sec:02d}.00",
        f"{t.tm_mday:02d}{t.tm_mon:02d}{t.tm_year % 100:02d}",
    )


def _make_startup() -> dict:
    return {
        "type": "startup",
        "module": "gps-reference-module",
        "version": "1.2.0-dev",
        "usbBaud": 115200,
        "gpsBaud": 9600,
        "rawNmeaJson": True,
        "oledAddress": None,
        "displayReady": False,
    }


def _make_state(millis: int) -> dict:
    """Build a parsed_state record with gentle simulated position drift."""
    angle = millis / 1000 * 0.008  # one full circle ≈ 785 s
    lat = _CENTER_LAT + math.sin(angle) * 0.0003
    lon = _CENTER_LON + math.cos(angle) * 0.0005
    alt = _CENTER_ALT + math.sin(angle * 3) * 1.5
    utc_time, utc_date = _utc_strings()

    return {
        "type": "parsed_state",
        "millis": millis & 0xFFFFFFFF,  # uint32 wrap as on the ESP32
        "state": "REFERENCE_OK",
        "valid": True,
        "gpsData": True,
        "displayReady": False,
        "fix": True,
        "fixType": "3D",
        "fixQuality": 1,
        "satellitesUsed": 8,
        "latitude": round(lat, 6),
        "longitude": round(lon, 6),
        "altitudeM": round(alt, 1),
        "geoidSeparationM": 40.0,
        "hdop": 0.8,
        "pdop": 1.2,
        "vdop": 0.9,
        "speedKnots": None,
        "speedKmh": None,
        "courseDeg": None,
        "utcTime": utc_time,
        "utcDate": utc_date,
        "nmeaAgeMs": 80,
        "ggaAgeMs": 80,
        "gsaAgeMs": 120,
        "rmcAgeMs": None,
        "rawSentenceCount": millis // 200,
        "acceptedSentenceCount": millis // 250,
        "checksumErrorCount": 0,
        "bufferOverflowCount": 0,
    }


# ── Writer thread ──────────────────────────────────────────────────────────────


def _writer(master_fd: int) -> None:
    """Write fake GPS JSON lines to the master pty end at 1 Hz."""
    start_ms = int(time.monotonic() * 1000)

    try:
        os.write(master_fd, (json.dumps(_make_startup()) + "\n").encode())
    except OSError:
        return

    while True:
        time.sleep(1.0)
        millis = int(time.monotonic() * 1000) - start_ms
        try:
            os.write(master_fd, (json.dumps(_make_state(millis)) + "\n").encode())
        except OSError:
            break  # service closed the port — exit quietly


# ── Cleanup ────────────────────────────────────────────────────────────────────


def _cleanup() -> None:
    for fd in (_master_fd, _slave_fd):
        try:
            os.close(fd)
        except OSError:
            pass
    try:
        os.unlink(_tmp_db.name)
    except OSError:
        pass


# ── Main ───────────────────────────────────────────────────────────────────────


def main() -> None:
    print()
    print("  GPS Reference — local development server")
    print(f"  Virtual serial port : {_serial_port}")
    print(f"  Database            : {_tmp_db.name}  (deleted on exit)")
    print(f"  Dashboard           : http://127.0.0.1:{_http_port}/")
    print(f"  Swagger UI          : http://127.0.0.1:{_http_port}/docs")
    print(f"  Live stream         : http://127.0.0.1:{_http_port}/api/stream")
    print()

    threading.Thread(target=_writer, args=(_master_fd,), daemon=True).start()

    # Suppress CancelledError tracebacks that uvicorn/starlette log during
    # shutdown — they are expected when the graceful timeout expires.
    class _SuppressCancelledError(logging.Filter):
        def filter(self, record: logging.LogRecord) -> bool:
            return "CancelledError" not in record.getMessage()

    logging.getLogger("uvicorn.error").addFilter(_SuppressCancelledError())

    async def _serve() -> None:
        cfg = uvicorn.Config(
            app,
            host="127.0.0.1",
            port=_http_port,
            log_level="info",
            timeout_graceful_shutdown=1,
        )
        server = uvicorn.Server(cfg)

        # Replace uvicorn's capture_signals so it doesn't re-raise SIGINT
        # after shutdown — that re-raise causes cascading tracebacks through
        # asyncio and starlette that are pure noise for a dev server.
        @contextlib.contextmanager
        def _clean_shutdown():
            original = {}
            for sig in (signal.SIGINT, signal.SIGTERM):
                original[sig] = signal.getsignal(sig)
                signal.signal(sig, server.handle_exit)
            try:
                yield
            finally:
                for sig, handler in original.items():
                    signal.signal(sig, handler)

        server.capture_signals = _clean_shutdown
        await server.serve()

    try:
        asyncio.run(_serve())
    except (KeyboardInterrupt, SystemExit):
        pass
    finally:
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        _cleanup()
        print()


if __name__ == "__main__":
    main()
    sys.exit(0)
