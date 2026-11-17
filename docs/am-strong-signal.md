# AM air band: keeping the demodulator out of overload

Patch `0046-am-air-band-gain-loop.patch`.

## Report

AM air band audio sounds saturated. It did after the rollback in [am-regression.md](am-regression.md) too: the rollback
restored an AM receiver with no gain control, so the original distortion stayed.

## What overdrives the demodulator

The FD6818's AM demodulator is overdriven when the RSSI register (0x67, 0.5 dB per count) reads above about 143. This is
RadioFoundry's finding on a DM-32UV, not a result of this repository: with its loop settling carriers at 0x67 148-158,
AM audio was reported distorted (RadioFoundry #545); with the loop settling them at 129-143 (137-142 over a 30 dB
range on its bench) it is not. It is a register reading and a report of the audio. Nobody has recorded this radio's
audio. See `docs/spec/hardware-dm32.md` 8.7, "Where the gain loop settles", in the derivative tree.

The reading follows the receiver gain (0x13). The radio's own firmware, as RadioFoundry read it, runs a software gain
loop in AM that holds the reading under that limit. These AM receivers have been in this tree. The last two columns are
a model: the reading is the signal's full-gain reading minus 2 counts per dB of gain taken off, run on the real driver code
([`experiments/am-agc/agc_model.c`](../experiments/am-agc/agc_model.c)). The first is over the 186 full-gain readings
the gain table can bring in range (144-329; an empty channel reads 133-158 at full gain on the bench), the second for
a carrier reading 220-320 at full gain (a 45-93 dB cut):

| Build | AM gain | Readings it leaves over 143 | Gain back within 12 dB of full after the carrier ends |
|---|---|---|---|
| before `527f5229`, and again since the rollback `7558e84` | fixed 0x037E (-12 dB), no loop | 162: every station reading over 167 | not applicable |
| `527f5229`, withdrawn | loop that cuts only at 10 dB or more over its target | 42: readings 144-187, the weak to moderate stations, never cut | 12-28 s |
| `0046` | the same loop with one-entry cuts from 1 dB over | 0 | 1.1-1.6 s |

So the rollback left the distortion on nearly every station, and the withdrawn loop left it on the weak and moderate
ones, which is most of what an air band receiver hears. RadioFoundry corrected the loop in `dc1140bf`, on 2026-11-15,
about three hours before the backport was made; the backport took `ad79ab67` alone. `0046` is that correction on top
of the same set-up.

## What `0046` does

- AM set-up, written at every AM tune (the radio's own air band set-up as RadioFoundry read it from its firmware):
  receive filter 0x43 = 0x0048 (the narrowest, about 1.7 kHz), AF output 0x47 = 0x6740, 0x4A = 0x5450, and 0x13 at full
  gain (0x03FF) to start the loop.
- Gain loop, one step per status poll (25 ms): a reading 1-9 dB over the target (0x67 = 142) takes the gain down one
  table entry, 10 dB or more takes it down at once by the excess less 8 dB, and a reading 6 dB or more under raises it
  one entry per poll once 30 polls have passed since the last cut or the last reading within 6 dB of the target. A
  steady station settles at 0x67 129-143.
- Squelch on the chip's noise indicator (`noise_sq`, 0x65 bits 7:0) plus an offset taken from 0x4F, so level 1 opens where
  the chip's own squelch does and each level is 3 dB tighter. The loop holds 0x67 near its target with or without a
  signal, so the RSSI cannot tell a carrier from an empty channel any more; the `+20 dB` adjustment of `0022` goes.
- The RSSI shown adds back the gain the loop took off.
- Leaving AM (FM or DMR, including a mode change) restores 0x13, 0x47 and 0x4A as FM left them. All of it sits behind
  `CONFIG_AM_RX`.

The register set-up is the withdrawn backport's. What is different is the loop's correction, and a closed-loop test
that the earlier suite lacked.

## Checked here

- `fd6818Test`: register sequences, the loop's steps, limits, RSSI correction, squelch offsets, restoration to FM and
  DMR, and a closed-loop test (`test_am_agc_closed_loop`): every reachable signal settles in 129-143 and stays; a
  carrier that starts at full gain is over the limit for at most six polls (150 ms) and in band for good within 1.5 s; the
  gain is back within 12 dB of full in under 2 s after it ends. With RadioFoundry's `dc1140bf` reverted in a copy of the
  driver, that test fails (the withdrawn loop is what it was written against).
- The radio core (six variants), radio features and listening-band suites, and the rest of the host suite, with
  AddressSanitizer and UndefinedBehaviorSanitizer. Six UI-test targets (`uiFlowTest`, `uiFlowTestNoColor`,
  `uiFlowTestP25`, `uiFlowTestP25Trunk`, `uiSim`, `beepReel`) do not link in the derivative: `ui_menus.c` calls
  `codeplug_aes_kid` since `0044` and those tests don't link `codeplug.c`. They fail the same way without `0046`, which was
  checked by reversing it; they are not part of this change and were left as they are.
- The image builds with the C-SKY toolchain and the release flags (`--speed-codec --stack-check --lean --trunk-tx`),
  with zero compiler warnings, and the static stack check passes (least free: the idle task, 136 bytes, as before).
  Against the same build without `0046` the image is 728 bytes larger and uses 8 bytes more RAM (561,068 against
  560,340 bytes; RAM 209,016 of 211,456, which leaves 2,440). With `CONFIG_AM_RX=n` the AM code is gone: 559,768 bytes
  and the RAM of the build without the patch, also with zero warnings.
- The generated tree matches the patch series (`python3 tools/rf_patch.py check`).
- No radio was flashed and no RF emitted.

## Not checked

- Nobody has heard this build. The settling points are RadioFoundry's bench readings on its DM-32UV, applied here
  through a model; the model's linear reading is a good fit near the settling point and an assumption far from it.
- A signal that reads over 329 at full gain cannot be brought under 143 by any loop that uses this table: its range is
  93 dB. A transmitter very close to the antenna may be beyond it, and then the loop cannot help.
- The squelch. `0022`'s was checked on this radio; the noise squelch was bench-tested only by RadioFoundry (empty
  channel shut at levels 1 and 5, carriers 5 dB apart opening it).
- After a strong transmission the gain needs up to 1.6 s to come back. A weak station that keys up inside that
  window starts quieter than it would from full gain.
- When a strong carrier starts, the loop cuts up to about 16 dB deeper than it needs: it averages each reading with the
  one before, which was taken before the cut, as the radio's own loop does. It gives that back within about a second.
  This is the averaging the diagnostic model in [am-regression.md](am-regression.md) pointed at; it is kept, not fixed.

## If it still sounds saturated

One step at a time, each as its own patch, and compare on the same station, frequency, volume and antenna:

1. Does a weak station sound as bad as a strong one? If only strong stations do, that points at the RF side still being
   overloaded; if both do, it points at the audio stages.
2. The radio's own air band set-up also writes 0x48 (AF gain) as `0x31E0 | s`, `0x31ED` by default, where the FM and DMR
   set-ups leave `0xBBBF`. RadioFoundry documents it and does not write it, and this tree does not either. It is the next
   candidate if weak stations are saturated too. It is not measured.
3. Turning the volume knob down does not help distortion made before the amplifier (in the chip); it does help
   distortion made in the amplifier or speaker.
