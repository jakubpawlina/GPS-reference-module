#!/usr/bin/env python3
"""Regression tests for the Raspberry Pi service."""

from __future__ import annotations

import asyncio
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import AsyncMock, patch


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "service"))

import api  # noqa: E402
import config  # noqa: E402
import database  # noqa: E402
import reader  # noqa: E402
from fastapi import HTTPException  # noqa: E402


TEST_DESCRIPTIONS = {
    "test_database_round_trip_and_stats": (
        "Persists parsed states and preserves cursor, time-range, and storage statistics."
    ),
    "test_database_cleanup_removes_oldest_rows": (
        "Enforces the configured storage threshold by removing the oldest records first."
    ),
    "test_api_rejects_invalid_ranges_and_webhooks": (
        "Rejects reversed time windows and non-HTTP webhook destinations before I/O."
    ),
    "test_reader_start_and_stop_are_idempotent": (
        "Starts one serial task and shuts it down cooperatively without leaking task state."
    ),
}


class ServiceTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.original_db_path = config.DB_PATH
        self.original_max_db_bytes = config.MAX_DB_BYTES
        config.DB_PATH = str(Path(self.temp_dir.name) / "positions.db")
        config.MAX_DB_BYTES = 1024**3
        await database.init()

    async def asyncTearDown(self) -> None:
        await reader.stop()
        config.DB_PATH = self.original_db_path
        config.MAX_DB_BYTES = self.original_max_db_bytes
        self.temp_dir.cleanup()

    async def test_database_round_trip_and_stats(self) -> None:
        record = {
            "state": "REFERENCE_OK",
            "valid": True,
            "fix": True,
            "fixType": "3D",
            "satellitesUsed": 9,
            "latitude": 50.026651,
            "longitude": 19.953602,
        }

        row_id = await database.insert(record)
        rows = await database.since(0, 10)
        ranged = await database.time_range(0, rows[0]["ts"] + 1, 10)
        stats = await database.stats()

        self.assertEqual(row_id, 1)
        self.assertEqual([row["id"] for row in rows], [1])
        self.assertEqual([row["id"] for row in ranged], [1])
        self.assertEqual(stats["record_count"], 1)
        self.assertGreater(stats["db_size_bytes"], 0)

    async def test_database_cleanup_removes_oldest_rows(self) -> None:
        for sequence in range(20):
            await database.insert({"state": f"state-{sequence}"})

        config.MAX_DB_BYTES = 1
        await database._cleanup_if_needed()
        rows = await database.since(0, 100)

        self.assertEqual(len(rows), 19)
        self.assertEqual(rows[0]["state"], "state-1")

    async def test_api_rejects_invalid_ranges_and_webhooks(self) -> None:
        with self.assertRaises(HTTPException) as range_error:
            await api.get_range(ts_from=10.0, ts_to=0.0, limit=10)
        self.assertEqual(range_error.exception.status_code, 422)

        original_webhook = config.CLOUD_WEBHOOK
        try:
            config.CLOUD_WEBHOOK = "file:///tmp/positions.json"
            with self.assertRaises(HTTPException) as webhook_error:
                await api.upload(since_cursor=0, limit=10)
            self.assertEqual(webhook_error.exception.status_code, 400)
        finally:
            config.CLOUD_WEBHOOK = original_webhook

    async def test_reader_start_and_stop_are_idempotent(self) -> None:
        calls = 0

        async def fake_run() -> None:
            nonlocal calls
            calls += 1
            while not reader._stop.is_set():
                await asyncio.sleep(0.01)

        with patch.object(reader, "_run", new=fake_run):
            await reader.start()
            first_task = reader._task
            await reader.start()
            self.assertIs(reader._task, first_task)
            await reader.stop()

        self.assertEqual(calls, 1)
        self.assertIsNone(reader._task)


class ReportingResult(unittest.TextTestResult):
    def addSuccess(self, test: unittest.TestCase) -> None:
        super().addSuccess(test)
        name = test._testMethodName
        self.stream.writeln(
            f"PASS\t{name.removeprefix('test_').replace('_', ' ').title()}"
            f"\t{TEST_DESCRIPTIONS[name]}"
        )


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ServiceTests)
    runner = unittest.TextTestRunner(
        stream=sys.stdout,
        verbosity=0,
        resultclass=ReportingResult,
    )
    result = runner.run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
