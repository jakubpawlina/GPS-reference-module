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
import json
import time
from contextlib import asynccontextmanager
from typing import Optional

import httpx
from fastapi import FastAPI, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse, JSONResponse, StreamingResponse

import config
import database
import reader


# ── Lifespan ───────────────────────────────────────────────────────────────────

@asynccontextmanager
async def lifespan(app: FastAPI):
    await reader.start()
    yield
    await reader.stop()


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
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


# ── Live state ─────────────────────────────────────────────────────────────────

@app.get("/api/status", summary="Current GPS state", tags=["Live"])
async def get_status():
    """
    Returns the most recent `parsed_state` record received from the ESP32.
    HTTP 503 if the serial reader has not yet received any data.
    """
    if not reader.current_state:
        raise HTTPException(503, detail="No data received yet - is the ESP32 connected?")
    return reader.current_state


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
    async def _generate():
        last = None
        while True:
            state = reader.current_state
            if state and state != last:
                last = state
                yield f"data: {json.dumps(state)}\n\n"
            await asyncio.sleep(1)

    return StreamingResponse(_generate(), media_type="text/event-stream")


# ── Storage ────────────────────────────────────────────────────────────────────

@app.get("/api/stats", summary="Storage statistics", tags=["Storage"])
async def get_stats():
    """Record count, DB file size, oldest/newest timestamps, and usage percentage."""
    return await database.stats()


# ── Records ────────────────────────────────────────────────────────────────────

@app.get("/api/records/since", summary="Incremental poll (cursor-based)", tags=["Records"])
async def get_since(
    cursor: int = Query(0, description="Last record id received. Use 0 on first call; save `next_cursor` for subsequent calls."),
    limit:  int = Query(1000, le=10_000, description="Maximum records returned (max 10 000)."),
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


@app.get("/api/records/range", summary="Time-range query", tags=["Records"])
async def get_range(
    ts_from: float           = Query(...,  description="Start of window, Unix timestamp (seconds)."),
    ts_to:   Optional[float] = Query(None, description="End of window, Unix timestamp. Defaults to now."),
    limit:   int             = Query(10_000, le=50_000, description="Maximum records returned (max 50 000)."),
):
    """
    Returns all records stored within `[ts_from, ts_to]`, oldest-first.
    If the window exceeds `limit`, only the oldest `limit` rows are returned.
    """
    rows = await database.time_range(ts_from, ts_to or time.time(), limit)
    return {"records": rows, "count": len(rows)}


# ── Cloud upload ───────────────────────────────────────────────────────────────

@app.post("/api/upload", summary="Upload records to a cloud webhook", tags=["Cloud"])
async def upload(
    webhook_url:  str = Query(None, description="POST target URL. Falls back to GPS_CLOUD_WEBHOOK env var."),
    since_cursor: int = Query(0,    description="Upload only records with id > this value."),
    limit:        int = Query(10_000, le=50_000, description="Max records per batch (max 50 000)."),
):
    """
    POSTs `{"source": "gps-reference", "records": [...]}` to the webhook.
    Returns `next_cursor` for incremental uploads.

    **Response:** `{"uploaded": N, "next_cursor": M, "http_status": 200}`
    """
    url = webhook_url or config.CLOUD_WEBHOOK
    if not url:
        raise HTTPException(400, detail="No webhook URL - pass ?webhook_url= or set GPS_CLOUD_WEBHOOK")

    rows = await database.since(since_cursor, limit)
    if not rows:
        return {"uploaded": 0, "next_cursor": since_cursor}

    async with httpx.AsyncClient(timeout=30) as client:
        try:
            resp = await client.post(url, json={"source": "gps-reference", "records": rows})
            resp.raise_for_status()
        except httpx.HTTPError as exc:
            raise HTTPException(502, detail=f"Webhook error: {exc}")

    return {"uploaded": len(rows), "next_cursor": rows[-1]["id"], "http_status": resp.status_code}


# ── Meta ───────────────────────────────────────────────────────────────────────

@app.get("/api/endpoints", summary="Machine-readable endpoint catalogue", tags=["Meta"])
async def endpoints():
    """Structured list of all endpoints with descriptions and example URLs."""
    base = f"http://{config.HTTP_HOST}:{config.HTTP_PORT}"
    return JSONResponse({"base_url": base, "endpoints": [
        {"method": "GET",  "path": "/api/status",          "summary": "Current GPS state",                   "example": f"{base}/api/status"},
        {"method": "GET",  "path": "/api/stats",            "summary": "Storage statistics",                  "example": f"{base}/api/stats"},
        {"method": "GET",  "path": "/api/records/since",    "summary": "Incremental poll",                    "example": f"{base}/api/records/since?cursor=0"},
        {"method": "GET",  "path": "/api/records/range",    "summary": "Time-range query",                    "example": f"{base}/api/records/range?ts_from=1748000000"},
        {"method": "GET",  "path": "/api/stream",           "summary": "SSE live stream",                     "example": f"{base}/api/stream"},
        {"method": "POST", "path": "/api/upload",           "summary": "Upload to cloud webhook",             "example": f"{base}/api/upload?webhook_url=https://example.com/ingest"},
        {"method": "GET",  "path": "/api/endpoints",        "summary": "This endpoint catalogue",             "example": f"{base}/api/endpoints"},
        {"method": "GET",  "path": "/",                     "summary": "Live browser dashboard",              "example": f"{base}/"},
        {"method": "GET",  "path": "/docs",                 "summary": "Swagger UI",                          "example": f"{base}/docs"},
        {"method": "GET",  "path": "/redoc",                "summary": "ReDoc",                               "example": f"{base}/redoc"},
    ]})


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


@app.get("/", response_class=HTMLResponse, include_in_schema=False)
async def dashboard():
    return _DASHBOARD
