// SPDX-License-Identifier: GPL-3.0-or-later
// Closed-loop model of the FD6818 air band AM gain loop: where each version of the AM receiver settles, and how it
// answers a carrier that starts or ends. Analysis only, not firmware; it runs no radio and emits no RF.
//
// The loops are the real driver code: this file includes fd6818.c from the generated derivative tree, so the gain table
// and constants are the ones the firmware builds, and the current loop is whatever the series (0046) put there.
// old_step() is the loop of RadioFoundry ad79ab67 as the withdrawn backport 527f5229 shipped it (MIT, Copyright (c)
// 2026 The RadioFoundry Authors), kept verbatim for the comparison.
//
//   python3 tools/build_radiofoundry.py --prepare-only
//   T=build/radiofoundry-p25
//   cc -std=gnu99 -Wall -Wextra -I$T/drivers/rf/fd6818 -I$T/core -I$T/drivers -I$T/drivers/rf '-D__attribute__(x)=' -o build/agc_model experiments/am-agc/agc_model.c $T/drivers/rf/rfbus/rfbus.c $T/drivers/rf/subaudio/subaudio.c
//   build/agc_model
//
// (That cc command is one line. No -O: the project's host builds define __attribute__ away, which glibc's optimized
// headers don't accept.)
//
// The plant is the assumption to keep in mind: the 0x67 reading is the signal's reading at full gain ("full") minus 2
// counts per dB of gain the loop has taken off, limited to the register's 9 bits. A DM-32UV bench agrees with it near
// the settling point: four carriers 10 dB apart settled at 0x13 words 10 dB-ish apart and 0x67 137-142 (hardware-dm32.md
// 8.7). Far from it (a reading that clips at 511, the demodulator's own compression) it is a model, not a measurement.
// RadioFoundry reports the AM demodulator overdriven, audio distorted, when 0x67 sits at 148-158 and not at 129-143, so
// a drive over 143 counts as overdriven here. One step per status poll: RADIO_POLL_MS, 25 ms.
#include <stdio.h>
#include "fd6818.c"

#define POLL_MS 25
#define OVER 143		// the reading above which the demodulator is taken as overdriven
#define FULL_MAX 329		// the last full-gain reading the 93 dB of the gain table can bring down to OVER
#define FIXED_GAIN_DB 12	// the firmware before 0046: 0x13 = 0x037E in AM, -12 dB, no loop

static bool old_step(fd6818_agc_t *a, uint16_t r67)
{
	uint16_t raw = r67 & 0x1FFu;
	uint16_t avg = a->prev == AGC_NO_PREV ? raw : (uint16_t)((raw + a->prev) / 2u);
	a->prev = raw;
	int32_t db = ((int32_t)avg - (int32_t)FD6818_AGC_TARGET) / 2;
	uint8_t idx = a->idx;
	if (db >= 10) {
		int32_t want = agc_table[idx].db - (db - 8);
		while (idx > 0 && agc_table[idx].db > want) {
			idx--;
		}
	} else if (db < -6 && a->hold == 0 && idx < AGC_TOP) {
		idx++;
	}
	bool changed = idx != a->idx;
	a->idx = idx;
	if (changed || db >= -6) {
		a->hold = FD6818_AGC_HOLD;
	} else if (a->hold > 0) {
		a->hold--;
	}
	return changed;
}

typedef bool (*step_fn)(fd6818_agc_t *, uint16_t);

// One status poll: the reading at the gain in force (returned unclipped: that is what the demodulator sees), then the step
static int poll(fd6818_agc_t *a, step_fn step, int full)
{
	int drive = full - 2 * (int)fd6818_agc_atten_db(a);
	(void)step(a, (uint16_t)(drive < 0 ? 0 : drive > 511 ? 511 : drive));
	return drive;
}

// The worst drive in the last 200 of 4000 polls: a limit cycle shows as a spread
static int settled_max(step_fn step, int full)
{
	fd6818_agc_t a;
	fd6818_agc_reset(&a);
	int worst = 0;
	for (int k = 0; k < 4000; k++) {
		int d = poll(&a, step, full);
		if (k >= 3800 && d > worst) {
			worst = d;
		}
	}
	return worst;
}

static void settle(void)
{
	printf("Where a steady station ends up, for every full-gain reading the gain table can reach (144-%d):\n", FULL_MAX);
	printf("  %-38s %s\n", "build", "readings left over 143 (overdriven)");
	struct { const char *name; step_fn step; } loops[] = {
		{ "withdrawn loop (527f5229)", old_step }, { "current loop (0046)", fd6818_agc_step },
	};
	for (int i = 0; i < 2; i++) {
		int n = 0, first = -1, last = -1;
		for (int full = 144; full <= FULL_MAX; full++) {
			if (settled_max(loops[i].step, full) > OVER) {
				n++;
				last = full;
				if (first < 0) {
					first = full;
				}
			}
		}
		printf("  %-38s %3d of %d", loops[i].name, n, FULL_MAX - 143);
		if (n > 0) {
			printf("  (full-gain readings %d to %d)", first, last);
		}
		printf("\n");
	}
	int n = 0;
	for (int full = 144; full <= FULL_MAX; full++) {
		if (full - 2 * FIXED_GAIN_DB > OVER) {
			n++;
		}
	}
	printf("  %-38s %3d of %d  (any reading over %d)\n\n", "fixed 0x037E, no loop (before 0046)", n, FULL_MAX - 143,
	       OVER + 2 * FIXED_GAIN_DB);
}

// A carrier appears while the receiver sits at full gain (a tune, or an aircraft keys up), then ends
static void steps(step_fn step, const char *name)
{
	printf("%s: a carrier starts at full gain, then ends\n", name);
	printf("  full  cut needed  overdriven polls  gain back to <=12 dB off after the carrier ends\n");
	const int levels[] = { 180, 220, 280, 320 };
	for (unsigned i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
		int full = levels[i];
		fd6818_agc_t a;
		fd6818_agc_reset(&a);
		int over = 0;
		for (int k = 0; k < 4000; k++) {
			if (poll(&a, step, full) > OVER && k < 400) {
				over++;
			}
		}
		int cut = fd6818_agc_atten_db(&a), back = -1;
		for (int k = 0; k < 4000 && back < 0; k++) {
			(void)poll(&a, step, 140);	// an empty channel reads about 140 at full gain
			if (fd6818_agc_atten_db(&a) <= 12) {
				back = k + 1;
			}
		}
		printf("  %4d  %5d dB   %8d (of the first 400)   ", full, cut, over);
		if (back < 0) {
			printf("never\n");
		} else {
			printf("%d ms\n", back * POLL_MS);
		}
	}
	printf("\n");
}

int main(void)
{
	settle();
	steps(old_step, "withdrawn loop (527f5229)");
	steps(fd6818_agc_step, "current loop (0046)");
	return 0;
}
