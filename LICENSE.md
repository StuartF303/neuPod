# Licensing

neuPod is split across three licences, mirroring the Sprout project:

| Part | Paths | Licence | File |
|------|-------|---------|------|
| Firmware and software | `app/`, `hal/`, `platforms/`, `tests/`, `tools/`, `scripts/`, build files | Mozilla Public License 2.0 | [LICENSE-MPL-2.0](LICENSE-MPL-2.0) |
| Hardware designs | `boards/` (schematics, PCB layouts, BOMs, mechanical files) | CERN Open Hardware Licence v2 – Strongly Reciprocal | [LICENSE-CERN-OHL-S-2.0](LICENSE-CERN-OHL-S-2.0) |
| Documentation | `docs/`, `README.md` and other prose Markdown | Creative Commons Attribution 4.0 International | [LICENSE-CC-BY-4.0](LICENSE-CC-BY-4.0) |

SPDX identifiers: `MPL-2.0`, `CERN-OHL-S-2.0`, `CC-BY-4.0`.

Where a file carries its own SPDX header, that header takes precedence. Third-party code
(e.g. the Pico SDK, ESP-IDF, FatFs, codec libraries) remains under its own licence.
