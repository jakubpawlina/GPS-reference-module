"""SQLite storage for GPS position records.

Schema (positions table)
------------------------
id          INTEGER PK  - monotonically increasing; used as polling cursor
ts          REAL        - Unix timestamp (RPi clock, seconds)
state       TEXT        - REFERENCE_OK | DEGRADED_* | NO_FIX | NO_GPS_DATA
valid       INTEGER     - 1 when usable position is available
fix         INTEGER     - 1 when GPS has any fix
fix_type    TEXT        - 3D | 2D | NONE | UNKNOWN
satellites  INTEGER     - satellites used in the solution
lat         REAL        - decimal degrees WGS-84 (null if no fix)
lon         REAL        - decimal degrees WGS-84 (null if no fix)
alt_m       REAL        - altitude above MSL in metres (null if no 3D fix)
hdop        REAL
pdop        REAL
vdop        REAL
speed_kmh   REAL
utc_time    TEXT        - HHMMSS.ss from NMEA
utc_date    TEXT        - DDMMYY from NMEA
raw_json    TEXT        - complete parsed_state JSON for forward compatibility

Connection lifecycle
--------------------
A single persistent connection is held open for the lifetime of the service.
This avoids per-operation connection overhead (~86 400 open/close cycles per
day at 1 Hz inserts) and keeps WAL mode active across writes.  init() opens
the connection; close() is called from the FastAPI lifespan shutdown.
"""

from __future__ import annotations

import json
import logging
import os
import time

import aiosqlite
import config

log = logging.getLogger("database")

_DDL = """
CREATE TABLE IF NOT EXISTS positions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts          REAL    NOT NULL,
    state       TEXT,
    valid       INTEGER,
    fix         INTEGER,
    fix_type    TEXT,
    satellites  INTEGER,
    lat         REAL,
    lon         REAL,
    alt_m       REAL,
    hdop        REAL,
    pdop        REAL,
    vdop        REAL,
    speed_kmh   REAL,
    utc_time    TEXT,
    utc_date    TEXT,
    raw_json    TEXT
);

CREATE INDEX IF NOT EXISTS idx_positions_ts ON positions (ts);
"""

_db: aiosqlite.Connection | None = None


async def _conn() -> aiosqlite.Connection:
    """Return the persistent connection, raising if init() was not called."""
    if _db is None:
        raise RuntimeError("database.init() has not been called")
    return _db


async def init() -> None:
    global _db
    os.makedirs(os.path.dirname(os.path.abspath(config.DB_PATH)), exist_ok=True)
    _db = await aiosqlite.connect(config.DB_PATH)
    _db.row_factory = aiosqlite.Row
    await _db.execute("PRAGMA journal_mode = WAL")
    await _db.execute("PRAGMA synchronous = NORMAL")
    await _db.executescript(_DDL)
    await _db.commit()
    log.info("Database ready: %s", config.DB_PATH)


async def close() -> None:
    global _db
    if _db is not None:
        await _db.close()
        _db = None


async def insert(record: dict) -> int:
    """Persist one parsed_state record.  Returns the new row id."""
    db = await _conn()
    cur = await db.execute(
        """
        INSERT INTO positions
            (ts, state, valid, fix, fix_type, satellites,
             lat, lon, alt_m, hdop, pdop, vdop,
             speed_kmh, utc_time, utc_date, raw_json)
        VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
        """,
        (
            time.time(),
            record.get("state"),
            1 if record.get("valid") else 0,
            1 if record.get("fix") else 0,
            record.get("fixType"),
            record.get("satellitesUsed"),
            record.get("latitude"),
            record.get("longitude"),
            record.get("altitudeM"),
            record.get("hdop"),
            record.get("pdop"),
            record.get("vdop"),
            record.get("speedKmh"),
            record.get("utcTime"),
            record.get("utcDate"),
            json.dumps(record),
        ),
    )
    row_id = cur.lastrowid
    await db.commit()

    if row_id is None:
        raise RuntimeError("SQLite did not return an id for the inserted position")

    try:
        await _cleanup_if_needed()
    except Exception:
        # Cleanup failures (e.g. VACUUM on a locked DB) must not interrupt
        # the serial reader loop - log and continue.
        log.exception("Storage cleanup failed; continuing without cleanup")
    return row_id


def _storage_size_bytes() -> int:
    """Return the SQLite database size including WAL and shared-memory files."""
    total = 0
    for suffix in ("", "-wal", "-shm"):
        try:
            total += os.path.getsize(f"{config.DB_PATH}{suffix}")
        except FileNotFoundError:
            pass
    return total


async def _cleanup_if_needed() -> None:
    """Delete old rows and reclaim disk space when storage approaches the cap."""
    size = _storage_size_bytes()
    if size < config.MAX_DB_BYTES * 0.95:
        return

    db = await _conn()
    async with db.execute("SELECT COUNT(*) FROM positions") as cur:
        row = await cur.fetchone()
        (total,) = row if row else (0,)

    if total == 0:
        return

    to_delete = max(1, int(total * config.CLEANUP_FRAC))
    log.warning(
        "Storage at %.1f MiB (%.0f%% of cap) - removing %d oldest records",
        size / 1024**2,
        size / config.MAX_DB_BYTES * 100,
        to_delete,
    )
    await db.execute(
        "DELETE FROM positions WHERE id IN (SELECT id FROM positions ORDER BY id ASC LIMIT ?)",
        (to_delete,),
    )
    await db.commit()
    async with db.execute("PRAGMA wal_checkpoint(TRUNCATE)") as cur:
        await cur.fetchone()
    await db.execute("VACUUM")

    new_size = _storage_size_bytes()
    log.info(
        "Storage cleanup: removed %d oldest rows (%.1f MiB -> %.1f MiB)",
        to_delete,
        size / 1024**2,
        new_size / 1024**2,
    )


# ── Queries ────────────────────────────────────────────────────────────────────


async def since(cursor: int, limit: int = 1000) -> list[dict]:
    """Records with id > cursor, oldest-first."""
    db = await _conn()
    async with db.execute(
        "SELECT * FROM positions WHERE id > ? ORDER BY id ASC LIMIT ?",
        (cursor, limit),
    ) as cur:
        return [dict(r) for r in await cur.fetchall()]


async def time_range(ts_from: float, ts_to: float, limit: int = 10_000) -> list[dict]:
    db = await _conn()
    async with db.execute(
        "SELECT * FROM positions WHERE ts >= ? AND ts <= ? ORDER BY ts ASC LIMIT ?",
        (ts_from, ts_to, limit),
    ) as cur:
        return [dict(r) for r in await cur.fetchall()]


async def stats() -> dict:
    size = _storage_size_bytes()

    db = await _conn()
    async with db.execute("SELECT COUNT(*), MIN(ts), MAX(ts) FROM positions") as cur:
        agg = await cur.fetchone()
        total, ts_min, ts_max = agg if agg else (0, None, None)

    return {
        "record_count": total or 0,
        "oldest_ts": ts_min,
        "newest_ts": ts_max,
        "db_size_bytes": size,
        "db_size_mb": round(size / 1024**2, 1),
        "max_size_mb": round(config.MAX_DB_BYTES / 1024**2, 1),
        "usage_pct": round(size / config.MAX_DB_BYTES * 100, 2) if config.MAX_DB_BYTES else 0,
    }
