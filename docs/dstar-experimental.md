<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (c) 2026 lyra XE1POC -->

# D-STAR DV simplex on the DM-32UV

Build with `python3 tools/build_current.py`. Pass `--no-dstar` to build without
D-STAR. Use the radio name in Settings → My radio as the station's callsign
(up to eight letters and digits). In the VFO, choose Quick → Mode. To save a
memory, open Channels → Edit channel → Mode, select D-STAR on a supported
frequency and hold OK to save. The web Codeplug Editor shows D-STAR when
the radio reports the feature. The channel tag stores the mode and the
receive-only setting alongside the channel's standard frequencies.

The outgoing header uses `CQCQCQ` for UR and `DIRECT` for RPT1/RPT2. The incoming
source appears on the home screen after a valid header. The VFO and memory
channels use simplex routing. Direct DV voice works in the host tests,
including late entry when the header was missed.

The default DM-32UV build omits the built-in HTTP status server and the DMR
single-frequency repeater (SFR) to leave more main RAM free. CPS, APRS
transmit and receive, iGate, KISS, DMR Tier III, and the custom startup image
are available in this build.

The radio header, sync, and slow-data idle follow JARL's
[D-STAR specification](https://www.jarl.org/English/7_dstar/STD7_0_E.pdf),
sections 4.1 and 6 and appendices 1–2. Microphone and speaker blocks go through
a fixed-point first-generation AMBE voice implementation. Its decoder uses the
ISC-licensed tables from mbelib, with attribution in the table header. The
encoder estimates the harmonics and quantizes them independently. D-STAR DV
uses the software implementation and the idle vocoder's RAM window. DMR and
NXDN still require the compatible factory AMBE+2 codec. Preserve it with the
RadioFoundry serial stub if already installed or merge it locally from a
supported manufacturer firmware file when using the factory loader.

The channel regression checks both protected words and both uncoded words
against the original ISC-licensed DSD interleave schedule. A software bench
also plays a modeled discriminator WAV into DSD-FME with `-fd` to check the
callsigns and voice with an independent receiver.

The receiver uses the 8 kHz discriminator stream and 4800-symbol/s binary
profile. The DM-32UV transmits 4800 binary symbols/s through the HR_C7000's
RRC pulse path with a nominal outer deviation of 1200 Hz. The provider marks
this shape as approximate. Validate deviation, occupied bandwidth, RX/TX with
another D-STAR radio, and the 20 ms audio cadence on a cabled bench before RF
use. The host tests do not measure the physical transmitter or MCU timing.

From the repository root after preparing the RadioFoundry tree:

```sh
cc -std=c11 -Wall -Wextra -Werror -Isrc/dstar -Ibuild/current/core \
    src/dstar/dv_frame.c src/dstar/dv_stream.c src/dstar/ambe_wire.c \
    build/current/core/dmr/ambe/ambeFec.c tests/test_dstar_stream.c \
    -o /tmp/test-dm32-dstar-stream
/tmp/test-dm32-dstar-stream
```
