# PIX-DT195 user-space driver (research)

A small, native user-space driver for the **PIXELA PIX-DT195** USB digital-TV
tuner, written for current macOS (Apple Silicon / arm64) using
[libusb](https://libusb.info/).

The stock `StationTV.pkg` in `../bin` only ships Intel (x86_64) binaries built
against the macOS 10.14/10.15 SDKs. They still run under Rosetta on recent
macOS, but there is no native code and no documented interface for talking to
the hardware yourself. This directory reconstructs enough of the USB interface,
from the shipped frameworks, to bring the tuner up and pull the raw MPEG-2
Transport Stream (TS) off it for research and interoperability work.

## Scope

* **In scope:** enumerating the device, uploading the controller firmware,
  issuing the vendor control/register commands, and reading the raw TS bulk
  endpoint to a file.
* **Out of scope (deliberately):** the B-CAS / ARIB CAS conditional-access and
  descrambling logic, and PIXELA's content-protection (`PxSeleneDrvEncrypt`,
  "secure" endpoints, DTCP move). This project does **not** reimplement,
  bypass, or document how to defeat any copy protection. The raw TS captured
  here is whatever the broadcaster transmits — for pay services that payload
  stays scrambled, and this driver keeps it that way.

This is interoperability research against hardware you own. Respect the
broadcast-content laws in your jurisdiction.

## Hardware, as reconstructed from the shipped frameworks

| Item | Value |
|------|-------|
| USB vendor ID | `0x06B8` (PIXELA Corporation) |
| USB product ID | `0x103A` |
| Controller SoC | Fujitsu/Socionext **MB86M01 "Felis"** (ASSP) |
| RF tuner front-ends | MaxLinear **MxL661**, Sony **CXD2832AWR** (demod) |
| Transport | USB bulk (TS in), vendor control requests + register read/write for control |

The matching values come from `DT195Lib`'s device-discovery thread
(`UsbWaitThread`), which builds an `IOUSBDevice` matching dictionary with
`idVendor`/`idProduct` taken from the two `USBConnectionManagerMacImpl(vid, pid)`
constructor arguments — `0x06B8` and `0x103A`.

### Controller firmware

The MB86M01 has no on-board application firmware; the host uploads it at
start-up. Three blobs ship inside
`DT195Lib.framework/Versions/A/Resources/`:

| File | Size (bytes) | Purpose |
|------|--------------|---------|
| `mb86m01_assp_osec_idle.bin`   | 333 056  | "idle" firmware, loaded first to bring the chip up |
| `mb86m01_assp_osec_trc_m_h.bin`| 1 497 648| MPEG-2 → H.264 transcode firmware |
| `mb86m01_assp_osec_trc_h_h.bin`| 1 565 616| H.264 → H.264 transcode firmware |

These are **PIXELA's proprietary firmware and are not redistributed here.**
Use `./extract-firmware.sh` to copy them out of a copy of `StationTV.pkg` (or an
already-installed `StationTV.app`) that you obtained legitimately. The build
and the runtime expect them in `./firmware/`.

## Software architecture (from the disassembly)

The shipped stack layers like this, top to bottom:

```
StationTV.app  (UI)
  └─ AirTunerSystemFelis.framework   high-level tuner/session API
       └─ DT195Lib.framework
            ├─ DT195ManagerImpl        per-device session objects
            ├─ SeleneConnectionManager firmware boot, I2C/register I/O, TS pump
            │     ├─ TunerManager       RF front-end orchestration
            │     │     ├─ MxL661Manager        (MaxLinear tuner)
            │     │     └─ CXD2832AWRManager     (Sony demod)
            │     └─ CommonQueue         ring buffers between USB reader and consumer
            └─ USBConnectionManagerMacImpl   thin IOKit USB wrapper
```

The USB wrapper (`USBConnectionManagerMacImpl`) exposes only:

* `usbWriteControl(dev, iface, bmRequestType, bRequest, wValue, wIndex, data, wLength)`
  — a standard USB control transfer (`IOUSBDevRequest` under the hood).
* `usbWriteBulk(dev, pipe, buf, len)` / `usbReadBulk(dev, pipe, buf, len, &got, timeout)`
  — bulk transfer on a pipe index obtained from interface enumeration (pipes
  are **not** hardcoded; they are discovered from the interface).
* `usbReadBulkAsync(...)` — async bulk read used by the TS pump thread.

Everything device-specific (register addresses, I2C targets, tuning tables)
lives above that in `SeleneConnectionManager` / `TunerManager`.

### Bring-up / streaming sequence

Reconstructed from `SeleneConnectionManager::downloadIdleFirwmware`,
`startDriverAuth`, `TunerManager::Initialize`, `DT195ManagerImpl::selectChannel`,
`startSys1Recv` and `getSys1Stream`:

1. **Enumerate & open** `06B8:103A`, set configuration, claim the interface.
2. **Reset** the controller (vendor "Force Reset" command) and wait on the
   interrupt endpoint for the chip to report status.
3. **Upload idle firmware**: stream `mb86m01_assp_osec_idle.bin` to the
   controller's load register (seen writing to address `0x90074`) in **4096-byte
   chunks**, then wait for the chip to reach the "Idle" firmware state.
4. **Initialise tuner**: `TunerManager::Initialize` wakes the RF path and
   programs the MxL661 / CXD2832AWR over the chip's I2C bus (via register
   read/write commands).
5. **Select channel**: `selectChannel(tuner, wave, frequency, ts_id, service_id)`
   where `wave` is terrestrial / BS / CS. This programs the front-end frequency
   and selects the transport stream.
6. **Start the raw TS path**: `startSys1Recv` tells the firmware to push the
   broadcast TS ("Sys1" = the raw system stream, as opposed to the transcoded
   "play" path), then `getSys1Stream` drains the bulk endpoint into a buffer.

Steps 1–3 are implemented in this skeleton. Steps 4–6 need the per-chip
register/I2C tables, which are large; the reader (`dt195_stream_raw`) and the
command plumbing are in place, and the exact tuning tables are the next
research target — see `TODO` markers in `dt195.c`.

## Build

```sh
brew install libusb        # arm64 Homebrew
./extract-firmware.sh /path/to/StationTV.pkg    # populate ./firmware
make
```

## Run

```sh
sudo ./dt195 info                      # enumerate and show device / firmware state
sudo ./dt195 capture out.ts            # bring chip up and dump the raw TS
```

`sudo` (or an appropriate entitlement) is needed because macOS requires
elevated privileges to claim a USB interface that no kernel driver has already
grabbed.

## Status

This is a research scaffold, not a finished product. What works today:
device discovery, firmware upload, the control/register command primitives, and
the bulk TS reader loop. What remains: the RF tuning register sequences
(MxL661 / CXD2832AWR programming) needed before a real stream appears on the
bulk endpoint.
