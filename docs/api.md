# API Reference

Base URL: `http://<rpi-ip>:8000`

Interactive documentation is also available at:
- `/docs` - Swagger UI (try endpoints in the browser)
- `/redoc` - ReDoc (clean readable reference)

---

## GET /api/status

Returns the latest GPS state received from the ESP32.

**Response 200:**

```json
{
  "type": "parsed_state",
  "millis": 452833,
  "state": "REFERENCE_OK",
  "valid": true,
  "gpsData": true,
  "displayReady": true,
  "fix": true,
  "fixType": "3D",
  "fixQuality": 2,
  "satellitesUsed": 12,
  "latitude": 50.026652,
  "longitude": 19.953602,
  "altitudeM": 263.1,
  "hdop": 0.8,
  "pdop": 1.6,
  "vdop": 1.4,
  "speedKnots": 0.02,
  "speedKmh": 0.04,
  "courseDeg": 54.7,
  "utcTime": "180810.00",
  "utcDate": "250526",
  "nmeaAgeMs": 80,
  "ggaAgeMs": 80,
  "gsaAgeMs": 120,
  "rmcAgeMs": 90,
  "rawSentenceCount": 617,
  "acceptedSentenceCount": 494,
  "checksumErrorCount": 0,
  "bufferOverflowCount": 0
}
```

**Response 503** - serial port not connected, no data received yet, or the last
state is older than `GPS_STATE_STALE_SECONDS`.

---

## GET /api/stats

Returns storage statistics.

**Response 200:**

```json
{
  "record_count": 86400,
  "oldest_ts": 1748000000.0,
  "newest_ts": 1748086400.0,
  "db_size_bytes": 52428800,
  "db_size_mb": 50.0,
  "max_size_mb": 4096.0,
  "usage_pct": 1.22
}
```

When `usage_pct` reaches 95 %, the service automatically deletes the oldest
5 % of rows to stay within the 4 GB cap.

---

## GET /api/records/since

Cursor-based incremental polling. Returns records newer than `cursor`, oldest first.

**Query parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `cursor` | int | 0 | ID of the last record already received. Use `0` to start from the beginning. |
| `limit` | int | 1000 | Maximum records returned. Max 10 000. |

**Response 200:**

```json
{
  "records": [ { ...position record... } ],
  "count": 42,
  "next_cursor": 1234
}
```

**Usage pattern:**

```
cursor = 0
loop every N seconds:
    GET /api/records/since?cursor={cursor}&limit=1000
    process response.records
    cursor = response.next_cursor
```

The endpoint is stateless - the cursor is maintained by the client.
An empty `records` array means no new data since the last call; `next_cursor`
is unchanged in that case.

---

## GET /api/records/range

Returns records within a time window ordered oldest-first.

**Query parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `ts_from` | float | yes | Start of window, Unix timestamp (seconds) |
| `ts_to` | float | no | End of window, Unix timestamp. Defaults to now. |
| `limit` | int | no | Max records returned. Default 10 000, max 50 000. |

**Example - last hour:**

```
GET /api/records/range?ts_from=1748082800
```

**Example - specific window:**

```
GET /api/records/range?ts_from=1748000000&ts_to=1748003600
```

**Response 200:**

```json
{
  "records": [ { ...position record... } ],
  "count": 3600
}
```

If the window contains more than `limit` records, only the oldest `limit` rows
are returned. Slide `ts_from` to page through larger ranges.

**Tip:** convert a human-readable date to a Unix timestamp:

```bash
date -d "2026-05-25 12:00:00" +%s
```

---

## GET /api/stream

Server-Sent Events (SSE) stream. Emits one JSON event per second whenever
the GPS state changes. The payload is identical to `/api/status`.

**Browser:**

```js
const es = new EventSource('http://<rpi-ip>:8000/api/stream');
es.onmessage = e => console.log(JSON.parse(e.data));
```

**curl:**

```bash
curl -N http://<rpi-ip>:8000/api/stream
```

The connection stays open indefinitely. Browsers using `EventSource` reconnect
automatically on interruption.

If the latest state expires, the stream emits one synthetic `NO_GPS_DATA`
event with `"serviceStale": true` so dashboards stop presenting the last fix as
current.

---

## POST /api/upload

Uploads a batch of records to a cloud webhook via HTTP POST.

**Query parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `since_cursor` | int | no | Upload only records with id > this value. Default 0. |
| `limit` | int | no | Max records per batch. Default 10 000, max 50 000. |

**Body sent to the webhook:**

```json
{
  "source": "gps-reference",
  "records": [ { ...position record... } ]
}
```

**Response 200:**

```json
{
  "uploaded": 3600,
  "next_cursor": 5678,
  "http_status": 200
}
```

**Response 400** - no webhook URL configured.
**Response 502** - webhook request failed.

**Incremental upload pattern:**

```
cursor = 0
loop:
    POST /api/upload?since_cursor={cursor}
    cursor = response.next_cursor
```

---

## GET /api/endpoints

Machine-readable JSON description of all endpoints. Useful for clients that
need to discover the API programmatically.

---

## GET /

Live browser dashboard. Shows current GPS state updated in real time via SSE,
plus a legend of all possible states. No navigation links - use direct URLs
to reach `/docs` and `/redoc`.

<p align="center">
  <img src="../docs/images/service/dashboard-reference-ok.png"
       alt="Browser dashboard showing REFERENCE_OK state with coordinates, altitude, HDOP, and UTC time"
       width="800">
</p>

---

## Position record fields

Every record returned by `/api/records/since` and `/api/records/range` contains:

| Field | Type | Description |
|-------|------|-------------|
| `id` | int | Monotonically increasing row ID - use as cursor |
| `ts` | float | Unix timestamp when stored on the RPi |
| `state` | string | `REFERENCE_OK` / `DEGRADED_LOW_SAT` / `DEGRADED_2D` / `NO_FIX` / `NO_GPS_DATA` |
| `valid` | int (0/1) | 1 when a usable position is available |
| `fix` | int (0/1) | 1 when GPS has a fix |
| `fix_type` | string | `3D` / `2D` / `NONE` / `UNKNOWN` |
| `satellites` | int | Number of satellites used in the solution |
| `lat` | float | Latitude, decimal degrees WGS-84 (null if no fix) |
| `lon` | float | Longitude, decimal degrees WGS-84 (null if no fix) |
| `alt_m` | float | Altitude above MSL in metres (null if no 3D fix) |
| `hdop` | float | Horizontal dilution of precision (null if unavailable) |
| `pdop` | float | Position DOP |
| `vdop` | float | Vertical DOP |
| `speed_kmh` | float | Ground speed km/h |
| `utc_time` | string | UTC time `HHMMSS.ss` |
| `utc_date` | string | UTC date `DDMMYY` |
| `raw_json` | string | Complete original `parsed_state` JSON from the ESP32 |
