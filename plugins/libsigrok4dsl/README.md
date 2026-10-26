# libsigrok4DSL driver plugin for ngscopeclient

Native, in-process ngscopeclient driver for every device supported by
**libsigrok4DSL** — the device layer of
[DSView](https://github.com/DreamSourceLab/DSView) and
[ALL-LOGIC](https://github.com/dovandung/ALL-LOGIC). No bridge server, no
sockets: ngscopeclient loads the plugin, the plugin links libsigrok4DSL and
talks to the USB device directly.

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

Logic analyzers appear as digital channels; DSCope in DSO mode as analog
channels.

## Build (Linux)

Build ngscopeclient as usual, adding `ALLLOGIC_DIR` pointing at an ALL-LOGIC
(or DSView 1.3+) source checkout. libsigrok4DSL is compiled from that tree
into the plugin; nothing has to be installed from it.

```sh
# ngscopeclient's own dependencies, plus:
sudo apt install libusb-1.0-0-dev zlib1g-dev libglib2.0-dev

git clone https://github.com/dovandung/ALL-LOGIC
git clone --recursive https://github.com/dovandung/scopehal-apps
cd scopehal-apps
cmake -B build -DALLLOGIC_DIR=$PWD/../ALL-LOGIC
cmake --build build -j
./build/src/ngscopeclient/ngscopeclient
```

The plugin is written next to the ngscopeclient binary
(`build/src/ngscopeclient/libscopehal-libsigrok4dsl.so`), which is one of the
places scopehal loads plugins from. `cmake --install` puts it in
`<prefix>/lib/scopehal/plugins/`. You can also copy it to
`~/.scopehal/plugins/`.

USB access needs the same udev rules as ALL-LOGIC / DSView, and devices that
load firmware or an FPGA bitstream need DSView's `res` directory (see below).

## Windows

`.github/workflows/windows.yml` builds everything on GitHub's Windows runners
with MSYS2 UCRT64 (ngscopeclient's documented toolchain): libuvc and
[libhsdaoh](https://github.com/dovandung/hsdaoh) for the hsdaoh-la driver,
ngscopeclient with all of its built-in drivers, this plugin, the engine tests,
and an in-process smoke test on software Vulkan. It uploads:

- `ngscopeclient-<version>-windows-x64-portable.zip`: unzip, run
  `ngscopeclient.exe`
- `ngscopeclient-<version>-windows-x64.msi`: installer

Both contain `plugins\libscopehal-libsigrok4dsl.dll` (scopehal loads
`<exe dir>\plugins\*.dll`), the DLLs it needs next to the exe, and the device
firmware in `res\`, which the plugin finds on its own. Pushing a `v*` tag also
attaches both files to a GitHub release; so does running the workflow by
hand (Actions → Windows build → Run workflow) with a `release_tag`.

USB devices need the WinUSB driver (install it with
[Zadig](https://zadig.akeo.ie/) as for DSView / ALL-LOGIC).

To build locally, follow ngscopeclient's MSYS2 instructions, add
`mingw-w64-ucrt-x86_64-glib2 mingw-w64-ucrt-x86_64-libusb`, and pass
`-DALLLOGIC_DIR=...` (and `-DWIXPATH=...` for the zip/MSI) to cmake.

## Use

**Add → Oscilloscope**, driver **`libsigrok4dsl`**, transport **`null`**,
path:

```
<device>[,res=<firmware dir>][,mode=logic|dso][,maxdepth=<samples>][,channels=<count>][,chmode=<id>][,op=buffer|stream]
```

- `<device>`: matched case-insensitively against, in order, a device index
  (`0`), part of the device name (`fx3`, `ch32`), or the exact driver name
  (`cypress-fx3`). See the "Path to use" column above. A name match wins over a
  driver match, so prefer the driver name when one name contains another
  (`slogic` would also match "DSLogic"). Empty picks the first USB device, or the demo
  device if none is connected.
- `res=`: firmware/bitstream directory. Default: `$DSVIEW_RES_DIR`, then
  `~/.local/share/DSView/res`, `/usr/local/share/DSView/res`,
  `/usr/share/DSView/res`, `/usr/local/share/ALL-LOGIC/res`, and finally the
  `DSView/res` folder of the ALL-LOGIC tree the plugin was built from.
- `mode=`: switch the device mode first (DSCope and the demo device have
  both).
- `op=buffer` / `op=stream`: Buffer Mode (captured into the device's memory,
  then read) or Stream Mode, on devices that have both (DSLogic, ALIENTEK
  DL32). Default: Buffer Mode on DSLogic, the driver's default (Stream Mode)
  on the DL32. The DL32's rates above 100 MS/s (up to 1 GS/s) exist in Buffer
  Mode only, as long as rate x enabled channels fits its memory budget
  (8 G on the DL32 Plus, 4 G on the DL32); a capture that does not fit is
  refused and the log says what would.
- `maxdepth=`: cap the memory depths offered (default: the driver's hardware
  depth, at most 100 M samples).

Examples: `fx3`, `dl32,op=buffer`, `fx3,channels=16`, `slogic-16u3`, `fx2lafw,res=/opt/ALL-LOGIC/res`, `demo,mode=dso`.

The path is saved with the session, so reopening a session reconnects to the
same device.

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

Examples: `fx3,channels=16`, `combo,channels=4`, `slogic16u3,channels=8`,
`hsdaoh-la,channels=4`.

**Cypress FX3:** each mode is its own firmware, loaded into the FX3's RAM
when the library finds it freshly plugged in (boot loader). The plugin saves
`channels=` (shared with ALL-LOGIC, in
`%LOCALAPPDATA%\AllLogic\cypress-fx3.ini`) *before* the library's first scan,
so:

- FX3 plugged in after the last run: start ngscopeclient and add `fx3,channels=16`;
  the 16 channel firmware is loaded right away.
- FX3 still running the other firmware (it stays loaded until unplugged): the
  instrument is named "FX3 (replug to apply channel mode)" and keeps the
  channels the firmware has. Close the instrument, unplug and plug in the FX3,
  and add it again with the same path; no need to restart ngscopeclient.

Only the channels the running firmware samples are shown (8 or 16).

**hsdaoh-la:** the la8 firmware has 8 channels (up to 140 MS/s), la4 has 4
(up to 280 MS/s), and la48 switches between the two: `hsdaoh-la,channels=4`
or `channels=8` sends `ch 4` / `ch 8` to the Pico over its USB serial port
when the instrument is added, at once, with no replug. The sample rate list
follows the mode. Without the Pico's serial port the mode cannot be changed.
la4 streams need libhsdaoh (branch `la`) from commit `25951fc` on; the
Windows build checks for it.

### What is supported

- Sample rates from the driver, depths in a 1-2-5 series up to the hardware
  limit.
- Run, single, stop, force (one capture with the trigger disabled).
- Edge trigger (rising / falling / any) on any channel, with pre-trigger
  position from the trigger offset. Drivers that report where they triggered
  (FX3, SLogic, hsdaoh, PXLogic, DSLogic) are aligned to the exact sample.
- Channel enable / disable (fewer channels allow higher rates on most devices).
- Logic threshold on devices that have one (`SR_CONF_VTH`), as one bank.
- DSCope: V/div, AC/DC coupling, probe attenuation, trigger level, clipping.

One libsigrok4DSL device per ngscopeclient process: the library keeps its
state in globals.

## How it works

```
ngscopeclient ── LibSigrok4DSLOscilloscope ── DSLSession ── libsigrok4DSL ── USB
                 (scopehal driver)             (capture engine)
```

- **DSLSession** (no scopehal types) opens the device through the library's
  `ds_*` API and runs one `ds_start_collect()` per capture on its own thread.
  Drivers deliver a capture as `SR_DF_LOGIC` chunks in `LA_CROSS_DATA` layout
  (64-sample groups, 8 bytes per enabled channel), an optional
  `SR_DF_TRIGGER`, then `SR_DF_END`; the engine reassembles per-channel data
  and queues complete captures. DSCope frames (`SR_DF_DSO`) are queued as they
  arrive.
- Reconfiguring (rate, depth, channels) aborts the running capture and drops
  anything captured with the old settings.
- libsigrok4DSL never joins its collect thread when a capture ends by itself;
  the engine joins it, so long sessions do not leak a thread per capture.
- **LibSigrok4DSLOscilloscope** turns captures into `SparseDigitalWaveform`s
  (run-length; buffers sized exactly by counting transitions first, so deep
  captures do not reserve worst-case memory) or `UniformAnalogWaveform`s.
- The plugin exports only `PluginInit`. libsigrok4DSL's symbols stay private;
  logging and scopehal symbols resolve to ngscopeclient's own.

## Tests without hardware or GPU

The engine and the bit decoder have a standalone test that uses the
libsigrok4DSL demo device:

```sh
cmake -B build -DALLLOGIC_DIR=... -DLIBSIGROK4DSL_PLUGIN_TESTS=ON
cmake --build build --target libsigrok4dsl-engine-test
./build/plugins/libsigrok4dsl/libsigrok4dsl-engine-test             # logic
./build/plugins/libsigrok4dsl/libsigrok4dsl-engine-test --mode dso  # DSO
./build/plugins/libsigrok4dsl/libsigrok4dsl-engine-test fx3         # real device
```

It checks continuous and single captures, reconfiguration while running,
forced capture, trigger restore, capture rate, thread count, and that the
decoder round-trips random data with an exact run count.
