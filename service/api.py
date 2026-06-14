"""FastAPI application - REST/SSE interface and browser dashboard.

Endpoints
---------
GET  /                          Browser dashboard (SSE live view + API reference)
GET  /api/status                Latest GPS state (live, from memory)
GET  /api/stats                 Storage statistics
GET  /api/records/since         Cursor-based incremental polling
GET  /api/records/range         Time-range historical query
GET  /api/stream                Server-Sent Events live stream
POST /api/upload                Batch upload to a cloud webhook
GET  /api/endpoints             Machine-readable endpoint catalogue
GET  /docs                      Swagger UI (auto-generated)
GET  /redoc                     ReDoc (auto-generated)
"""

from __future__ import annotations

import asyncio
import hashlib
import hmac
import ipaddress
import json
import logging
import socket
import time
from collections.abc import AsyncGenerator
from contextlib import asynccontextmanager
from urllib.parse import urlparse

import config
import database
import httpx
import reader
from fastapi import FastAPI, HTTPException, Query, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse, JSONResponse, StreamingResponse
from pydantic import BaseModel, Field

log = logging.getLogger("api")

SSE_KEEPALIVE_SECONDS = 15.0
SSE_POLL_INTERVAL_SECONDS = 1.0

# ── Response models ───────────────────────────────────────────────────────────


class GpsStatus(BaseModel):
    """Current GPS state as received from the ESP32."""

    type: str = Field("parsed_state", examples=["parsed_state"])
    millis: int | None = Field(None, examples=[123456])
    state: str | None = Field(None, examples=["REFERENCE_OK"])
    valid: bool | None = Field(None, examples=[True])
    gpsData: bool | None = Field(None, examples=[True])
    displayReady: bool | None = Field(None, examples=[True])
    fix: bool | None = Field(None, examples=[True])
    fixType: str | None = Field(None, examples=["3D"])
    fixQuality: int | None = Field(None, examples=[1])
    satellitesUsed: int | None = Field(None, examples=[8])
    latitude: float | None = Field(None, examples=[50.026651])
    longitude: float | None = Field(None, examples=[19.953602])
    altitudeM: float | None = Field(None, examples=[263.0])
    hdop: float | None = Field(None, examples=[0.8])
    pdop: float | None = Field(None, examples=[1.2])
    vdop: float | None = Field(None, examples=[0.9])
    speedKnots: float | None = Field(None, examples=[0.05])
    speedKmh: float | None = Field(None, examples=[0.09])
    courseDeg: float | None = Field(None, examples=[123.4])
    utcTime: str | None = Field(None, examples=["120530.00"])
    utcDate: str | None = Field(None, examples=["140626"])
    nmeaAgeMs: int | None = Field(None, examples=[80])
    ggaAgeMs: int | None = Field(None, examples=[80])
    gsaAgeMs: int | None = Field(None, examples=[120])
    rmcAgeMs: int | None = Field(None, examples=[90])
    rawSentenceCount: int | None = Field(None, examples=[617])
    acceptedSentenceCount: int | None = Field(None, examples=[494])
    checksumErrorCount: int | None = Field(None, examples=[0])
    bufferOverflowCount: int | None = Field(None, examples=[0])

    model_config = {"extra": "allow"}


class StorageStats(BaseModel):
    """Database storage statistics."""

    record_count: int = Field(..., examples=[8640])
    oldest_ts: float | None = Field(None, examples=[1750000000.0])
    newest_ts: float | None = Field(None, examples=[1750086400.0])
    db_size_bytes: int = Field(..., examples=[4194304])
    db_size_mb: float = Field(..., examples=[4.0])
    max_size_mb: float = Field(..., examples=[4096.0])
    usage_pct: float = Field(..., examples=[0.1])


class RecordsSinceResponse(BaseModel):
    """Cursor-based polling response."""

    records: list[dict] = Field(..., description="List of position records.")
    count: int = Field(..., examples=[100])
    next_cursor: int = Field(..., examples=[100])


class RecordsRangeResponse(BaseModel):
    """Time-range query response."""

    records: list[dict] = Field(..., description="List of position records.")
    count: int = Field(..., examples=[100])


class UploadResponse(BaseModel):
    """Cloud upload result."""

    uploaded: int = Field(..., examples=[100])
    next_cursor: int = Field(..., examples=[100])
    http_status: int | None = Field(None, examples=[200])


# ── Lifespan ───────────────────────────────────────────────────────────────────


@asynccontextmanager
async def lifespan(_app: FastAPI) -> AsyncGenerator[None, None]:
    await database.init()
    await reader.start()
    try:
        yield
    finally:
        try:
            await reader.stop()
        except asyncio.CancelledError:
            reader.request_stop()
            try:
                await asyncio.sleep(1.0)
            except asyncio.CancelledError:
                pass
        await database.close()


# ── Application ────────────────────────────────────────────────────────────────

app = FastAPI(
    title="GPS Reference API",
    version="1.0.0",
    description=(
        "Serial → SQLite → REST/SSE bridge for the GPS reference module.\n\n"
        "**Quick links:** [Dashboard](/) · [Swagger UI](/docs) · [ReDoc](/redoc)"
    ),
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=config.CORS_ORIGINS,
    allow_methods=["*"],
    allow_headers=["*"],
)


# ── Optional API key authentication ──────────────────────────────────────────

_WRITE_PATHS = ("/api/upload",)
_active_sse_connections = 0


def _valid_bearer_token(authorization: str) -> bool:
    """Compare bearer tokens using fixed-length digests."""
    scheme, separator, token = authorization.partition(" ")
    if separator != " " or scheme.lower() != "bearer" or not token:
        return False

    supplied = hashlib.sha256(token.encode("utf-8")).digest()
    expected = hashlib.sha256(config.API_KEY.encode("utf-8")).digest()
    return hmac.compare_digest(supplied, expected)


@app.middleware("http")
async def _api_key_middleware(request: Request, call_next):
    """Require a Bearer token on write endpoints when GPS_API_KEY is configured.

    Read-only endpoints (status, stream, records, stats) remain open so the
    dashboard and passive consumers work without credentials.  Only mutating
    operations (upload) are gated.
    """
    if not config.API_KEY:
        return await call_next(request)

    path = request.url.path
    if not any(path.startswith(p) for p in _WRITE_PATHS):
        return await call_next(request)

    if request.method in ("GET", "HEAD", "OPTIONS"):
        return await call_next(request)

    auth = request.headers.get("authorization", "")
    if _valid_bearer_token(auth):
        return await call_next(request)

    return JSONResponse(status_code=401, content={"detail": "Invalid or missing API key"})


# ── Live state ─────────────────────────────────────────────────────────────────


@app.get(
    "/api/status",
    summary="Current GPS state",
    tags=["Live"],
    response_model=GpsStatus,
    responses={503: {"description": "No data received yet - is the ESP32 connected?"}},
)
async def get_status():
    """
    Returns the most recent `parsed_state` record received from the ESP32.
    HTTP 503 if the serial reader has not yet received any data.
    """
    state = reader.get_state()
    if not state:
        raise HTTPException(503, detail="No data received yet - is the ESP32 connected?")
    return state


@app.get(
    "/api/stream",
    summary="Server-Sent Events live stream",
    tags=["Live"],
    response_class=StreamingResponse,
)
async def stream():
    """
    Pushes one JSON event per second when the GPS state changes.

    **Browser:** `new EventSource('/api/stream')`
    **curl:** `curl -N http://<host>:8000/api/stream`
    """

    global _active_sse_connections
    if _active_sse_connections >= config.MAX_SSE_CONNECTIONS:
        raise HTTPException(503, detail="Too many active SSE connections")
    _active_sse_connections += 1

    async def _generate():
        global _active_sse_connections
        last = None
        last_keepalive = asyncio.get_running_loop().time()
        try:
            while True:
                state = reader.get_state()
                now = asyncio.get_running_loop().time()
                # Identity check: _set_state() replaces the dict reference on
                # every update, so `is not` detects new data without deep comparison.
                if state and state is not last:
                    last = state
                    yield f"data: {json.dumps(state)}\n\n"
                    last_keepalive = now
                elif now - last_keepalive >= SSE_KEEPALIVE_SECONDS:
                    # SSE comment - keeps the connection alive through proxies that
                    # close idle connections after ~60 s of silence.
                    yield ": keepalive\n\n"
                    last_keepalive = now
                await asyncio.sleep(SSE_POLL_INTERVAL_SECONDS)
        except asyncio.CancelledError:
            return
        finally:
            _active_sse_connections -= 1

    return StreamingResponse(
        _generate(),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "X-Accel-Buffering": "no",
        },
    )


# ── Storage ────────────────────────────────────────────────────────────────────


@app.get("/api/stats", summary="Storage statistics", tags=["Storage"], response_model=StorageStats)
async def get_stats():
    """Record count, DB file size, oldest/newest timestamps, and usage percentage."""
    return await database.stats()


# ── Records ────────────────────────────────────────────────────────────────────


@app.get(
    "/api/records/since",
    summary="Incremental poll (cursor-based)",
    tags=["Records"],
    response_model=RecordsSinceResponse,
)
async def get_since(
    cursor: int = Query(
        0,
        ge=0,
        description="Last record id received. Use 0 on first call; save `next_cursor` for subsequent calls.",
    ),
    limit: int = Query(1000, ge=1, le=10_000, description="Maximum records returned (max 10 000)."),
):
    """
    Stateless cursor-based polling. The server holds no session; the cursor is
    maintained by the client.

    ```
    cursor = 0
    while True:
        r = GET /api/records/since?cursor={cursor}
        process(r.records)
        cursor = r.next_cursor
    ```
    """
    rows = await database.since(cursor, limit)
    next_cursor = rows[-1]["id"] if rows else cursor
    return {"records": rows, "count": len(rows), "next_cursor": next_cursor}


@app.get(
    "/api/records/range",
    summary="Time-range query",
    tags=["Records"],
    response_model=RecordsRangeResponse,
    responses={422: {"description": "ts_to must be greater than or equal to ts_from"}},
)
async def get_range(
    ts_from: float = Query(..., ge=0, description="Start of window, Unix timestamp (seconds)."),
    ts_to: float | None = Query(
        None, description="End of window, Unix timestamp. Defaults to now."
    ),
    limit: int = Query(
        10_000, ge=1, le=50_000, description="Maximum records returned (max 50 000)."
    ),
):
    """
    Returns all records stored within `[ts_from, ts_to]`, oldest-first.
    If the window exceeds `limit`, only the oldest `limit` rows are returned.
    """
    effective_ts_to = time.time() if ts_to is None else ts_to
    if effective_ts_to < ts_from:
        raise HTTPException(422, detail="ts_to must be greater than or equal to ts_from")

    rows = await database.time_range(ts_from, effective_ts_to, limit)
    return {"records": rows, "count": len(rows)}


# ── Cloud upload ───────────────────────────────────────────────────────────────


def _is_public_url(url: str) -> bool:
    """Return True if the webhook hostname resolves to a public (non-private) address."""
    hostname = urlparse(url).hostname
    if not hostname:
        return False
    try:
        addr_info = socket.getaddrinfo(hostname, None, proto=socket.IPPROTO_TCP)
    except socket.gaierror:
        return False
    for _family, _type, _proto, _canonname, sockaddr in addr_info:
        ip = ipaddress.ip_address(sockaddr[0])
        if ip.is_private or ip.is_loopback or ip.is_link_local or ip.is_reserved:
            return False
    return True


@app.post(
    "/api/upload",
    summary="Upload records to a cloud webhook",
    tags=["Cloud"],
    response_model=UploadResponse,
    responses={
        400: {
            "description": "No webhook configured, invalid URL, or URL resolves to a private address"
        },
        401: {"description": "Missing or invalid API key (when GPS_API_KEY is set)"},
        502: {"description": "Webhook connection error or non-2xx response from the remote server"},
    },
)
async def upload(
    since_cursor: int = Query(0, ge=0, description="Upload only records with id > this value."),
    limit: int = Query(10_000, ge=1, le=50_000, description="Max records per batch (max 50 000)."),
):
    """
    POSTs `{"source": "gps-reference", "records": [...]}` to the webhook.
    Returns `next_cursor` for incremental uploads.

    **Response:** `{"uploaded": N, "next_cursor": M, "http_status": 200}`
    """
    url = config.CLOUD_WEBHOOK
    if not url:
        raise HTTPException(400, detail="No webhook URL configured in GPS_CLOUD_WEBHOOK")
    parsed_url = urlparse(url)
    if parsed_url.scheme not in {"http", "https"} or not parsed_url.netloc:
        raise HTTPException(400, detail="Webhook URL must be an absolute HTTP or HTTPS URL")
    if not _is_public_url(url):
        raise HTTPException(
            400, detail="Webhook URL must not resolve to a private or loopback address"
        )

    rows = await database.since(since_cursor, limit)
    if not rows:
        return {"uploaded": 0, "next_cursor": since_cursor}

    # Use separate connect/read timeouts so a slow webhook body read does not
    # stall the event loop longer than the combined 35-second ceiling.
    timeout = httpx.Timeout(connect=5.0, read=30.0, write=10.0, pool=5.0)
    try:
        async with httpx.AsyncClient(timeout=timeout) as client:
            resp = await client.post(url, json={"source": "gps-reference", "records": rows})
    except httpx.RequestError as exc:
        log.warning("Webhook connection error: %s", exc)
        raise HTTPException(502, detail="Webhook connection error") from exc

    try:
        resp.raise_for_status()
    except httpx.HTTPStatusError as exc:
        raise HTTPException(502, detail=f"Webhook returned {resp.status_code}") from exc

    return {"uploaded": len(rows), "next_cursor": rows[-1]["id"], "http_status": resp.status_code}


# ── Meta ───────────────────────────────────────────────────────────────────────


@app.get("/api/endpoints", summary="Machine-readable endpoint catalogue", tags=["Meta"])
async def endpoints(request: Request):
    """Structured list of all endpoints with descriptions and example URLs."""
    base = str(request.base_url).rstrip("/")
    return JSONResponse(
        {
            "base_url": base,
            "endpoints": [
                {
                    "method": "GET",
                    "path": "/api/status",
                    "summary": "Current GPS state",
                    "example": f"{base}/api/status",
                },
                {
                    "method": "GET",
                    "path": "/api/stats",
                    "summary": "Storage statistics",
                    "example": f"{base}/api/stats",
                },
                {
                    "method": "GET",
                    "path": "/api/records/since",
                    "summary": "Incremental poll",
                    "example": f"{base}/api/records/since?cursor=0",
                },
                {
                    "method": "GET",
                    "path": "/api/records/range",
                    "summary": "Time-range query",
                    "example": f"{base}/api/records/range?ts_from=1748000000",
                },
                {
                    "method": "GET",
                    "path": "/api/stream",
                    "summary": "SSE live stream",
                    "example": f"{base}/api/stream",
                },
                {
                    "method": "POST",
                    "path": "/api/upload",
                    "summary": "Upload to configured cloud webhook",
                    "example": f"{base}/api/upload",
                },
                {
                    "method": "GET",
                    "path": "/api/endpoints",
                    "summary": "This endpoint catalogue",
                    "example": f"{base}/api/endpoints",
                },
                {
                    "method": "GET",
                    "path": "/",
                    "summary": "Live browser dashboard",
                    "example": f"{base}/",
                },
                {
                    "method": "GET",
                    "path": "/docs",
                    "summary": "Swagger UI",
                    "example": f"{base}/docs",
                },
                {"method": "GET", "path": "/redoc", "summary": "ReDoc", "example": f"{base}/redoc"},
            ],
        }
    )


# ── Dashboard ──────────────────────────────────────────────────────────────────

_DASHBOARD = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>GPS Reference</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: monospace; background: #0d0d0d; color: #ccc;
         display: flex; flex-direction: column; align-items: center;
         min-height: 100vh; padding: 2rem 1rem; }
  h1 { color: #0ff; font-size: 1.3rem; letter-spacing: .1em;
       text-transform: uppercase; margin-bottom: 1.5rem; }
  #data { width: 100%; max-width: 480px; }
  table { border-collapse: collapse; width: 100%; }
  td, th { padding: .45rem .8rem; border: 1px solid #222; }
  th { color: #0ff; text-align: left; width: 38%; background: #111; }
  td { font-size: 1rem; }
  .ok   { color: #0f0; }
  .warn { color: #ff0; }
  .err  { color: #f44; }
  #states { width: 100%; max-width: 480px; margin-top: 1.8rem; }
  #states h2 { color: #555; font-size: .75rem; text-transform: uppercase;
                letter-spacing: .08em; margin-bottom: .5rem; }
  #states table td, #states table th { font-size: .8rem; padding: .3rem .6rem; }
  #states table th { width: 42%; }
  .dot { display: inline-block; width: .6rem; height: .6rem;
         border-radius: 50%; margin-right: .4rem; vertical-align: middle; }
  .dot.ok   { background: #0f0; }
  .dot.warn { background: #ff0; }
  .dot.err  { background: #f44; }
  #msg { color: #555; font-size: .85rem; margin-top: 1rem; }
</style>
</head>
<body>
<h1>GPS Reference Module</h1>

<div id="data">
  <table>
    <tr><th>STATE</th>      <td id="f-state">-</td></tr>
    <tr><th>FIX</th>        <td id="f-fix">-</td></tr>
    <tr><th>SATELLITES</th> <td id="f-sats">-</td></tr>
    <tr><th>LATITUDE</th>   <td id="f-lat">-</td></tr>
    <tr><th>LONGITUDE</th>  <td id="f-lon">-</td></tr>
    <tr><th>ALTITUDE</th>   <td id="f-alt">-</td></tr>
    <tr><th>HDOP</th>       <td id="f-hdop">-</td></tr>
    <tr><th>SPEED</th>      <td id="f-spd">-</td></tr>
    <tr><th>UTC</th>        <td id="f-utc">-</td></tr>
  </table>
</div>

<div id="states">
  <h2>States</h2>
  <table>
    <tr><th><span class="dot ok"></span>REFERENCE_OK</th>      <td>3D fix, ≥6 satellites, all data fresh</td></tr>
    <tr><th><span class="dot warn"></span>DEGRADED_LOW_SAT</th> <td>Fix acquired, fewer than 6 satellites or GGA stale</td></tr>
    <tr><th><span class="dot warn"></span>DEGRADED_2D</th>      <td>Fix acquired, 2D only (no altitude)</td></tr>
    <tr><th><span class="dot err"></span>NO_FIX</th>            <td>NMEA arriving but no position fix yet</td></tr>
    <tr><th><span class="dot err"></span>NO_GPS_DATA</th>       <td>No NMEA received from the GPS receiver</td></tr>
  </table>
</div>

<div id="msg">Connecting…</div>

<script>
const STATES = { REFERENCE_OK: 'ok', DEGRADED_LOW_SAT: 'warn', DEGRADED_2D: 'warn', NO_FIX: 'err', NO_GPS_DATA: 'err' };

function fmt(v, unit) { return v != null ? v + (unit || '') : '---'; }

function utc(date, time) {
  if (!date && !time) return '---';
  const t = time ? time.replace(/([0-9]{2})([0-9]{2})([0-9]{2}).*/, '$1:$2:$3') : '';
  const day = date ? `20${date.slice(4,6)}-${date.slice(2,4)}-${date.slice(0,2)} ` : '';
  return day + t + ' UTC';
}

const es = new EventSource('/api/stream');

es.onmessage = e => {
  const d = JSON.parse(e.data);
  const cls = STATES[d.state] || 'warn';
  document.getElementById('f-state').className = cls;
  document.getElementById('f-state').textContent = d.state ?? '---';
  document.getElementById('f-fix').textContent   = d.fixType ?? '---';
  document.getElementById('f-sats').textContent  = fmt(d.satellitesUsed);
  document.getElementById('f-lat').textContent   = fmt(d.latitude, '°');
  document.getElementById('f-lon').textContent   = fmt(d.longitude, '°');
  document.getElementById('f-alt').textContent   = fmt(d.altitudeM, ' m');
  document.getElementById('f-hdop').textContent  = fmt(d.hdop);
  document.getElementById('f-spd').textContent   = fmt(d.speedKmh, ' km/h');
  document.getElementById('f-utc').textContent   = utc(d.utcDate, d.utcTime);
  document.getElementById('msg').textContent = `Last update: ${new Date().toLocaleTimeString()}`;
};

es.onerror = () => { document.getElementById('msg').textContent = 'Stream disconnected - retrying…'; };
</script>
</body>
</html>"""


_DASHBOARD_CSP = (
    "default-src 'none'; "
    "style-src 'unsafe-inline'; "
    "script-src 'unsafe-inline'; "
    "connect-src 'self'; "
    "img-src 'none'; "
    "frame-ancestors 'none'"
)


@app.get("/", response_class=HTMLResponse, include_in_schema=False)
async def dashboard():
    return HTMLResponse(content=_DASHBOARD, headers={"Content-Security-Policy": _DASHBOARD_CSP})
