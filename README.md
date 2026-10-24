# MH4U-native

Port of [Scrimas/MH4U-port-to-MHGU](https://github.com/Scrimas/MH4U-port-to-MHGU) (MH4U monsters in
MHGU 1.4.0) to the native PC build of MHGU produced by SUYU-GURecomp, as a Forge PC plugin.

The upstream mod ships as an ExeFS `subsdk1` module plus an IPS patch of `main`. Neither mechanism works on
the native export: its game code is recompiled x64, so code-byte patches have no effect, and it does not layer
mod ExeFS. The romfs half (monster/quest/stage/gear arcs, BGM, per-monster data) works unchanged. This repo
carries the code half over as a Forge plugin: hooks instead of IPS branch records, and the upstream ARM32
module injected into guest memory where it runs on the export's JIT.

Status: planning. See `docs/port-study.md` (feasibility study) and `docs/port-study-review.md` (its review;
the review's corrections take precedence).

## License
GPL-3.0-or-later (`LICENSE`), same as upstream. Upstream copyright notices are kept on anything absorbed
from it; see `NOTICE.md`.

Monster Hunter is a trademark of Capcom. Not affiliated with Capcom or Nintendo. No game files are included.
