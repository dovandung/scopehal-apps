ngscopeclient for Windows x64 with the **libsigrok4DSL plugin**: native support for the logic analyzers handled by [ALL-LOGIC](https://github.com/dovandung/ALL-LOGIC) / DSView, in addition to all of ngscopeclient's built-in instrument drivers.

### Downloads

- `ngscopeclient-…-windows-x64-portable.zip`: unzip anywhere and run `ngscopeclient.exe`
- `ngscopeclient-…-windows-x64.msi`: installer

### Supported by the plugin

| Hardware | Driver name | Device name shown | Path to use |
|---|---|---|---|
| DreamSourceLab DSLogic | `DSLogic` | DSLogic model, e.g. `DSLogic U3Pro16` | `dslogic` |
| DreamSourceLab DSCope | `DSCope` | DSCope model, e.g. `DSCope U3P100` | `dscope` |
| CH32H417 logic analyzer | `ch32-logic` | `CH32H417 LA USB3.0` / `USB2.0` | `ch32` |
| Sipeed SLogic Combo 8 / 16U3 / 32U3 | `slogic-16u3` | `SLogic Combo 8`, `SLogic16U3`, `SLogic32U3` | `slogic-16u3` |
| PXLogic32U3 | `pxlogic` | `PX-Logic U3 channel 32` (and variants) | `px-logic` |
| Alientek ATK-Logic (DL16 ...) | `atk-logic` | `ATK-Logic` | `atk` |
| ALIENTEK DL32 / DL32 Plus | `alientek-dl32` | `DL32`, `DL32 Plus` | `dl32` |
| nanoDLA / Cypress FX2 (fx2lafw) | `fx2lafw` | `nanoDLA`, `FX2 LA (16ch)` | `fx2lafw` |
| Cypress FX3 | `cypress-fx3` | `FX3` | `fx3` |
| RP2350 + MS2130 (hsdaoh-la) | `hsdaoh-la` | `la8`, `la4` or `la48 (RP2350 + MS2130)` | `hsdaoh-la` |
| Demo device (no hardware) | `virtual-demo` | `Demo Device` | `demo` |

Device firmware is included in `res\`, next to `ngscopeclient.exe`.

### Use

1. Install the WinUSB driver for your analyzer with [Zadig](https://zadig.akeo.ie/), as for ALL-LOGIC / DSView.
2. In ngscopeclient: **Add → Oscilloscope**, driver `libsigrok4dsl`, transport `null`, path from the table above (e.g. `fx3`, `slogic-16u3`, `fx2lafw`), or `demo` to try it without hardware. The path matches part of the device name or the exact driver name, case-insensitively.
   Options after a comma: `channels=N` (channel mode, see below), `res=C:\path\to\firmware`, `mode=dso`, `maxdepth=10000000`, `op=buffer` or `op=stream` (DSLogic, ALIENTEK DL32: Buffer Mode gives the DL32 its rates above 100 MS/s, up to 1 GS/s).

One libsigrok4DSL device per ngscopeclient window.

### Channel modes (`channels=`)

Many devices trade channels for sample rate. `channels=N` selects the mode
with N channels when the device is opened; `chmode=ID` selects a mode by the
driver's id instead. A count the device does not offer is ignored with a
warning in the log that lists its modes.

| Device | `channels=` | Max rate |
|---|---|---|
| Cypress FX3 | `8`, `16` | 200 / 100 MS/s |
| SLogic16U3 | `16`, `8`, `4` | 200 / 400 / 800 MS/s |
| SLogic32U3 | `32`, `16`, `8`, `4` | 200 / 400 / 800 / 1400 MS/s |
| SLogic Combo 8 | `8`, `4`, `2` | 40 / 80 / 160 MS/s |
| CH32H417 | `16`, `8` | |
| DSLogic | depends on model, e.g. `16`, `12`, `6`, `3` | |
| PXLogic | depends on model | |
| hsdaoh-la, la48 firmware | `8`, `4` | 140 / 280 MS/s |
| Demo | `16`, `12`, `6`, `3` | 125 / 250 / 500 / 1000 MS/s |

Examples: `fx3,channels=16`, `combo,channels=4`, `slogic16u3,channels=8`, `hsdaoh-la,channels=4`.

**Cypress FX3:** each mode is its own firmware, loaded into the FX3's RAM
when it is plugged in. The plugin saves `channels=` (shared with ALL-LOGIC,
in `%LOCALAPPDATA%\AllLogic\cypress-fx3.ini`) before the device scan, so an
FX3 plugged in after the last run gets the chosen firmware at once. If it
still runs the other firmware, the instrument is named "FX3 (replug to apply
channel mode)": close it, unplug and plug in the FX3, and add it again with
the same path (no restart needed).

**hsdaoh-la:** la8 has 8 channels (140 MS/s), la4 has 4 (280 MS/s). The la48
firmware switches between them at once, no replug: `hsdaoh-la,channels=4` or
`channels=8` (needs the Pico's USB serial port).

### Notes

Unofficial build, not endorsed by the ngscopeclient project or any hardware vendor. Built by GitHub Actions with MSYS2 UCRT64 from this repository's `libsigrok4dsl-plugin` branch and ALL-LOGIC `main`. The plugin is GPL-3.0 (as libsigrok4DSL); ngscopeclient is BSD-3-Clause.
