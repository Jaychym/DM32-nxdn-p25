<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Boot picture

`avatar.png` is the unmodified image supplied by the maintainer for the boot screen on November 15, 2026.
SHA-256: `da4f1e55c237cd0b887bbfb8db1b2819912a01b593f5dea5263410d15c87dcb2`.

`python3 tools/gen_splash.py` uses Pillow to produce the 132 x 132 pixel, 11-color RGB565 picture in patch 0024.
The other five palette entries remain available for the screen's background, text, accent, rule, and footer.
The PNG is an input asset; no generated or modified replacement was used.
