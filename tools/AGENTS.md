# tools/ KNOWLEDGE BASE

**Scope:** `tools/` subdirectory of NASA cFS. Three git submodules, all lab/example quality.

## TOOL MAP

| Directory | Language | Purpose | Key Deps |
|-----------|----------|---------|----------|
| `cFS-GroundSystem/` | Python 3 | GUI ground station: telemetry display + command sending | PyQt5, pyzmq |
| `elf2cfetbl/` | C + CMake | Converts ELF object files to cFE binary table images (`.tbl`) | CMake |
| `tblCRCTool/` | C | Computes CRC-16/ARC for `.tbl` files to validate integrity | CMake |

---

## GROUND SYSTEM (`cFS-GroundSystem/`)

### Architecture

```
cFS target ──UDP 2234──► RoutingService.py ──ZMQ PUB──► TelemetrySystem.py ──Qt signals──► GUI
cmdUtil / MiniCmdUtil.py ──UDP──► cFS target
```

- **RoutingService.py** binds UDP 2234, republishes packets as ZeroMQ PUB on `ipc:///tmp/GroundSystem-<user>`
- **TelemetrySystem.py** subscribes to ZMQ, decodes packets, emits Qt signals for display widgets
- **MiniCmdUtil.py** assembles CCSDS/cFE command packets and sends them via UDP to a host:port
- ZMQ topic format: `GroundSystem.<spacecraft>.TelemetryPackets.<pkt_id>`

### Setup and run

```bash
pip3 install pyzmq PyQt5
cd tools/cFS-GroundSystem/Subsystems/cmdUtil && make  # build C cmdUtil for scripted use
cd ../.. && python3 GroundSystem.py
```

### Config files (edit before running)

| File | Controls |
|------|---------|
| `command-pages.txt` | Command definitions and parameter layouts |
| `telemetry-pages.txt` | Telemetry page layouts and field mappings |
| `quick-buttons.txt` | Toolbar shortcut commands |

**cmdUtil** (`Subsystems/cmdUtil/`) is a standalone C binary for scripted command injection without the GUI. Build with `make` in that directory.

---

## TABLE TOOLS

### elf2cfetbl

Reads the `CFE_TBL_FileDef` symbol from a compiled ELF object and writes a cFE binary table image.

```bash
elf2cfetbl [-t table_name] [-d description] [-s/-S SCID] [-p/-P PRID] <input.o> <output.tbl>
```

CMake integration: `add_cfe_tables` macro in `scripts/add_cfe_tables_impl.cmake` wires this into the build automatically. You normally don't call it directly.

### tblCRCTool

Computes CRC-16/ARC over a `.tbl` file body (skips file and table headers). Produces `cfe_ts_crc`.

```bash
cfe_ts_crc <table_file.tbl>
```

Use this to verify a table image hasn't been corrupted before uploading to a target.
