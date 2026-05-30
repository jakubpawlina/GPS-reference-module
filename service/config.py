"""Service configuration - all settings read from environment variables."""

import os

# Serial
SERIAL_PORT: str = os.getenv("GPS_SERIAL_PORT", "/dev/ttyUSB0")
BAUD_RATE:   int = int(os.getenv("GPS_BAUD_RATE", "115200"))

# Storage
DB_PATH:       str   = os.getenv("GPS_DB_PATH", "/var/lib/gps-reference/data.db")
MAX_DB_BYTES:  int   = int(os.getenv("GPS_MAX_DB_BYTES", str(4 * 1024 ** 3)))  # 4 GB default
CLEANUP_FRAC:  float = 0.05  # fraction of rows to delete when the cap is approached

# HTTP server
HTTP_HOST: str = os.getenv("GPS_HTTP_HOST", "0.0.0.0")
HTTP_PORT: int = int(os.getenv("GPS_HTTP_PORT", "8000"))

# Cloud upload - optional POST target for /api/upload
CLOUD_WEBHOOK: str = os.getenv("GPS_CLOUD_WEBHOOK", "")
