# Security Policy

## Supported Versions

Only the latest release on the `main` branch receives security updates.

## Reporting a Vulnerability

**Do not open a public issue for security vulnerabilities.**

To report a vulnerability, contact the maintainer privately:

- **Email:** jakub@jakubpawlina.com
- **GitHub:** Send a private message via [jakubpawlina](https://github.com/jakubpawlina)

Please include:

1. A description of the vulnerability and its potential impact.
2. Steps to reproduce, or a minimal proof of concept.
3. The affected component (firmware, service, deployment, simulation).

If the report is accepted, a fix will be developed privately and disclosed
in a patch release with credit to the reporter (unless anonymity is
requested).

## Scope

The following components are in scope:

- **Raspberry Pi service** (`service/`) -- REST API, SSE, SQLite storage,
  serial reader, and deployment scripts.
- **ESP32 firmware** (`firmware/`) -- NMEA parsing, serial output, and
  peripheral control.
- **Deployment tooling** (`tools/`) -- installation and configuration scripts.

Out of scope: third-party dependencies (report upstream), the Wokwi simulation
environment, and hardware design.

## Known Limitations

- The HTTP API ships with **optional API key authentication** for write
  endpoints (`GPS_API_KEY`), but read-only endpoints remain open by default.
  Deploy on a trusted network or behind an authenticated reverse proxy for
  full protection. See the [deployment guide](docs/deploy.md) for details.
- CORS is configured to allow all origins by default (`GPS_CORS_ORIGINS=*`).
  Restrict this in production deployments.
