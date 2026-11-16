# Aviation AM regression: withdrawal and rollback

## User observation

After installing the AM Hotfix `527f5229`, the user reports worse audio than before, described as crunchy and
overloaded. There is no controlled audio recording or receiver-register trace of this observation. The report
does not establish whether squelch 0 changes it.

The AM backport changed the filter, demodulator output/control, gain loop and squelch together. Host logic tests
and an upstream bench report did not establish that those combined settings improved audio on the affected radio.

## Mitigation

- Mark `v2026.11.15-527f5229` as a withdrawn prerelease and restore the previous stable release in the installer.
- Revert the entire `0028-am-strong-signal-agc.patch` backport. Do not guess another combination of unmeasured AM
  register settings for a public release.
- Keep the existing DMR PTT/abort correction, custom boot picture, and FM/DMR/P25/NXDN VFO.
- The rollback's receiver, UI and digital-voice source files match `b736e952`; only documentation and the build
  identity differ. This restores the previous AM behavior, including its original limitations.

## Diagnostic model, not an RF/audio measurement

The withdrawn gain loop averages consecutive RSSI values without accounting for a gain change between them.
For example, a reading of 222 reduces gain by 32 dB; an immediate next reading of 142 is averaged with 222 and
causes another 13 dB reduction. The previous host test explicitly expected that extra cut rather than checking
the closed-loop receiver response.

A simple host model using the withdrawn driver and an RSSI that follows applied gain starts with a full-gain
reading of 260 for 20 polls (500 ms at the core's 25 ms period), then drops to 150. The loop has removed 79 dB
after the strong interval and needs 882 more polls (22.050 s) to return to at most 12 dB attenuation. The model
includes the loop's bounds and hold, and uses no hardware or foreign firmware. It demonstrates an undesirable
response to a signal step; it does not prove that this explains the reported crunchy/overloaded audio. The new
filter, AF control or hardware overload remain possible contributors.

## Verification and next physical check

Verify the generated tree against the patch series and its code against `b736e952`; build the target and check
its compiler/stack audits. Run receiver and radio-core regressions. Verify the public installer serves the stable
rollback image with the same checksum as its release.

Before recommending a new AM change, compare the same source, frequency, volume and antenna on the previous
and candidate versions. Record whether squelch 0 removes the cuts, the RF/noise and gain readings, and the audio
at controlled weak and strong levels. Change one receive component at a time. No aircraft-band RF is emitted
and no radio is flashed as part of this rollback task.
