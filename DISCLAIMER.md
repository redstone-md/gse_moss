# Disclaimer & legal notice

**This is not legal advice.** This document records the intent and terms under
which this software is published. If you need certainty about your situation,
consult a qualified lawyer in your jurisdiction.

## What this project is

This project is a community **fork of the Goldberg Steam Emulator** (originally by
Mr_Goldberg, and of the `gbe_fork` community fork), extended with a peer-to-peer
networking transport built on the [moss](https://github.com/redstone-md/moss)
library. It re-implements a subset of a third-party networking/lobby API so that
software written against that API can run and communicate **without a running
Steam client**, primarily for:

- interoperability, testing and research,
- offline / LAN / private play of software the user already legally owns,
- development and debugging of multiplayer code.

It ships **no games, no game assets, and no copyrighted third-party content.** It
does not download, unlock, decrypt, or otherwise provide access to any software
you do not already possess.

## No affiliation / trademarks

This project is **not affiliated with, authorized, sponsored, or endorsed by Valve
Corporation** or any game publisher. "Steam", "Steamworks", and related names and
logos are trademarks and/or registered trademarks of Valve Corporation. Any other
product and company names are the property of their respective owners. They are
used here only for identification and interoperability purposes (nominative use).

This project does **not** connect to, authenticate against, impersonate, or relay
traffic to or from Valve's Steam network or servers. All networking it performs is
peer-to-peer between users who choose to run it.

## Your responsibilities

By downloading, building, or using this software you agree that:

- You will use it **only with software you have the legal right to use**, and in
  compliance with all applicable laws and with the license terms of any software
  you run alongside it.
- You are **solely responsible** for how you use it. The authors and contributors
  do not encourage, assist, or condone copyright infringement, circumvention of
  technological protection measures, or any other unlawful activity.
- Some jurisdictions restrict circumventing DRM or reverse engineering even for
  interoperability. **You are responsible for knowing and following the law where
  you live.**

## No warranty

This software is provided **"AS IS", without warranty of any kind**, express or
implied, including but not limited to the warranties of merchantability, fitness
for a particular purpose and non-infringement. In no event shall the authors or
copyright holders be liable for any claim, damages, or other liability arising
from, out of, or in connection with the software or its use. See the full terms in
[`LICENSE`](./LICENSE).

## Licensing & attribution

- The emulator code is distributed under the **GNU LGPL-3.0** (inherited from the
  Goldberg Emulator). See [`LICENSE`](./LICENSE).
- The bundled **moss** runtime (`third-party/moss/`) is distributed under the
  **MIT License** by its authors; see its upstream repository for the full text.
- Third-party libraries retain their own licenses; see [`CREDITS.md`](./CREDITS.md).

If you redistribute this project or builds of it, keep these notices intact and
comply with each component's license.

## Takedown / contact

If you are a rights holder and believe something in **this repository** infringes
your rights, please open an issue or contact the maintainer so it can be reviewed
and addressed.
