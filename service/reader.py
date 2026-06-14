"""Serial reader - runs pyserial in a thread pool, feeds data via asyncio.

Design notes
------------
- pyserial-asyncio 0.6 silently blocks on Python 3.13; plain pyserial via
  asyncio.to_thread() is used instead.
- asyncio.to_thread() tasks cannot be cancelled while the thread is blocking.
  A threading.Event stop flag with a 1-second readline timeout lets the thread
  exit cleanly within ~1 s of a shutdown signal.
- dsrdtr=False, rtscts=False prevent the CP2102 from resetting the ESP32 when
  the serial port is opened or closed.
- If no data arrives within 10 s of connecting, the port is closed and reopened
  to recover from the rare case where the file descriptor silently stalls.

Lifecycle: call start() at FastAPI lifespan startup, stop() at shutdown.
"""

from __future__ import annotations

import asyncio
import json
import logging
import threading
import time

import config
import database
import serial

log = logging.getLogger("reader")

SERIAL_RETRY_DELAY_SECONDS = 5.0
SERIAL_STALL_TIMEOUT_SECONDS = 10.0

# Live state shared with API handlers (written only from _loop).
# Protected by _state_lock because the dict reference is replaced from a
# thread-pool worker while async handlers may be iterating it for JSON
# serialisation.
_state_lock: threading.Lock = threading.Lock()
current_state: dict = {}
_state_received_at: float | None = None

_stop: threading.Event = threading.Event()
_task: asyncio.Task | None = None


def get_state() -> dict:
    """Return the current GPS state, or an empty dict when it is stale."""
    with _state_lock:
        if _state_received_at is None:
            return {}
        if time.monotonic() - _state_received_at > config.STATE_STALE_SECONDS:
            return {}
        return current_state


def _set_state(state: dict, received_at: float | None = None) -> None:
    """Replace the current GPS state (thread-safe)."""
    global current_state, _state_received_at
    with _state_lock:
        current_state = state
        _state_received_at = (
            (time.monotonic() if received_at is None else received_at) if state else None
        )


def get_health() -> dict:
    """Return serial-reader health metadata for diagnostics and API errors."""
    with _state_lock:
        age_seconds = (
            None if _state_received_at is None else max(0.0, time.monotonic() - _state_received_at)
        )
    task = _task
    return {
        "task_running": task is not None and not task.done(),
        "state_age_seconds": age_seconds,
        "state_stale": age_seconds is None or age_seconds > config.STATE_STALE_SECONDS,
    }


# ── Lifecycle ──────────────────────────────────────────────────────────────────


async def start() -> None:
    global _task
    if _task is not None and not _task.done():
        return

    _stop.clear()
    _task = asyncio.create_task(_run(), name="serial-reader")


def request_stop() -> None:
    """Signal the blocking serial loop to finish at its next timeout."""
    _stop.set()


async def stop() -> None:
    global _task
    request_stop()
    task = _task
    if task is not None and not task.done():
        try:
            await asyncio.wait_for(asyncio.shield(task), timeout=3.0)
        except asyncio.TimeoutError:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
    _task = None


# ── Internal ───────────────────────────────────────────────────────────────────


def _open_port() -> serial.Serial:
    return serial.Serial(
        config.SERIAL_PORT,
        config.BAUD_RATE,
        timeout=1,  # readline() returns every 1 s even with no data
        dsrdtr=False,  # no DTR toggle - prevents ESP32 reset on open/close
        rtscts=False,
    )


def _readline_interruptible(ser: serial.Serial) -> bytes:
    """Read one bounded line; returns b"" when stopped or on serial timeout."""
    while not _stop.is_set():
        line = ser.read_until(b"\n", config.SERIAL_MAX_LINE_BYTES)
        if line:
            return line
    return b""


async def _run() -> None:
    while not _stop.is_set():
        try:
            await _loop()
        except asyncio.CancelledError:
            raise
        except (OSError, serial.SerialException) as exc:
            if _stop.is_set():
                break
            log.warning("Serial fault (%s), retry in %.0f s …", exc, SERIAL_RETRY_DELAY_SECONDS)
        except Exception:
            if _stop.is_set():
                break
            log.exception(
                "Unexpected serial-reader failure; retry in %.0f s", SERIAL_RETRY_DELAY_SECONDS
            )

        retry_ticks = max(1, int(SERIAL_RETRY_DELAY_SECONDS * 10))
        for _ in range(retry_ticks):
            if _stop.is_set():
                break
            await asyncio.sleep(0.1)


async def _loop() -> None:
    ser: serial.Serial = await asyncio.to_thread(_open_port)
    log.info("Serial connected: %s @ %d baud", config.SERIAL_PORT, config.BAUD_RATE)

    loop = asyncio.get_running_loop()
    deadline = loop.time() + SERIAL_STALL_TIMEOUT_SECONDS
    received_any_data = False

    try:
        while not _stop.is_set():
            raw = await asyncio.to_thread(_readline_interruptible, ser)

            if not raw:
                if not received_any_data and loop.time() > deadline:
                    log.warning("No data within 10 s of connect - reopening port")
                    raise OSError("stalled port")
                continue

            received_any_data = True
            line = raw.decode("utf-8", errors="replace").strip()

            if not line:
                continue

            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            if not isinstance(msg, dict):
                log.debug("Ignoring JSON serial message that is not an object")
                continue

            msg_type = msg.get("type")

            if msg_type == "parsed_state":
                _set_state(msg)
                await database.insert(msg)
            elif msg_type == "startup":
                log.info("ESP32 firmware %s", msg.get("version", "?"))
            elif msg_type == "error":
                log.warning("ESP32 error: %s", msg.get("msg"))
            elif msg_type != "raw_nmea":
                log.debug("Unknown message type: %s", msg_type)

    finally:
        await asyncio.to_thread(ser.close)
