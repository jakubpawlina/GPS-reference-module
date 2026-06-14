#!/usr/bin/env python3
"""Generate and serve firmware API documentation."""

import functools
import http.server
import os
import signal
import subprocess

# When launched via `trap '' INT; exec python3 ...`, SIGINT is inherited as
# SIG_IGN so the parent sh wrapper doesn't die from Ctrl+C.  Re-enable it
# here so KeyboardInterrupt works normally in Python.
signal.signal(signal.SIGINT, signal.default_int_handler)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS_DIR = os.path.join(ROOT, "firmware/gps_reference_module/build/doxygen/html")
PORT = int(os.environ.get("PORT", "8080"))

subprocess.run(
    [os.path.join(ROOT, "tools/run-firmware-checks.sh"), "docs"],
    check=True,
)

handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=DOCS_DIR)
server = http.server.HTTPServer(("0.0.0.0", PORT), handler)

print(f"Serving docs at http://0.0.0.0:{PORT}/")
try:
    server.serve_forever()
except KeyboardInterrupt:
    # Ignore further SIGINTs during Python's threading shutdown to prevent
    # the "Exception ignored on threading shutdown" traceback.
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    server.server_close()
    print()
