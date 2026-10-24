# Review: mh4u-port-study.md

Reviewer: DsvMh4uReview. Target (read only): `.tmp/reports-mhgu/mh4u-port-study.md` (244 lines, has the
"Verdict in short" 5 answers and Q1-Q5). Platform reference: `.tmp/ext/forge-pc/include/forge_pc.h`
(ForgeApi 0.7), forge-pc README/src/loader.cpp, SUYU-GURecomp src (export/loader/a32recomp), and the
local MHGU exefs extract `.tmp/mhgu/extract/exefs-0eb5`.

## Verdict

**AGREE with the study's triage; DOWNGRADE the confidence on four of its eight checked claims.**
The study's overall answer is right: the mod is **not usable as-is** on the native export (romfs assets can
be dropped in, but the code half never activates), a **Forge PC port is feasible** with the 0.7 API (hooks,
`mem_alloc`, `mem_write`, JIT-run guest code), and a port is **not impossible** — no hard blocker was found
in `forge_pc.h`. The port is large (113 hook sites, ~40 code immediates, 11 tables, the ARM32 module on the
JIT) and has three go/no-go experiments the study correctly leaves open (literal loads, main base,
mem_alloc/JIT cost).

The corrections that matter for planning:
1. The build has **28** item `movw` bound sites, not 26, so **~40** code-immediate records, not ~38 (Q2/Q5).
2. Q5 says **12** relocated tables; the verdict line already says 11. The right count is **11** (8 registry
   tables including `reg_mapicon` + 3 Notes tables); adding "the map-icon size class" double-counts.
3. **M9 contradicts `forge_pc.h` and the study's own `util.c` bullet**: SVCs are serviced only when guest
   code runs on the emulated CPU; a system call inside `forge_api->call` makes the call fail. The bootstrap
   plan ("run `module_init` from `forge_onInit`") therefore fails at the first `logf`. Stub/remove the SVC in
   the loaded blob, or enter the module through `FORGE_HOOK_JUMP` instead of `api->call`.
4. **M1's evidence is wrong, its conclusion is right.** `game_export.cpp:2091-2094` lists
   `rtld, main, subsdk0..subsdk9, sdk` (and the vector is unused), and `deconstructed_rom_directory.cpp:191-193`
   includes `subsdk1`; the reason mod-supplied exefs is ignored is that the standalone export loads
   `exefs/main` through `loader.cpp:312` with the default `override_update = false`, so
   `deconstructed_rom_directory.cpp:167-171` never calls `PatchExeFS`. MHGU's own exefs has only
   `main/rtld/sdk/subsdk0` (local extract), which is why the mod's `subsdk1` has no slot.

## Confirmed

Method: the 4 mandated files were re-read in full from the repo (README.md, module/main.c, module/link.ld,
module/build.py), plus 6 more cited files (module/core.toml, allocations.toml, module/port.c,
installer/src/core.rs, notes/porting-guide.md, module/util.c) and module/gear_hooks.txt; all 8 checks
below use the raw file text, not the study's quotes. `raw.githubusercontent.com` is blocked from this box;
the repo was read through `https://ghproxy.net/...`, the same mirror the study used. The local
`.`/`G:/omp works` sources (forge_pc.h, loader.cpp, game_export.cpp, a32disc.h, svc.h, deconstructed loader)
were read for the platform side.

### The 8 claims

1. **Verdict (4) triage — romfs works as-is, code half needs a Forge port, exefs/IPs mechanisms impossible.
   AGREE / PASS.** `forge_pc.h:6-10` (game code is native x64; patching ARM bytes does nothing),
   `forge_pc.h:229-230` ("changing code bytes in guest memory does NOT change the recompiled game: use
   hooks for code"), forge-pc README.md:36-42 (romfs file mods work; byte patches of code have no effect).
   The exefs/IPs side is true for the *shipped standalone export*: `loader.cpp:312` constructs
   `AppLoader_DeconstructedRomDirectory` with the default `override_update = false`;
   `deconstructed_rom_directory.cpp:167-171` calls `PatchExeFS` only when `override_update` is true; the
   export boots `exefs/main` (`suyu.cpp:683-692`). So mods/<title>/<mod>/exefs (subsdk1, .ips) is never
   layered in. Wording nit: "impossible" means "not through this export's mod folder", not "the platform
   cannot" — `PatchManager::PatchExeFS` exists and an NCA boot path would use it
   (`patch_manager.cpp:334-363`, `nca.cpp:83`).

2. **Loading mechanism (Q1): subsdk1 is a normal rtld-relocated dynamic module; IPS32 patches main; both
   install into exefs/; data files in romfs/nativeNX/mh4uport. AGREE / PASS.**
   `link.ld:1-3,15-32` (linked at 0, `.dynsym/.dynstr/.hash/.rel.dyn/.rel.plt/.dynamic`, "relocated by
   rtld"); `build.py:404-407` (`ld.lld -shared -T link.ld -init=module_init`, `nso.elf2nso`);
   `build.py:414-430` (IPS32 records at `site + 0x100`, `ldr pc,[pc,#-4]` + absolute trampoline);
   `build.py:443-450` (copies `subsdk1` + `<MAIN_BUILD_ID>.ips` into `<mod>/exefs/`); `main.c:29-46`
   (`module_init` copies manifests, registers classes, redirects the three data words);
   `core.toml:1-6` (core = exefs module + every data file). Minor wording: the ELF is linked at 0 and the
   `MODULE_BASE 0x2E19000` constant (`build.py:44`) is only the load address assumed by the absolute IPS
   trampoline words, not a link address; the M1 row's "relocated by rtld at a fixed 0x2E19000" states an
   assumption as a fact.

3. **Hook and table counts (Q2): 65 core + 45 gear_hooks + 3 obtained = 113 sites; 16 literal rewrites;
   REPLACE_HOOKS correct. AGREE / PASS.** `build.py:52-111` holds 65 tuples (counted; the study's own
   grouped table lists all of them); `gear_hooks.txt` has 45 lines (14 classes x 3 = 42 + `gear_w_create`,
   `gear_w_process`, `gear_kireaji`); `build.py:177-192` adds `obtained_set/clear/test` when
   `ITEMS_APPENDED > 0`, and Dalamadur's `items = [2991, 3020]` (allocations.toml) makes 30 appended, so a
   full build registers 113. `LIT_PATCHES` has 16 entries (`build.py:138-151`); `REPLACE_HOOKS =
   {'name_index'}` (`build.py:119`). The study also correctly flags the repo defect that `stats_hunted` is
   after `#` on the `stats_add` line (`build.py:59`) and is never registered.

4. **Immediate-site count (Q2/Q5): "~38 = 2 registry cmp + 7 Notes cmp + 26 item movw + 1 array size +
   2 stack-frame". DOWNGRADE / FAIL.** `ITEM_BOUND_SITES` (`build.py:181-190`) has **28** entries
   (11 + 10 + 1 + 6), every one asserted to be a `movw #0xbad/0xbae/0xbaf`; with the array-size and frame
   words that is 31 item records, so the total is **40**, not ~38 (the "26 movw" appears twice, Q2 and Q5).
   Planning consequence: two more immediate sites, and the study's own "some need an override of the whole
   function" is the honest part — a pre-hook at an enclosing block start cannot generally change a later
   `cmp/movw` immediate; those sites need a copied/adapted guest block or a function override, which is the
   riskiest part of the port and is under-specified.

5. **Table count (Q5): "12 tables = 8 registry + 3 Notes + the map-icon size class". DOWNGRADE / FAIL.**
   `REG_TABLES` (`build.py:121-133`) has 8 entries and **includes `reg_mapicon`**; `NOTES_TABLES` has 2
   (`notes_unlock`, `notes_order`) plus the `notes_list` table built by `notes_init` (`main.c:44-45`) = 3
   Notes tables. Total **11**. The study's own verdict line (`study.md:23`) says 11, so Q5 contradicts the
   summary and double-counts the map-icon table.

6. **M2: Forge `hook_create` at a block start plus `FORGE_HOOK_CONTINUE` is semantically the same as the
   displaced-instruction trampoline, so the 113 sites are mechanically 1:1. AGREE / PASS.**
   `forge_pc.h:196-204` (hook at a recompiled block start; pre runs on entry; CONTINUE runs the function),
   `forge_pc.h:134-142` (pre-hook arguments/return), `forge_pc.h:139-142,150` (`FORGE_HOOK_JUMP` continues at
   `cpu->r[15]` with registers as left), `build.py:348-372` (trampoline pushes r0-r5,r12,lr + d0-d7, calls
   `name_c`, returns saved r0 on nonzero, else replays the two instructions and jumps to `site+8`). For the
   post-call site 0x3a75fc, `a32disc.h:135-142,202-207,345-347` inserts `pc+4` after a call as a block start
   (soft root), so `is_block` should accept it unless the word was marked data. Improvement the study misses:
   the cleanest 1:1 is a pre-hook that sets `cpu->r[15] = module_base + <name>_tramp` and returns
   `FORGE_HOOK_JUMP` — the module's own trampoline then does the register save, `name_c` call, replay and
   return, with SVCs serviced normally. That is simpler than "113 thin shims that marshal ForgeCpu* <-> regs[]"
   and avoids problem 7.

7. **M9: "Debug logging via svc works (or replace with api->log)". DOWNGRADE / FAIL.**
   `forge_pc.h:207-215` says `forge_api->call` is only for functions that never wait: "a system call inside
   makes the call fail (returns false, registers restored, memory changes stay)". `module/util.c:28-35` ends
   `logf` in `svc_output_debug_string`, and `main.c:31` is the first thing `module_init` does, so the study's
   own bootstrap step "run `module_init` from `forge_onInit`" fails immediately if it uses `api->call`. SVC
   0x27 is implemented and serviced when code runs on the emulated CPU (`svc.h:424`,
   `svc_debug_string.cpp:73-79`, `arm_recomp32.cpp:1273-1301`), which is why M9 is right only for the
   JIT/dispatcher path, not for `api->call`. The study's earlier `util.c` bullet states the correct rule;
   M9 contradicts it. Fix: say "SVC works only on the emulated CPU; via `api->call` it aborts; stub `logf`
   or enter through `FORGE_HOOK_JUMP`". This is the most consequential error in the study.

8. **M1 evidence: "the export recompiles a fixed set of exefs modules (main, rtld, sdk, subsdk0); there is
   no path that loads an extra ARM32 module". DOWNGRADE / FAIL as written (conclusion PASS).**
   `game_export.cpp:2091-2094` lists `rtld, main, subsdk0..subsdk9, sdk`; the loop at 2095+ calls
   `AnalyzeNsoFile` on every NSO present, and `module_names` is unused (grep finds only the declaration).
   `deconstructed_rom_directory.cpp:191-193` also includes `subsdk1`, so the loader *would* load one if it
   were in the exefs. The actual reasons the mod's module is not loaded: (a) the standalone export boots the
   deconstructed path with `override_update = false` (`loader.cpp:312`), so `PatchExeFS` never layers
   mod exefs (`deconstructed_rom_directory.cpp:167-171`); (b) MHGU's exefs contains only
   `main/rtld/sdk/subsdk0` (local `.tmp/mhgu/extract/exefs-0eb5`), so there is no `subsdk1` slot in the
   export's recompiled set. The conclusion "cannot be installed as a mod" is correct; "no path loads an
   extra ARM32 module" is too strong (a manual edit of the export's own `exefs/` would make the generic
   loader load it; that is untested, shifts the module index used by the recompiled base table, and still
   does not apply the IPS). Replace the evidence.

### Other checks (not in the 8)

- **stats_hunted defect — AGREE / PASS.** `build.py:59` really has the tuple after `#`; `main.c:178` has
  `stats_hunted_c`, so the handler is dead. The study flags it in Q2.
- **M3 literal loads — AGREE / PASS as an explicit inference.** `forge_pc.h` does not settle whether the
  lifter re-reads literal-pool words; the study puts it in Unknown with a test. `build.py:138-151` has the
  16 entries and `build.py:422-426` rewrites them. `mem_write` can reach guest memory; whether the
  recompiled block reads it is genuinely unproven.
- **M6 relinking — AGREE / PASS.** `link.ld:15-32` relies on `.rel.dyn/.rel.plt`; a flat `mem_alloc`
  blob must be relocated or linked at the allocation address. Correct.
- **M7 nn::fs — AGREE / PASS (as an inference).** `port.c:20-33,94-135,149-165` uses the main stubs; the
  host-side fopen + mem_write route is robust and the study marks the guest-HLE route as unknown.
- **M8 romfs — AGREE / PASS.** forge-pc README.md:36-42: file mods work as they are; files are applied at
  game start.
- **Licence — AGREE / PASS.** README.md is GPL-3.0-or-later; a port reusing the module code must be GPL and
  a separate mod. The study's last paragraph says this correctly.
- **Q1 install paths — AGREE / PASS.** `installer/src/core.rs` (`TITLE_ID`, `Layout::ModFolder` /
  `Layout::Atmosphere`, `rel_dest`): ModFolder `<dir>/<mod>/{exefs,romfs}` and Atmosphere
  `atmosphere/contents/<title>/{exefs,romfs}` + `atmosphere/exefs_patches/<mod>/*.ips`.
- **The 5 answers are present and complete** (study.md:15-24): load mechanism, hook count, data/code split,
  native-export mapping, port size. The triage is in the verdict sentence and the Q4 table rather than in a
  single word label ("works / needs port / impossible"), but the content is the same.

### Copied GPL code in the study

No C function bodies or data tables were copied. The study does reproduce **verbatim GPL-3.0 comments /
prose** from the target repo and from SUYU-GURecomp:
- `module/link.ld:1-3` header ("subsdk1 layout: R-X text ... Linked at 0, relocated by rtld") quoted in Q1;
- `module/core.toml:1` ("the exefs module (subsdk1 + IPS) and every registered monster's data file ...")
  quoted in Q1;
- `notes/porting-guide.md` sentences in Q3 ("A copy of uFestaEnemy's 286-slot vtable ... AArch32 ...");
- `a32disc.h` "execution continues after a call" in M2.

These are short review quotes, acceptable in `.tmp`, but they must not be merged into MIT docs or code:
paraphrase them, keep only facts/addresses, and keep the GPL conclusion (a code-reusing port stays
GPL-3.0-or-later and a separate mod). The MIT quotes from `forge_pc.h` / forge-pc README are fine with
attribution.

## In progress

Nothing. All 5 study answers are present and the 8 claims above are resolved.

## Unknown

- The study's open experiments remain genuinely open: whether the recompiled main re-reads literal-pool
  words (M3), whether JIT guest code can call main's recompiled `nn::fs` stubs (M7), whether
  `mem_getModuleBase("main")` is 0x704000 in this export, and whether `mem_alloc` is large enough for the
  code blob + 256 KB data pool. This review did not run any experiment and did not build anything.
- Whether a manual addition of `subsdk1` to the export's own `exefs/` loads cleanly (module index shift vs
  the recompiled base table) is untested and is not a distribution path; it does not change the verdict.
- No measurement of JIT cost for the monster class (the study flags this too).
