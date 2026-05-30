"""Entry point.

Shutdown sequence on SIGTERM:
  1. server.handle_exit() fires → _stop is set immediately via the patched handler
  2. Serial reader thread exits within ~1 s (readline timeout)
  3. uvicorn waits up to timeout_graceful_shutdown (3 s) for HTTP connections
  4. FastAPI lifespan shutdown calls reader.stop() (reader already done)
  5. Process exits cleanly - typically within 4-5 s, well under TimeoutStopSec=10
"""

import asyncio
import logging

import uvicorn

import config
import database
import reader
from api import app

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(name)-12s  %(levelname)s  %(message)s",
)
log = logging.getLogger("main")


async def _main() -> None:
    await database.init()

    cfg = uvicorn.Config(
        app,
        host=config.HTTP_HOST,
        port=config.HTTP_PORT,
        log_level="info",
        timeout_graceful_shutdown=3,
    )
    server = uvicorn.Server(cfg)

    # Patch uvicorn's signal handler so reader._stop is set the instant
    # SIGTERM/SIGINT arrives, before uvicorn starts waiting for connections.
    _orig = server.handle_exit

    def _handle_exit(sig: int, frame: object) -> None:
        reader._stop.set()
        _orig(sig, frame)

    server.handle_exit = _handle_exit

    log.info("HTTP server on %s:%d", config.HTTP_HOST, config.HTTP_PORT)
    await server.serve()


if __name__ == "__main__":
    asyncio.run(_main())
