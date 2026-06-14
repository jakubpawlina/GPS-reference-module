"""Service configuration - all settings read from environment variables."""

import os


def _int_env(name: str, default: int) -> int:
    """Read an integer environment variable with a descriptive error on bad input."""
    raw = os.getenv(name)
    if raw is None:
        return default
    try:
        return int(raw)
    except ValueError:
        raise ValueError(f"Environment variable {name}={raw!r} is not a valid integer") from None


def _float_env(name: str, default: float) -> float:
    """Read a float environment variable with a descriptive error on bad input."""
    raw = os.getenv(name)
    if raw is None:
        return default
    try:
        return float(raw)
    except ValueError:
        raise ValueError(f"Environment variable {name}={raw!r} is not a valid number") from None


# Serial
SERIAL_PORT: str = os.getenv("GPS_SERIAL_PORT", "/dev/ttyUSB0")
BAUD_RATE: int = _int_env("GPS_BAUD_RATE", 115200)
SERIAL_MAX_LINE_BYTES: int = _int_env("GPS_SERIAL_MAX_LINE_BYTES", 4096)
STATE_STALE_SECONDS: float = _float_env("GPS_STATE_STALE_SECONDS", 3.0)

# Storage
DB_PATH: str = os.getenv("GPS_DB_PATH", "/var/lib/gps-reference/data.db")
MAX_DB_BYTES: int = _int_env("GPS_MAX_DB_BYTES", 4 * 1024**3)  # 4 GB default
CLEANUP_FRAC: float = 0.05  # fraction of rows to delete when the cap is approached

# HTTP server
HTTP_HOST: str = os.getenv("GPS_HTTP_HOST", "0.0.0.0")
HTTP_PORT: int = _int_env("GPS_HTTP_PORT", 8000)
MAX_SSE_CONNECTIONS: int = _int_env("GPS_MAX_SSE_CONNECTIONS", 32)

# CORS — comma-separated list of allowed origins, or "*" to allow all
CORS_ORIGINS: list[str] = [
    o.strip() for o in os.getenv("GPS_CORS_ORIGINS", "*").split(",") if o.strip()
]

# Authentication — optional Bearer token for write endpoints.  When set,
# mutating operations (POST /api/upload) require an
# ``Authorization: Bearer <token>`` header.  Read-only endpoints remain open.
API_KEY: str = os.getenv("GPS_API_KEY", "")

# Cloud upload - optional POST target for /api/upload
CLOUD_WEBHOOK: str = os.getenv("GPS_CLOUD_WEBHOOK", "")


def validate() -> None:
    """Validate all configuration values at startup.

    Raises ValueError with a descriptive message on the first invalid setting
    so misconfigured deployments fail immediately rather than misbehaving at
    runtime.
    """
    if not SERIAL_PORT:
        raise ValueError("GPS_SERIAL_PORT must not be empty")
    if BAUD_RATE <= 0:
        raise ValueError(f"GPS_BAUD_RATE must be a positive integer, got {BAUD_RATE!r}")
    if SERIAL_MAX_LINE_BYTES < 256:
        raise ValueError(
            f"GPS_SERIAL_MAX_LINE_BYTES must be at least 256, got {SERIAL_MAX_LINE_BYTES!r}"
        )
    if STATE_STALE_SECONDS <= 0:
        raise ValueError(
            f"GPS_STATE_STALE_SECONDS must be greater than zero, got {STATE_STALE_SECONDS!r}"
        )
    if not 1 <= HTTP_PORT <= 65535:
        raise ValueError(f"GPS_HTTP_PORT must be between 1 and 65535, got {HTTP_PORT!r}")
    if MAX_SSE_CONNECTIONS <= 0:
        raise ValueError(
            f"GPS_MAX_SSE_CONNECTIONS must be a positive integer, got {MAX_SSE_CONNECTIONS!r}"
        )
    if MAX_DB_BYTES < 1024 * 1024:
        raise ValueError(f"GPS_MAX_DB_BYTES must be at least 1 MiB (1048576), got {MAX_DB_BYTES!r}")
