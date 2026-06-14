"""Service configuration - all settings read from environment variables."""

import os

# Serial
SERIAL_PORT: str = os.getenv("GPS_SERIAL_PORT", "/dev/ttyUSB0")
BAUD_RATE: int = int(os.getenv("GPS_BAUD_RATE", "115200"))

# Storage
DB_PATH: str = os.getenv("GPS_DB_PATH", "/var/lib/gps-reference/data.db")
MAX_DB_BYTES: int = int(os.getenv("GPS_MAX_DB_BYTES", str(4 * 1024**3)))  # 4 GB default
CLEANUP_FRAC: float = 0.05  # fraction of rows to delete when the cap is approached

# HTTP server
HTTP_HOST: str = os.getenv("GPS_HTTP_HOST", "0.0.0.0")
HTTP_PORT: int = int(os.getenv("GPS_HTTP_PORT", "8000"))

# Cloud upload - optional POST target for /api/upload
CLOUD_WEBHOOK: str = os.getenv("GPS_CLOUD_WEBHOOK", "")


def validate() -> None:
    """Validate all configuration values at startup.

    Raises ValueError with a descriptive message on the first invalid setting
    so misconfigured deployments fail immediately rather than misbehaving at
    runtime.
    """
    if BAUD_RATE <= 0:
        raise ValueError(f"GPS_BAUD_RATE must be a positive integer, got {BAUD_RATE!r}")
    if not 1 <= HTTP_PORT <= 65535:
        raise ValueError(f"GPS_HTTP_PORT must be between 1 and 65535, got {HTTP_PORT!r}")
    if MAX_DB_BYTES < 1024 * 1024:
        raise ValueError(f"GPS_MAX_DB_BYTES must be at least 1 MiB (1048576), got {MAX_DB_BYTES!r}")
