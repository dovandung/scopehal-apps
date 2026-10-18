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
| nanoDLA / Cypress FX2 (fx2lafw) | `fx2lafw` | `nanoDLA`, `FX2 LA (16ch)` | `fx2lafw` |
| Cypress FX3 | `cypress-fx3` | `FX3` | `fx3` |
| RP2350 + MS2130 (hsdaoh-la) | `hsdaoh-la` | `la8 (RP2350 + MS2130)` | `hsdaoh-la` |
| Demo device (no hardware) | `virtual-demo` | `Demo Device` | `demo` |

Device firmware is included in `res\`, next to `ngscopeclient.exe`.

### Use

1. Install the WinUSB driver for your analyzer with [Zadig](https://zadig.akeo.ie/), as for ALL-LOGIC / DSView.
2. In ngscopeclient: **Add → Oscilloscope**, driver `libsigrok4dsl`, transport `null`, path from the table above (e.g. `fx3`, `slogic-16u3`, `fx2lafw`), or `demo` to try it without hardware. The path matches part of the device name or the exact driver name, case-insensitively.
   Options: `slogic,res=C:\path\to\firmware`, `demo,mode=dso`, `fx2lafw,maxdepth=10000000`.

One libsigrok4DSL device per ngscopeclient window.

### Notes

Unofficial build, not endorsed by the ngscopeclient project or any hardware vendor. Built by GitHub Actions with MSYS2 UCRT64 from this repository's `libsigrok4dsl-plugin` branch and ALL-LOGIC `main`. The plugin is GPL-3.0 (as libsigrok4DSL); ngscopeclient is BSD-3-Clause.
