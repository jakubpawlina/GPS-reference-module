#!/usr/bin/env python3
"""Regression and HTTP-layer tests for the Raspberry Pi service.

Test classes
------------
ServiceTests   - Unit-style tests for the database, reader, and API helpers.
ApiHttpTests   - Full HTTP round-trip tests via AsyncClient + ASGITransport.
               The ASGI lifespan is intentionally bypassed (no serial reader
               is started); the database is initialised directly in asyncSetUp.
"""

from __future__ import annotations

import asyncio
import sys
import tempfile
import time as time_module
import unittest
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch

import httpx
from httpx import ASGITransport, AsyncClient

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "service"))

import api  # noqa: E402
import config  # noqa: E402
import database  # noqa: E402
import reader  # noqa: E402
from fastapi import HTTPException  # noqa: E402

# A realistic parsed_state payload matching the firmware JSON schema.
_SAMPLE_STATE: dict = {
    "type": "parsed_state",
    "state": "REFERENCE_OK",
    "valid": True,
    "fix": True,
    "fixType": "3D",
    "satellitesUsed": 8,
    "latitude": 50.0267,
    "longitude": 19.9536,
    "altitudeM": 263.1,
    "hdop": 0.8,
    "speedKmh": 0.0,
    "utcTime": "120000.00",
    "utcDate": "140626",
}

TEST_DESCRIPTIONS: dict[str, str] = {
    # ── ServiceTests ──────────────────────────────────────────────────────────
    "test_database_round_trip_and_stats": (
        "Persists parsed states and preserves cursor, time-range, and storage statistics."
    ),
    "test_database_cleanup_removes_oldest_rows": (
        "Enforces the configured storage threshold by removing the oldest records first."
    ),
    "test_database_close_is_idempotent": (
        "Calling close() twice does not raise so shutdown paths are robust."
    ),
    "test_database_operations_fail_after_close": (
        "Operations on a closed database raise RuntimeError immediately."
    ),
    "test_api_rejects_invalid_ranges_and_webhooks": (
        "Rejects reversed time windows and non-HTTP webhook destinations before I/O."
    ),
    "test_api_rejects_missing_webhook_url": (
        "Returns HTTP 400 when GPS_CLOUD_WEBHOOK is not configured."
    ),
    "test_api_status_returns_503_before_first_message": (
        "Returns HTTP 503 when the serial reader has not yet received any data."
    ),
    "test_stream_disables_caching_and_proxy_buffering": (
        "SSE responses disable caches and nginx buffering so updates arrive immediately."
    ),
    "test_reader_skips_malformed_json": (
        "Silently discards lines that are not valid JSON without crashing the reader loop."
    ),
    "test_reader_start_and_stop_are_idempotent": (
        "Starts one serial task and shuts it down cooperatively without leaking task state."
    ),
    # ── ApiHttpTests ──────────────────────────────────────────────────────────
    "test_status_200_with_live_state": (
        "GET /api/status returns 200 with the current GPS state when data is available."
    ),
    "test_status_503_without_data": (
        "GET /api/status returns 503 before the first parsed_state message arrives."
    ),
    "test_stats_schema_and_values": (
        "GET /api/stats returns all expected schema keys with sensible numeric values."
    ),
    "test_since_empty_on_fresh_database": (
        "GET /api/records/since returns an empty list and cursor=0 on a fresh database."
    ),
    "test_since_cursor_pagination": (
        "GET /api/records/since pages through records in two calls and advances the cursor."
    ),
    "test_since_rejects_negative_cursor": (
        "GET /api/records/since returns 422 for a cursor below the minimum."
    ),
    "test_range_filters_by_timestamp": (
        "GET /api/records/range returns only records within the requested time window."
    ),
    "test_range_rejects_reversed_window": (
        "GET /api/records/range returns 422 when ts_to is less than ts_from."
    ),
    "test_upload_posts_to_webhook_and_returns_count": (
        "POST /api/upload sends all pending records to the webhook and returns the count."
    ),
    "test_upload_skips_when_no_new_records": (
        "POST /api/upload returns uploaded=0 immediately when the cursor is already up to date."
    ),
    "test_upload_incremental_cursor": (
        "POST /api/upload with since_cursor transfers only the records after the cursor."
    ),
    "test_upload_returns_502_on_webhook_error": (
        "POST /api/upload returns 502 when the webhook responds with an HTTP error status."
    ),
    "test_since_rejects_zero_limit": ("GET /api/records/since returns 422 when limit is 0."),
    "test_since_rejects_overlimit": (
        "GET /api/records/since returns 422 when limit exceeds the configured maximum."
    ),
    "test_range_ts_to_defaults_to_now": (
        "GET /api/records/range accepts a request without ts_to and defaults the window end to now."
    ),
    "test_endpoints_lists_all_paths": (
        "GET /api/endpoints lists every documented API path in the catalogue."
    ),
    "test_dashboard_returns_html_page": (
        "GET / serves an HTML page containing the GPS dashboard and SSE client script."
    ),
    # ── ApiKeyTests ──────────────────────────────────────────────────────────
    "test_status_open_without_token": (
        "GET /api/status remains accessible when API key is configured."
    ),
    "test_stats_open_without_token": (
        "GET /api/stats remains accessible when API key is configured."
    ),
    "test_records_since_open_without_token": (
        "GET /api/records/since remains accessible when API key is configured."
    ),
    "test_records_range_open_without_token": (
        "GET /api/records/range remains accessible when API key is configured."
    ),
    "test_endpoints_open_without_token": (
        "GET /api/endpoints remains accessible when API key is configured."
    ),
    "test_dashboard_open_without_token": ("GET / remains accessible when API key is configured."),
    "test_upload_rejected_without_token": (
        "POST /api/upload returns 401 when API key is configured but no token is provided."
    ),
    "test_upload_rejected_with_wrong_token": (
        "POST /api/upload returns 401 when the provided Bearer token does not match."
    ),
    "test_upload_accepted_with_correct_token": (
        "POST /api/upload succeeds when the correct Bearer token is provided."
    ),
    "test_upload_open_when_api_key_not_set": (
        "POST /api/upload requires no token when GPS_API_KEY is empty."
    ),
}


# ── Unit-style tests ───────────────────────────────────────────────────────────


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
        await database.close()
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

    async def test_api_rejects_missing_webhook_url(self) -> None:
        original_webhook = config.CLOUD_WEBHOOK
        try:
            config.CLOUD_WEBHOOK = ""
            with self.assertRaises(HTTPException) as ctx:
                await api.upload(since_cursor=0, limit=10)
            self.assertEqual(ctx.exception.status_code, 400)
            self.assertIn("GPS_CLOUD_WEBHOOK", ctx.exception.detail)
        finally:
            config.CLOUD_WEBHOOK = original_webhook

    async def test_api_status_returns_503_before_first_message(self) -> None:
        original_state = reader.get_state()
        try:
            reader._set_state({})
            with self.assertRaises(HTTPException) as ctx:
                await api.get_status()
            self.assertEqual(ctx.exception.status_code, 503)
        finally:
            reader._set_state(original_state)

    async def test_stream_disables_caching_and_proxy_buffering(self) -> None:
        response = await api.stream()
        self.assertEqual(response.headers["cache-control"], "no-cache")
        self.assertEqual(response.headers["x-accel-buffering"], "no")
        await response.body_iterator.aclose()

    async def test_database_close_is_idempotent(self) -> None:
        """Calling close() twice must not raise so shutdown paths are robust."""
        await database.close()
        await database.close()
        # Re-open so tearDown's close() and subsequent tests still work.
        config.DB_PATH = str(Path(self.temp_dir.name) / "positions.db")
        await database.init()

    async def test_database_operations_fail_after_close(self) -> None:
        """Operations on a closed connection raise RuntimeError immediately."""
        await database.close()
        with self.assertRaises(RuntimeError):
            await database.insert({"state": "REFERENCE_OK"})
        with self.assertRaises(RuntimeError):
            await database.since(0)
        with self.assertRaises(RuntimeError):
            await database.stats()
        # Re-open so tearDown's close() and subsequent tests still work.
        config.DB_PATH = str(Path(self.temp_dir.name) / "positions.db")
        await database.init()

    async def test_reader_skips_malformed_json(self) -> None:
        """Lines that are not valid JSON must be silently discarded without crashing."""
        feed = [
            b"not json at all\n",
            b"{broken\n",
            b"\n",
            b'{"type":"parsed_state","state":"REFERENCE_OK"}\n',
        ]
        call_index = 0

        def fake_readline(_: object) -> bytes:
            nonlocal call_index
            if call_index < len(feed):
                line = feed[call_index]
                call_index += 1
                return line
            reader._stop.set()
            return b""

        class FakeSerial:
            def close(self) -> None:
                pass

        fake_ser = FakeSerial()

        with (
            patch.object(reader, "_open_port", new=lambda: fake_ser),
            patch.object(reader, "_readline_interruptible", new=fake_readline),
        ):
            reader._stop.clear()
            await reader._loop()

        # Only the valid parsed_state line should have been inserted.
        rows = await database.since(0, 10)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["state"], "REFERENCE_OK")

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


# ── HTTP-layer tests ───────────────────────────────────────────────────────────


class ApiHttpTests(unittest.IsolatedAsyncioTestCase):
    """Full HTTP round-trip tests via AsyncClient + ASGITransport.

    Each test creates a fresh in-memory database and makes real HTTP requests
    to the FastAPI app.  The ASGI lifespan is bypassed so no serial reader
    task is started; reader state is controlled directly per test.
    """

    async def asyncSetUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self._orig_db_path = config.DB_PATH
        self._orig_max_db_bytes = config.MAX_DB_BYTES
        self._orig_webhook = config.CLOUD_WEBHOOK
        self._orig_api_key = config.API_KEY
        config.DB_PATH = str(Path(self.temp_dir.name) / "positions.db")
        config.MAX_DB_BYTES = 1024**3
        config.CLOUD_WEBHOOK = ""
        config.API_KEY = ""
        await database.init()
        reader._set_state({})
        self.client = AsyncClient(
            transport=ASGITransport(app=api.app),
            base_url="http://test",
        )

    async def asyncTearDown(self) -> None:
        await self.client.aclose()
        await database.close()
        config.DB_PATH = self._orig_db_path
        config.MAX_DB_BYTES = self._orig_max_db_bytes
        config.CLOUD_WEBHOOK = self._orig_webhook
        config.API_KEY = self._orig_api_key
        reader._set_state({})
        self.temp_dir.cleanup()

    # ── /api/status ───────────────────────────────────────────────────────────

    async def test_status_200_with_live_state(self) -> None:
        reader._set_state(_SAMPLE_STATE.copy())

        resp = await self.client.get("/api/status")

        self.assertEqual(resp.status_code, 200)
        body = resp.json()
        self.assertEqual(body["state"], "REFERENCE_OK")
        self.assertAlmostEqual(body["latitude"], 50.0267)
        self.assertAlmostEqual(body["longitude"], 19.9536)
        self.assertEqual(body["fixType"], "3D")
        self.assertEqual(body["satellitesUsed"], 8)

    async def test_status_503_without_data(self) -> None:
        reader._set_state({})

        resp = await self.client.get("/api/status")

        self.assertEqual(resp.status_code, 503)
        self.assertIn("ESP32", resp.json()["detail"])

    # ── /api/stats ────────────────────────────────────────────────────────────

    async def test_stats_schema_and_values(self) -> None:
        await database.insert({"state": "REFERENCE_OK"})

        resp = await self.client.get("/api/stats")

        self.assertEqual(resp.status_code, 200)
        body = resp.json()
        for key in (
            "record_count",
            "oldest_ts",
            "newest_ts",
            "db_size_bytes",
            "db_size_mb",
            "max_size_mb",
            "usage_pct",
        ):
            self.assertIn(key, body, f"response missing key: {key!r}")
        self.assertEqual(body["record_count"], 1)
        self.assertGreater(body["db_size_bytes"], 0)
        self.assertGreaterEqual(body["usage_pct"], 0.0)
        self.assertIsNotNone(body["oldest_ts"])
        self.assertIsNotNone(body["newest_ts"])

    # ── /api/records/since ────────────────────────────────────────────────────

    async def test_since_empty_on_fresh_database(self) -> None:
        resp = await self.client.get("/api/records/since?cursor=0")

        self.assertEqual(resp.status_code, 200)
        body = resp.json()
        self.assertEqual(body["records"], [])
        self.assertEqual(body["count"], 0)
        self.assertEqual(body["next_cursor"], 0)

    async def test_since_cursor_pagination(self) -> None:
        for i in range(5):
            await database.insert({"state": f"state-{i}"})

        # First page: ask for 3 of 5.
        first = (await self.client.get("/api/records/since?cursor=0&limit=3")).json()
        self.assertEqual(first["count"], 3)
        self.assertEqual(first["next_cursor"], 3)
        self.assertEqual([r["id"] for r in first["records"]], [1, 2, 3])

        # Second page: resume from next_cursor, get remaining 2.
        second = (
            await self.client.get(f"/api/records/since?cursor={first['next_cursor']}&limit=10")
        ).json()
        self.assertEqual(second["count"], 2)
        self.assertEqual(second["next_cursor"], 5)
        self.assertEqual([r["id"] for r in second["records"]], [4, 5])

        # Idempotent: calling again with the final cursor returns nothing.
        third = (await self.client.get("/api/records/since?cursor=5&limit=10")).json()
        self.assertEqual(third["count"], 0)
        self.assertEqual(third["next_cursor"], 5)

    async def test_since_rejects_negative_cursor(self) -> None:
        resp = await self.client.get("/api/records/since?cursor=-1")
        self.assertEqual(resp.status_code, 422)

    async def test_since_rejects_zero_limit(self) -> None:
        resp = await self.client.get("/api/records/since?cursor=0&limit=0")
        self.assertEqual(resp.status_code, 422)

    async def test_since_rejects_overlimit(self) -> None:
        resp = await self.client.get("/api/records/since?cursor=0&limit=99999")
        self.assertEqual(resp.status_code, 422)

    # ── /api/records/range ────────────────────────────────────────────────────

    async def test_range_filters_by_timestamp(self) -> None:
        ts_before = time_module.time()
        await database.insert({"state": "REFERENCE_OK"})
        ts_after = time_module.time()

        # Window that covers the record.
        resp = await self.client.get(
            f"/api/records/range?ts_from={ts_before - 1}&ts_to={ts_after + 1}"
        )
        self.assertEqual(resp.status_code, 200)
        body = resp.json()
        self.assertEqual(body["count"], 1)
        self.assertEqual(body["records"][0]["state"], "REFERENCE_OK")

        # Window that misses the record.
        resp_miss = await self.client.get(
            f"/api/records/range?ts_from={ts_before - 100}&ts_to={ts_before - 1}"
        )
        self.assertEqual(resp_miss.json()["count"], 0)

    async def test_range_rejects_reversed_window(self) -> None:
        resp = await self.client.get("/api/records/range?ts_from=1000&ts_to=100")
        self.assertEqual(resp.status_code, 422)

    async def test_range_ts_to_defaults_to_now(self) -> None:
        """ts_to is optional and defaults to the current time."""
        await database.insert({"state": "NO_FIX"})
        # Provide only ts_from; omit ts_to.
        resp = await self.client.get("/api/records/range?ts_from=0")
        self.assertEqual(resp.status_code, 200)
        self.assertGreaterEqual(resp.json()["count"], 1)

    # ── /api/upload ───────────────────────────────────────────────────────────

    async def test_upload_posts_to_webhook_and_returns_count(self) -> None:
        for sat in range(6, 9):
            await database.insert({"state": "REFERENCE_OK", "satellitesUsed": sat})

        mock_resp = MagicMock()
        mock_resp.status_code = 200
        mock_resp.raise_for_status.return_value = None
        mock_http = AsyncMock()
        mock_http.__aenter__ = AsyncMock(return_value=mock_http)
        mock_http.__aexit__ = AsyncMock(return_value=False)
        mock_http.post = AsyncMock(return_value=mock_resp)

        config.CLOUD_WEBHOOK = "https://example.com/webhook"
        with patch("httpx.AsyncClient", return_value=mock_http):
            resp = await self.client.post("/api/upload?since_cursor=0&limit=100")

        self.assertEqual(resp.status_code, 200)
        body = resp.json()
        self.assertEqual(body["uploaded"], 3)
        self.assertEqual(body["next_cursor"], 3)
        self.assertEqual(body["http_status"], 200)

        # Verify the payload sent to the webhook.
        mock_http.post.assert_awaited_once()
        call_kwargs = mock_http.post.call_args.kwargs
        payload = call_kwargs["json"]
        self.assertEqual(payload["source"], "gps-reference")
        self.assertEqual(len(payload["records"]), 3)

    async def test_upload_skips_when_no_new_records(self) -> None:
        """When the cursor is already past all records, upload returns immediately."""
        config.CLOUD_WEBHOOK = "https://example.com/webhook"
        # since_cursor=999 is beyond any record in the empty database.
        resp = await self.client.post("/api/upload?since_cursor=999")

        self.assertEqual(resp.status_code, 200)
        body = resp.json()
        self.assertEqual(body["uploaded"], 0)
        self.assertEqual(body["next_cursor"], 999)

    async def test_upload_returns_502_on_webhook_error(self) -> None:
        await database.insert({"state": "REFERENCE_OK"})

        mock_resp = MagicMock()
        mock_resp.status_code = 500
        mock_resp.raise_for_status.side_effect = httpx.HTTPStatusError(
            "Internal Server Error", request=MagicMock(), response=mock_resp
        )
        mock_http = AsyncMock()
        mock_http.__aenter__ = AsyncMock(return_value=mock_http)
        mock_http.__aexit__ = AsyncMock(return_value=False)
        mock_http.post = AsyncMock(return_value=mock_resp)

        config.CLOUD_WEBHOOK = "https://example.com/webhook"
        with patch("httpx.AsyncClient", return_value=mock_http):
            resp = await self.client.post("/api/upload?since_cursor=0")

        self.assertEqual(resp.status_code, 502)
        self.assertIn("500", resp.json()["detail"])  # detail contains the upstream status

    async def test_upload_incremental_cursor(self) -> None:
        """A second upload from next_cursor transfers only the new records."""
        for _ in range(4):
            await database.insert({"state": "REFERENCE_OK"})

        mock_resp = MagicMock()
        mock_resp.status_code = 200
        mock_resp.raise_for_status.return_value = None
        mock_http = AsyncMock()
        mock_http.__aenter__ = AsyncMock(return_value=mock_http)
        mock_http.__aexit__ = AsyncMock(return_value=False)
        mock_http.post = AsyncMock(return_value=mock_resp)

        config.CLOUD_WEBHOOK = "https://example.com/webhook"
        with patch("httpx.AsyncClient", return_value=mock_http):
            first_resp = await self.client.post("/api/upload?since_cursor=0&limit=2")
        self.assertEqual(first_resp.json()["uploaded"], 2)
        self.assertEqual(first_resp.json()["next_cursor"], 2)

        # Insert two more records, then upload from where we left off.
        for _ in range(2):
            await database.insert({"state": "NO_FIX"})

        mock_http.post.reset_mock()
        with patch("httpx.AsyncClient", return_value=mock_http):
            second_resp = await self.client.post("/api/upload?since_cursor=2&limit=100")
        body = second_resp.json()
        self.assertEqual(body["uploaded"], 4)  # records 3,4,5,6
        self.assertEqual(body["next_cursor"], 6)

    # ── /api/endpoints ────────────────────────────────────────────────────────

    async def test_endpoints_lists_all_paths(self) -> None:
        resp = await self.client.get("/api/endpoints")

        self.assertEqual(resp.status_code, 200)
        body = resp.json()
        self.assertIn("endpoints", body)
        self.assertIn("base_url", body)

        paths = {e["path"] for e in body["endpoints"]}
        for expected_path in (
            "/api/status",
            "/api/stats",
            "/api/records/since",
            "/api/records/range",
            "/api/stream",
            "/api/upload",
            "/api/endpoints",
            "/",
        ):
            self.assertIn(expected_path, paths, f"endpoint path missing: {expected_path!r}")

        # Each entry must have method, path, summary, and example fields.
        for entry in body["endpoints"]:
            for field in ("method", "path", "summary", "example"):
                self.assertIn(field, entry, f"endpoint entry missing field: {field!r}")

    # ── / (dashboard) ─────────────────────────────────────────────────────────

    async def test_dashboard_returns_html_page(self) -> None:
        resp = await self.client.get("/")

        self.assertEqual(resp.status_code, 200)
        self.assertIn("text/html", resp.headers.get("content-type", ""))
        self.assertIn("GPS Reference Module", resp.text)
        # SSE client must be present so the dashboard auto-updates.
        self.assertIn("EventSource", resp.text)
        # All GPS fields the dashboard is expected to display.
        for field_id in (
            "f-state",
            "f-fix",
            "f-sats",
            "f-lat",
            "f-lon",
            "f-alt",
            "f-hdop",
            "f-spd",
            "f-utc",
        ):
            self.assertIn(field_id, resp.text, f"dashboard missing element: {field_id!r}")


# ── API key authentication tests ──────────────────────────────────────────────


class ApiKeyTests(unittest.IsolatedAsyncioTestCase):
    """Verify the API key middleware protects write endpoints and leaves reads open."""

    async def asyncSetUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self._orig_db_path = config.DB_PATH
        self._orig_max_db_bytes = config.MAX_DB_BYTES
        self._orig_webhook = config.CLOUD_WEBHOOK
        self._orig_api_key = config.API_KEY
        config.DB_PATH = str(Path(self.temp_dir.name) / "positions.db")
        config.MAX_DB_BYTES = 1024**3
        config.CLOUD_WEBHOOK = ""
        config.API_KEY = "test-secret-key"
        await database.init()
        reader._set_state(_SAMPLE_STATE.copy())
        self.client = AsyncClient(
            transport=ASGITransport(app=api.app),
            base_url="http://test",
        )

    async def asyncTearDown(self) -> None:
        await self.client.aclose()
        await database.close()
        config.DB_PATH = self._orig_db_path
        config.MAX_DB_BYTES = self._orig_max_db_bytes
        config.CLOUD_WEBHOOK = self._orig_webhook
        config.API_KEY = self._orig_api_key
        reader._set_state({})
        self.temp_dir.cleanup()

    # ── Read endpoints remain open with API key enabled ──────────────────────

    async def test_status_open_without_token(self) -> None:
        resp = await self.client.get("/api/status")
        self.assertEqual(resp.status_code, 200)

    async def test_stats_open_without_token(self) -> None:
        resp = await self.client.get("/api/stats")
        self.assertEqual(resp.status_code, 200)

    async def test_records_since_open_without_token(self) -> None:
        resp = await self.client.get("/api/records/since?cursor=0")
        self.assertEqual(resp.status_code, 200)

    async def test_records_range_open_without_token(self) -> None:
        resp = await self.client.get("/api/records/range?ts_from=0")
        self.assertEqual(resp.status_code, 200)

    async def test_endpoints_open_without_token(self) -> None:
        resp = await self.client.get("/api/endpoints")
        self.assertEqual(resp.status_code, 200)

    async def test_dashboard_open_without_token(self) -> None:
        resp = await self.client.get("/")
        self.assertEqual(resp.status_code, 200)

    # ── Write endpoint requires valid token ──────────────────────────────────

    async def test_upload_rejected_without_token(self) -> None:
        config.CLOUD_WEBHOOK = "https://example.com/webhook"
        resp = await self.client.post("/api/upload?since_cursor=0")
        self.assertEqual(resp.status_code, 401)
        self.assertIn("API key", resp.json()["detail"])

    async def test_upload_rejected_with_wrong_token(self) -> None:
        config.CLOUD_WEBHOOK = "https://example.com/webhook"
        resp = await self.client.post(
            "/api/upload?since_cursor=0",
            headers={"Authorization": "Bearer wrong-key"},
        )
        self.assertEqual(resp.status_code, 401)

    async def test_upload_accepted_with_correct_token(self) -> None:
        config.CLOUD_WEBHOOK = "https://example.com/webhook"

        mock_resp = MagicMock()
        mock_resp.status_code = 200
        mock_resp.raise_for_status.return_value = None
        mock_http = AsyncMock()
        mock_http.__aenter__ = AsyncMock(return_value=mock_http)
        mock_http.__aexit__ = AsyncMock(return_value=False)
        mock_http.post = AsyncMock(return_value=mock_resp)

        await database.insert({"state": "REFERENCE_OK"})

        with patch("httpx.AsyncClient", return_value=mock_http):
            resp = await self.client.post(
                "/api/upload?since_cursor=0",
                headers={"Authorization": "Bearer test-secret-key"},
            )
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.json()["uploaded"], 1)

    # ── No API key configured = everything open ──────────────────────────────

    async def test_upload_open_when_api_key_not_set(self) -> None:
        config.API_KEY = ""
        config.CLOUD_WEBHOOK = "https://example.com/webhook"

        # No records to upload, but the point is it reaches the handler (200)
        # instead of being blocked (401).
        resp = await self.client.post("/api/upload?since_cursor=999")
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.json()["uploaded"], 0)


# ── Reporting ──────────────────────────────────────────────────────────────────


class ReportingResult(unittest.TextTestResult):
    def addSuccess(self, test: unittest.TestCase) -> None:
        super().addSuccess(test)
        name = test._testMethodName
        desc = TEST_DESCRIPTIONS.get(name, "")
        line = f"PASS\t{name.removeprefix('test_').replace('_', ' ').title()}"
        if desc:
            line += f"\t{desc}"
        self.stream.writeln(line)


if __name__ == "__main__":
    suite = unittest.TestSuite()
    suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(ServiceTests))
    suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(ApiHttpTests))
    suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(ApiKeyTests))
    runner = unittest.TextTestRunner(
        stream=sys.stdout,
        verbosity=0,
        resultclass=ReportingResult,  # type: ignore[arg-type]  # typeshed variance: _TextTestStream vs _WritelnDecorator
    )
    result = runner.run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
