# libsigrok4DSL driver plugin for ngscopeclient

Native, in-process ngscopeclient driver for every device supported by
**libsigrok4DSL** — the device layer of
[DSView](https://github.com/DreamSourceLab/DSView) and
[ALL-LOGIC](https://github.com/dovandung/ALL-LOGIC). No bridge server, no
sockets: ngscopeclient loads the plugin, the plugin links libsigrok4DSL and
talks to the USB device directly.

| Driver | Hardware |
|---|---|
| `DSLogic` / `DSCope` | DreamSourceLab |
| `ch32` | CH32H417 logic analyzer |
| `slogic16u3` | Sipeed SLogic Combo 8 / 16U3 / 32U3 |
| `pxlogic` | PXLogic32U3 |
| `atk-logic` | Alientek ATK-Logic (DL16 ...) |
| `fx2lafw` | nanoDLA / Cypress FX2 |
| `cypress-fx3` | Cypress FX3 |
| `hsdaoh-la` | RP2350 + MS2130 (when `libhsdaoh` is found) |
| `virtual-demo` | libsigrok4DSL's demo device, no hardware |

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

## Use

**Add → Oscilloscope**, driver **`libsigrok4dsl`**, transport **`null`**,
path:

```
<device>[,res=<firmware dir>][,mode=logic|dso][,maxdepth=<samples>]
```

- `<device>`: an index (`0`), part of the device name (`slogic`, `DSLogic`),
  or a driver name (`fx2lafw`). Empty picks the first USB device, or the demo
  device if none is connected.
- `res=`: firmware/bitstream directory. Default: `$DSVIEW_RES_DIR`, then
  `~/.local/share/DSView/res`, `/usr/local/share/DSView/res`,
  `/usr/share/DSView/res`, `/usr/local/share/ALL-LOGIC/res`, and finally the
  `DSView/res` folder of the ALL-LOGIC tree the plugin was built from.
- `mode=`: switch the device mode first (DSCope and the demo device have
  both).
- `maxdepth=`: cap the memory depths offered (default: the driver's hardware
  depth, at most 100 M samples).

Examples: `slogic`, `fx2lafw,res=/opt/ALL-LOGIC/res`, `demo,mode=dso`.

The path is saved with the session, so reopening a session reconnects to the
same device.

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
./build/plugins/libsigrok4dsl/libsigrok4dsl-engine-test slogic      # real device
```

It checks continuous and single captures, reconfiguration while running,
forced capture, trigger restore, capture rate, thread count, and that the
decoder round-trips random data with an exact run count.
