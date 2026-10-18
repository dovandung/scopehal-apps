ngscopeclient for Windows x64 with the **libsigrok4DSL plugin**: native support for the logic analyzers handled by [ALL-LOGIC](https://github.com/dovandung/ALL-LOGIC) / DSView, in addition to all of ngscopeclient's built-in instrument drivers.

### Downloads

- `ngscopeclient-…-windows-x64-portable.zip`: unzip anywhere and run `ngscopeclient.exe`
- `ngscopeclient-…-windows-x64.msi`: installer

### Supported by the plugin

DreamSourceLab DSLogic / DSCope, CH32H417, Sipeed SLogic Combo 8 / 16U3 / 32U3, PXLogic32U3, ATK-Logic (DL16 …), nanoDLA / FX2 (fx2lafw), Cypress FX3, hsdaoh-la (RP2350 + MS2130), and a demo device that needs no hardware.

Device firmware is included in `res\`, next to `ngscopeclient.exe`.

### Use

1. Install the WinUSB driver for your analyzer with [Zadig](https://zadig.akeo.ie/), as for ALL-LOGIC / DSView.
2. In ngscopeclient: **Add → Oscilloscope**, driver `libsigrok4dsl`, transport `null`, path = device name or driver, e.g. `slogic`, `fx2lafw`, `DSLogic`, or `demo` to try it without hardware.
   Options: `slogic,res=C:\path\to\firmware`, `demo,mode=dso`, `fx2lafw,maxdepth=10000000`.

One libsigrok4DSL device per ngscopeclient window.

### Notes

Unofficial build, not endorsed by the ngscopeclient project or any hardware vendor. Built by GitHub Actions with MSYS2 UCRT64 from this repository's `libsigrok4dsl-plugin` branch and ALL-LOGIC `main`. The plugin is GPL-3.0 (as libsigrok4DSL); ngscopeclient is BSD-3-Clause.
