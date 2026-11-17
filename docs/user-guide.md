# User guide: FM, DMR, P25, NXDN and P25 trunk channels

This firmware is experimental and **not verified on a radio** ([hil-checklist.md](hil-checklist.md)). Read this
before you install it:

- Keep a full backup of your radio, including its calibration ([recovery.md](recovery.md)).
- Transmit only where your license allows.
- Transmit on a trunked system only if its administrator authorized you and assigned your unit ID.
- Encrypted calls in any mode are muted. Nothing is ever decrypted.

## Channel modes

| Mode | Screen | What it is |
|---|---|---|
| FM | `FM` | Analog, as the stock radio |
| DMR | `DMR` | Tier II, both timeslots, as RadioFoundry |
| P25 | `P25` | P25 Phase 1 conventional: one frequency (or a repeater pair), a NAC, a talkgroup |
| NXDN | `NXDN` | NXDN48 conventional (6.25 kHz): a RAN and a group |
| P25 trunk | `P25T` | One P25 Phase 1 trunked system. The channel's frequency is the system's control channel; the radio moves to the voice channels by itself |

The radio transmits on 136-174 and 400-480 MHz. It also receives, never transmits, on 65-108 MHz (broadcast FM),
108-136 MHz (air band, AM) and 174-260, 350-400 and 480-520 MHz, with less sensitivity outside the front-end filters.
800 MHz trunked systems are out of its range.

## Aviation AM regression rollback

The AM change in version `527f5229` was withdrawn after a report of worse, crunchy/overloaded audio. The current
rollback restores the AM behavior from `b736e952`, retaining the DMR PTT fix, boot picture and four-mode VFO.
It does not establish a fix for the original close-aircraft distortion. See [the investigation](am-regression.md).

## Setting up channels

Use the codeplug editor or a CSV file. The radio's menu also edits NAC, RAN, talkgroup and unit ID. On the NAC or RAN row, OK with nothing typed switches RX
between the channel's own NAC/RAN and every NAC/RAN, keeping the TX one.

| Field (CSV column) | P25 | NXDN | P25 trunk |
|---|---|---|---|
| RX / TX frequency | the channel (TX: the repeater input) | the channel | RX: the control channel. TX: its inbound frequency |
| RX NAC / TX NAC | 000-FFF; RX `any` (F7E or F7F) hears every NAC | - | RX NAC: the control channel's, or `any` |
| RX RAN / TX RAN | - | 0-63; RX 0 (or `any`) hears every RAN | - |
| Talkgroup | 1-65535: where PTT talks, and what plays | 1-65519 | 1-65535: affiliated to, PTT talks to it |
| Hear All Groups | play every talkgroup | play every group | follow every group call on the system |
| WACN | - | - | 5 hex digits, from the system's administrator (or RadioReference); 0 hears any (receive only) |
| System ID | - | - | 3 hex digits, from the system's administrator (or RadioReference); 0 hears any (receive only) |
| Unit ID | the channel's ID, else the radio's DMR ID | the same; NXDN needs 1-65519 | the same: **the ID the administrator assigned you** |

## DMR repeaters with restricted access (RAS)

A repeater with RAS (restricted access to system) alters the checks of each call's header and embedded link control
with its key, so a radio without the key never sees a valid call and plays nothing. Turn on **digital monitor** (a
short press of the monitor key, SK1): a whole voice superframe in order on the channel's color code and timeslot then
plays as an unknown group call (talkgroup and ID 0), until a header or embedded LC that decodes names it. Calls marked
private (encrypted) still do not play. Without digital monitor the talkgroup can't be told, so nothing plays. Transmit
through such a repeater needs the system's key, which this firmware doesn't have.

## Air band channels from the stock codeplug

The installer's conversion of the original codeplug now keeps air band (and broadcast FM) channels, also those
without a TX frequency: they come over as receive only, FM (played in AM in the air band).

## Writing a DMR message

On a DMR channel, open **Contacts**, focus the person or talkgroup, press **#**, then **3 (Message)**.
Choose **Write...**. Keys **2-9** cycle letters; pause a second between letters on the same key. **0** enters
a space, **1** punctuation, and holding a digit enters that digit. **#** cycles uppercase, numbers, and lowercase.
**Back** deletes; on an empty message it returns to Compose. Hold **Back** to cancel and go Home.

The title shows the input mode and character count, up to **140 characters**. **OK** opens the review, with the
recipient in the title. **Up/Down** scroll the complete text, **Back** returns to editing, and **OK** sends.
Entering the review does not transmit. A refused send keeps the text available for another try; sent text is not
saved across power cycles.

The existing message-format setting chooses ETSI IP text, ETSI short data, or Motorola TMS. The channel supplies
the frequencies, timeslot, and color code; a recipient ID alone is not enough. A private message requests delivery
confirmation where supported; group messages have no confirmation. The new editor has passed host tests and the
DM-32 build, but sending typed text has not been checked on the physical radio.

## Using a P25 or NXDN channel

The screen shows:
- The PTT line with the talkgroup.
- The NAC or RAN.
- The caller's talkgroup and unit ID during a call.

Calls on another talkgroup, NAC or RAN, or encrypted calls, show but do not play.

**Digital monitor**, as on DMR: a short press of the monitor key (SK1) plays every talkgroup and every NAC (P25) or
RAN (NXDN) on this channel; the status row shows `DMon` and a toast confirms it. Another press, Back or a change of
channel ends it. A zone scan started with digital monitor on keeps it on every channel it scans and where it stops, so
a call on any talkgroup stops the scan. PTT still talks with the channel's own NAC/RAN and talkgroup, and the channel
is not changed. On a P25 trunk channel it plays every group of the system.

On P25, the speaker is muted between calls, including after power-on and after releasing PTT. A continuous hiss
after the hotspot has returned to standby is not an expected transmit tail.

PTT sends a call to the channel's talkgroup with your unit ID. The time-out timer applies. Busy channel lockout
refuses PTT while another NAC (RAN) is heard, or any traffic, as the setting says.

## Hotspots, repeaters and other radios

If the DM-32 hears a hotspot, repeater or radio but they don't hear it, check these first.

**NXDN is NXDN48 only.** That is the 6.25 kHz, 4800 bps mode: **"VN"** on Icom, "Very Narrow / 6.25 kHz" on
Kenwood. The DM-32 has no NXDN96 ("N", 12.5 kHz) mode, so the codeplug editor shows no bandwidth setting for NXDN.
Set the other radios and the repeater's channel to VN / NXDN48.

**The TX NAC or RAN must match.** RX can be set to `any` (NAC F7E, RAN 0), so you can hear a system whose NAC
or RAN is different from yours. The other side only accepts your **TX** NAC/RAN. Pi-Star / MMDVMHost defaults to
NAC 293 and RAN 1. Set a RAN on the radio's channel editor (RAN row, type 0-63) or in the editor/CSV's TX RAN column.

**Frequency error.** If the hotspot only works with an RX offset of a few hundred Hz, or DMR/P25 show a high BER,
correct the radio instead: **Settings › Advanced › Freq trim** (0.1 ppm steps). One ppm is about 145 Hz on 2 m and
440 Hz on 70 cm. It moves receive and transmit, FM, DMR, P25 and NXDN together. Then set the hotspot's offset
back to 0.

**MMDVM / Pi-Star.** The hotspot also drops a call when:
- `[P25] OverrideUIDCheck=0` and your unit ID is not in its DMR ID file. Set it to 1, or use your registered ID.
- `SelfOnly=1` under `[P25]` or `[NXDN]` and your unit ID is not the hotspot's own ID. For NXDN that is your
  NXDN ID (65519 or less), not your DMR ID.

**NXDN ID.** NXDN IDs go up to 65519. A channel without its own Unit ID uses your DMR ID, and a 7-digit DMR ID is
too large: PTT shows "NXDN ID above 65519". Put your NXDN ID in the channel's Unit ID.

The Pi-Star live log shows where a call stops. If nothing at all appears while you transmit, the hotspot does not
hear a valid signal. Report it with the radio's version, mode and frequency.

## Using a P25 trunk channel

| PTT line | Meaning |
|---|---|
| Searching for the site | No control channel with the channel's WACN and System ID is heard yet. Check the frequency, set RX NAC to `any`, and WACN/System ID to the system's (or 0 for any) |
| Listen only | On the system; Trunk TX is disabled for this channel, RX-only is set, or the firmware was built without trunk TX |
| Not registered | On the system; transmit is built in, but the system has not registered the radio (yet, or it refused) |
| TG n | Registered: PTT asks for a channel to talk to talkgroup n |
| Requesting a channel | PTT is down and the request is out |
| Queued by the system | The system will give a channel when one is free (at most 10 s) |

Group calls on the channel's talkgroup play, and so do all calls with Hear All Groups. The radio goes to the voice
channel and comes back to the control channel after the call.

Select **P25 Trunk** in the channel editor or the VFO mode cycle (**\* → 4**). The channel editor includes **WACN** (five hex digits) and **System ID** (three hex digits). Like NAC entry, number keys enter digits and the knob/arrow keys cycle the last digit through A–F. Zero accepts any system for receive.

Trunk TX starts disabled, including on existing channels. To enable it, enter the system's nonzero WACN/System ID, assigned unit ID and talkgroup, then enable **Trunk TX** in the channel editor or web CPS. The switch clears RX-only; disabling it restores RX-only. The release includes experimental trunk TX support, but hardware timing and interoperability remain unverified. AES trunk TX is unavailable.

PTT on a trunk channel with explicit TX permission works as follows:
- Hold PTT. The radio asks the system for a channel and talks when the system grants it. Not registered yet: PTT
  registers first, then asks for the channel.
- If the system refuses ("System refused") or doesn't answer ("System did not answer"), the screen shows it until you
  release PTT. "Searching for the site" means the radio is not on the channel's system: check WACN and System ID.
- The radio sends nothing without PTT, except its registration and its affiliation to the talkgroup. It sends each
  at most every 30 s, and only to the system the channel names.

Limits:

| What | Behavior |
|---|---|
| Phase 2 (TDMA) calls | Not heard: the radio cannot receive or send Phase 2 |
| Control channels | One per channel. For another site, use another channel |
| Call types | No emergency, private calls, data or supplementary services |
| Leaving the channel | De-registration is not sent; the system ages the registration out |

## DMR receive-end tone

**Settings › Sounds › DMR call end tone** controls the local beep after a received DMR call. It starts enabled. Key sounds and Talk Permit remain separate. This cannot remove a tone contained in a network's audio.

## AES-256 conventional voice

In the web CPS **Radio** tab, provision up to four AES-256 slots (64 hex digits per key), each with a unique key ID. Select the slot on a conventional P25 or DMR channel, in the web CPS or on the radio (channel menu, **AES-256** row: OK steps Off, then each slot that has a key); no slot keeps clear voice. Keys are only entered in the web CPS, never on the radio. DMR key IDs must fit 1–255; P25 accepts 1–65535. Keys stay out of CSV/YAML exports, but binary codeplug backups contain them. Keep backups private.

RX looks up the received key ID among the four slots. Missing encryption synchronization or a missing key mutes encrypted voice; TX with an invalid key/configuration is refused. Hardware interoperability is untested. Read the [protocol and sequence limits](aes256.md) before use, especially for DMR shared keys or after a settings/flash restore.
