# AM aviation strong-signal Hotfix

The report describes choppy, distorted AM aviation audio when an aircraft passes close overhead. The previous
derivative changed only the demodulator selection for AM, retained FM's receive filter and fixed gain, and used
RSSI for squelch with a board-level +20 dB adjustment. That is a software deficiency consistent with overload,
but the specific aircraft report has not been reproduced on this radio.

## Change and provenance

Patch `0028-am-strong-signal-agc.patch` backports the MIT RadioFoundry change
[ad79ab67](https://github.com/RadioFoundry/RadioFoundry/commit/ad79ab67b57365c3342d1afa04b488a1eefb92f5),
with its copyright notices and host tests. It does not update unrelated upstream features.

- AM receive filter 0x43 = 0x0048, AF 0x47 = 0x6740 and AM control 0x4A = 0x5450.
- Gain 0x13 starts at 0x03FF on every AM tune. Each status poll runs the upstream 42-entry gain loop, with fast
  reduction on strong readings and delayed, gradual recovery. The displayed RSSI adds back the attenuation used
  for that reading before the loop changes it.
- AM squelch uses noise_sq plus the offset derived from the variant's 0x4F threshold. It no longer follows RSSI,
  which the gain loop holds near its target. The obsolete +20 dB adjustment from patch 0022 is removed.
- Leaving AM restores FM's gain word and the saved 0x4A; entering DMR also ends AM first. AM state and the loop
  remain behind CONFIG_AM_RX. The shared FD6818 driver and both existing board callers are kept consistent.

## Validation

- FD6818 host suite: upstream register sequences, gain attack/hold/recovery, limits, RSSI correction, squelch
  offsets and restoration to FM/DMR pass.
- Additional mock-receiver regression: 440 status polls across rising and falling full-gain readings verify gain
  bounds, an uninterrupted open AM squelch while gain changes, gain-corrected RSSI, restoration on exit and full
  sensitivity after tuning another AM channel.
- Radio core suite passes, including the 259-check listening-band suite, AM TX refusal and AM squelch using the
  board's noise independently of RSSI.
- FD6818 and listening-band suites also pass with AddressSanitizer and UndefinedBehaviorSanitizer.
- DM-32 target builds with AM enabled and disabled have no compiler warnings. Main target stack analysis passes.
- The generated derivative matches the patch series. No radio is flashed and no RF is emitted for these checks.

The source change's original author reports clean AM audio and gain-loop measurements on a DM-32. Those are
upstream results, not a hardware test of this derivative or the aircraft described by this user.

## Physical check still required

Compare a controlled weak and strong AM source on the same frequency, and verify that the noise squelch does not
chop speech while gain changes. Repeat after AM-to-FM-to-AM and AM-to-DMR-to-AM transitions. Check a real overflight
after installing the image. Record receiver readings and whether squelch-open monitoring changes the symptom.

The loop cannot guarantee reception if the antenna/front end is driven beyond its physical input range. This
Hotfix therefore must not be described as a confirmed cure of every strong-signal overload case.
