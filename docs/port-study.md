# MH4U-port-to-MHGU on the native PC recompile: study

Target: https://github.com/Scrimas/MH4U-port-to-MHGU, GPL-3.0-or-later (README.md:34), MHGU Switch 1.4.
Reference for the target platform: `G:/omp works/.tmp/ext/forge-pc/include/forge_pc.h` (ForgeApi 0.7),
`G:/omp works/.tmp/ext/forge-pc/README.md`, `G:/omp works/.tmp/ext/forge-pc/src/loader.cpp`,
`G:/omp works/.tmp/ext/SUYU-GURecomp/src/...`.

Access note: github.com and raw.githubusercontent.com are unreachable from this machine (ECONNREFUSED /
ECONNRESET), so every repo file was read through the ghproxy.net raw mirror and the GitHub tree API was used for
file sizes. Citations `path:line` are repo-relative; line numbers come from the raw file text (pinned by grep).
Only `README.md`, `module/main.c`, `module/link.ld`, `module/build.py`, `module/core.toml`, `module/port.c`,
`module/util.c`, `module/game.h`, `module/gear_hooks.txt`, `allocations.toml`, `notes/porting-guide.md` and
`installer/src/core.rs` were opened (12 files).

**Verdict in short.** (1) The mod loads as an ExeFS `subsdk1` module relocated by rtld plus an IPS32 patch of main
whose branch records are the hooks; (2) it hooks 65 sites listed in build.py (shared, per-monster and diagnostic
handlers) plus 45 generated table-load exits plus 3 item hooks (113 sites, offsets listed below) and also rewrites
16 literal words and ~38 code immediates; (3) its
romfs half is plain extra files (monster/quest/stage/gear arcs, BGM and one data file per monster) while the code
half is registry/Notes table relocation, table appends, quest-board renames and an entire new enemy class; (4) on the
native export the romfs half works as-is, the whole code half needs a Forge PC plugin port and the exefs-module and
IPS mechanisms themselves are impossible there; (5) the port is large: ~113 Forge hooks, ~38 immediate sites that
need hand-written replacements, 11 relocated tables, and the entire AArch32 module (core ~60 KB plus per-monster
behaviour code, one file alone 165 KB) still injected into guest memory to run on the export's JIT.

---

## Confirmed

### 1. How the mod is loaded (question 1)

There is no single mechanism: the release ships **one exefs module plus one IPS patch of the game's main NSO**, and
the two only work together.

- The "core" package is defined as "the exefs module (subsdk1 + IPS) and every registered monster's data file,
  always installed together" (module/core.toml:1). Version 1.3.0 (module/core.toml:6).
- `module/build.py` builds `subsdk1` from ARM32 C with `clang --target=armv7a-none-eabihf -marm -fPIC`, links it
  with `ld.lld -shared -T link.ld -init=module_init --hash-style=sysv -z norelro` and converts the ELF to an NSO
  (module/build.py:312-314; 404-407: `-init=module_init`, `nso.elf2nso`). It also writes
  `<MAIN_BUILD_ID>.ips` with `MAIN_BUILD_ID = 'FB08F1D20FD1204FE7070672777F869B'` (module/build.py:43, 430).
- `install()` copies exactly `subsdk1` and `<MAIN_BUILD_ID>.ips` into `<mod>/exefs/` and the monster data files into
  `<mod>/romfs/nativeNX/mh4uport/`, plus the selected monsters' asset files under `romfs/`
  (module/build.py:443-479).
- The module is a normal Atmosphère-style dynamic module: "subsdk1 layout: R-X text (crt0 first: rtld reads the MOD0
  offset at base+4), R-- rodata + dynamic linking tables, RW- data + bss. Linked at 0, relocated by rtld."
  (module/link.ld:1-3), with `.dynsym/.dynstr/.hash/.rel.dyn/.rel.plt` and `.dynamic` (module/link.ld:16-32).
- It expects a fixed load address: `MODULE_BASE = 0x2E19000   # subsdk1 lands where sdk was (Ryujinx log: image 3)`
  (module/build.py:44). The IPS patch therefore stores **absolute** trampoline addresses.
- The installer supports two layouts only: the emulator mod folder `<dir>/<mod name>/{exefs,romfs}` and an
  Atmosphère SD card `atmosphere/contents/<title>/{exefs,romfs}` with the IPS going to
  `atmosphere/exefs_patches/<mod name>/*.ips` (installer/src/core.rs:44-49, 84-92; title 0100770008DD8000,
  installer/src/core.rs:11).
- Hooks are **not** installed by the module at runtime: for each hook site the IPS record at `site + 0x100` writes
  8 bytes `ldr pc, [pc, #-4]` + `.word trampoline` (module/build.py:414-421). `site + 0x100` is the NSO image header
  offset, "as in Atmosphere/Ryujinx" (module/build.py:414).
- Each trampoline is generated ARM32 in the module: `push {r0-r5, r12, lr}`, `vpush {d0-d7}`, `add r0, sp, #64`,
  `bl <name>_c`, `cmp r0, #0`, `vpop`, `pop`, `bxne lr`, then the **two displaced instructions** of the hook site
  and `ldr pc, [pc, #-4]` + the absolute return address `MAIN_BASE + site + 8` (module/build.py:348-372). Displaced
  instructions may not be pc-relative or branches, and the build aborts if anything branches into `site + 4`
  (module/build.py:324-346, 379-381).
- Handlers are plain C with a register convention: `int name_c(uint32_t *regs)` where `regs` = saved r0-r5, r12, lr
  and stack arguments start at `regs[8]`; returning 0 runs the hooked function with the (possibly edited) registers,
  returning nonzero returns to the caller with `regs[0]` (module/main.c:1-3; notes/porting-guide.md:89-91).
  `REPLACE_HOOKS = {'name_index'}` marks hooks that never replay the displaced instructions
  (module/build.py:117-119).
- The module entry `module_init` runs at load and does the data-side setup: for every registered monster it copies a
  donor's 51-word file manifest, calls the monster's class registration, then redirects three GOT-like data words so
  the game reads the module's extended tables (module/main.c:29-48). `MAIN_BASE` is hardcoded 0x704000 and all
  addresses are `MAIN_BASE + main offset` (module/game.h:6-9; notes/porting-guide.md:4).

So: **exefs module (subsdk1, rtld-relocated, fixed base) + IPS patch of main** whose branches are the actual hook
mechanism; there is no code cave in main and no runtime patching library.

### 2. What it hooks, and how many (question 2)

The build registers **65 hook sites** in `HOOKS` (module/build.py:52-111), **45 more** from the generated
`module/gear_hooks.txt` (module/build.py:112-116; the file lists 14 weapon classes x 3 tables = 42 plus
`gear_w_create`, `gear_w_process`, `gear_kireaji`), and **3 more** (`obtained_set/clear/test`) whenever items are
appended past GU's 2991 (module/build.py:177-192) — Dalamadur does append items (allocations.toml:166, 201), so a
full build registers **113 hook sites**.

Core sites (module/build.py:53-110), grouped:

| purpose | handlers and main offsets |
|---|---|
| spawn / registry / files | `em_create` 0x4EBCC, `quest_prep` 0x68A70, `em_arc_request` 0x15334C, `em_record` 0x69C258, `file_id` 0x152960 |
| save / tally | `stats_add` 0x3A75FC, `save_hunts` 0x523104 (`stats_hunted` 0x3A3930 is **commented out**, see below) |
| quest list | `quest_group_fix` 0x3B7DE8, `quest_unlocked` 0x3B2174, `quest_clear_fix` 0x388D40 |
| names / notes | `name_index` 0x55BDA4 (replacement hook), `notes_unlock_check` 0x554DA4 |
| items / gear | `item_data_loaded` 0x19237C, `item_text_loaded` 0xB83C48, `bui_record` 0x3A3874, `gear_pwl` 0x272910, `gear_armor_series` 0x129994, `gear_armor_create` 0x4BFEE0, `gear_armor_process` 0x4C0360, `gear_skill_type` 0x4CEE64, `gear_skill` 0x4CE98C, `gear_deco` 0x191A20, `gear_deco_create` 0x4C0CB0, `gear_has_skill` 0xE0DA0 |
| BGM | `bgm_select` 0x4E60D4, `bgm_stqr` 0xB6D72C, `bgm_play` 0x4E34A4 |
| stage / arena | `stage_path` 0x15339C, `stage_maprec` 0x61C394, `stage_wholerec` 0x61B608, `stage_gimmicks` 0x50249C, `stage_spear_count` 0x502694, `stage_siege_phase` 0x3A5EC4, `stage_dragonator_fire` 0x503BB0, `stage_cart_request` 0x497040, `stage_cart_avail` 0x495B40, `stage_cart_tick` 0x495D0C, `stage_frame` 0x503C7C, `stage_kobj_bcast` 0x500D18, `stage_cannon_ready` 0x503B88, `stage_dragonator_post` 0x503A5C, `stage_cannon_shot` 0x46365C, `stage_shell_dtor` 0x4A18B4, `stage_kobj_extra` 0x4FDF7C, `stage_cleanup` 0x4FF074 |
| diagnostics / damage | `dmg_sub` 0x98AF4, `dmg_special` 0x98AC4, `set_hp` 0x9A764, `add_hp` 0x9A7B8, `dmg_recv` 0x9D6F8, `set_action` 0x76154, `set_target` 0xC6E14 |
| safety | `utf8_null_guard` 0x7CA340 |
| Tigerstripe (em067 variant 1) | `em067_01_act` 0xF78F98, `em067_01_post` 0x48C4F0, `em067_01_ball` 0xF72700, `em067_01_ball43` 0xF735C0, `em067_01_ball42` 0xF73870, `em067_01_end` 0xCA118, `em067_01_end7` 0xCA10C, `em067_01_amove` 0x8F004, `em067_01_atransfer` 0xB5D6C |
| Dalamadur | `dala_dmg_end` 0x98E44, `dala_heat_zone` 0x6A160, `dala_map_pos` 0xC09C8 |

Repo defect worth knowing: the `stats_hunted` tuple sits after `#` on the `stats_add` line, so it is a comment and
never registered (module/build.py:59), even though `stats_hunted_c` exists (module/main.c:178).

Besides hooks the build also patches the game image in two non-branch ways:

- **Literal-pool rewrites (16)**: `LIT_PATCHES` lists (literal address, the pc-relative `add`/`ldr` using it, table
  symbol) — 14 sites for six of the relocated registry tables (`reg_pairs` 6, `reg_dti`, `reg_cro` 4, `reg_flags`,
  `reg_icon`, `reg_u16`) and 2 for the Hunter's Notes tables (module/build.py:138-149, 151) — and the record rewrites
  the literal word to point at the module's copy (module/build.py:422-428).
- **Immediate rewrites**: 2 `cmp rN, #137` -> `#137+N` registry bounds and 7 Hunter's Notes loop bounds
  (`WORD_PATCHES`, module/build.py:164-175), plus item-count bounds (`movw rX, #0xbad/0xbae/0xbaf` at 26 sites, the
  item-sort array size and the `sub/add sp` frame) in `item_patches` (module/build.py:177-192, 204-236).

`module_init` additionally writes 3 data words by value: file manifest 0x1839618, map icon size 0x183fff0, Hunter's
Notes 0x183ec4c, each through `redirect` (module/main.c:42-45, 15-24).

### 3. Data: romfs vs code (question 3)

**RomFS (shipped files, no code):**
- Per-monster data file `romfs/nativeNX/mh4uport/<name>.bin` (tables + text; sections ITEM, BUI, TBLA, TXTA, NOTE,
  NAME, SKIL, PWLF, FILE), installed for every registered monster (module/build.py:449-456;
  notes/porting-guide.md:97-100). Gogmazios' file is about 37.6 KB (module/port.c:27).
- Monster arcs `nativeNX/arc/enemy/emNNN_VV.arc` with their 0xCC-byte / 51-field file manifests
  (notes/porting-guide.md:80-85); the arc's presence is what marks a monster "installed" (module/port.c:151-165).
- The monster's other assets: BGM arcs, quest arcs, stage-alias arcs, map UI, gear models, weapon sounds
  (notes/porting-guide.md:92-96; allocations.toml:47-55, 60-88).
- Nothing replaces a GU file: the installer refuses two packages shipping the same RomFS path, and the port patches
  loaded resources instead (notes/porting-guide.md:94-96; module/build.py:460-466).

**Code / in-memory only (no file):**
- Registry relocation: 8 copies of GU's per-index tables (id<->index pairs, class DTI, "roEmNNN.cro" names, file
  manifests, flags, UI icon, u16, map icon size class) grown by one entry per monster, with the data words that
  address them repointed (module/build.py:120-133; module/main.c:15-47).
- Hunter's Notes: three 123-entry tables copied and grown, seven loop bounds patched, five save bits used
  (notes/porting-guide.md:260-264; module/build.py:148-157).
- Items / gear: appended records are pushed into the game's loaded tables by hooking the table load exits and
  running the game's own parsers (module/build.py:87-93, 112-116; notes/porting-guide.md:223-235).
- Quests: DUMMY `quest_group` entries renamed in memory, unlock and clear hooks (module/build.py:66-68;
  allocations.toml:19-30).
- Stage aliasing: while a ported quest runs, arc path kinds are redirected to another folder, the quest-start
  KowareObj command is held back, and area/whole map records are replaced (module/build.py:63-65, 84;
  allocations.toml:54, 165, 238).
- Save tally remap to unused registry slots 105..111, break-reward record, BGM selection (module/build.py:59-60,
  78-80, 89).
- **The monster class itself**: a DTI + a copy of `uFestaEnemy`'s 286-slot vtable with overrides, plus every
  behaviour, shell and effect handler, compiled as ARM32 into the module (notes/porting-guide.md:86-91, 142-160;
  module/game.h:24-30).

### 4. Mechanism by mechanism on the native export (question 4)

| # | mechanism in the repo | on the native export | why |
|---|---|---|---|
| M1 | `exefs/subsdk1` loaded and relocated by rtld at a fixed 0x2E19000 | **impossible as-is; must be replaced** | The export recompiles a fixed set of exefs modules (main, rtld, sdk, subsdk0) and loads mods as `<mod>/romfs` file overlays plus optional plugin DLLs; there is no path that loads an extra ARM32 module or runs rtld on one (src/suyu/game_export.cpp:2090-2095, 2176-2199; forge-pc README.md:36-42). |
| M2 | IPS branch patches at 113 hook sites (`ldr pc,[pc,#-4]` -> trampoline) | **needs a Forge plugin port, mechanically 1:1** | Forge `hook_create(target, pre, post, user)` runs at a recompiled block start, before the block executes, and `FORGE_HOOK_CONTINUE` then runs the block (i.e. the original instructions) — the same semantics as the module's displaced-instruction trampoline (forge_pc.h:187-204, 132-142). Post-call sites are the case that matters here: the a32 lifter adds `pc + 4` as a block start after every call (`add_soft(pc + 4)`, "execution continues after a call") (a32disc.h:340-348), so a hook placed right after a `bl` should be accepted by `is_block` — [INFERENCE: the soft starts are emitted blocks, which the probe in "In progress" step 1 confirms site by site]. Function entries are roots (a32disc.h:205-211, 234-246). Mod-host `is_block` is a plain lookup of that table (mod_host.cpp:160-162). |
| M3 | 16 literal-pool rewrites to the module's table copies | **port with `mem_write`** (needs one experiment) | Literals are data words inside `.text`; `mem_write` reaches guest memory, and the recompiled code loads them from the guest address space at runtime [INFERENCE from the lifter marking literal words as `Discovery::Data`, a32disc.h:205-245 — the alternative is that the emitter bakes constants]. |
| M4 | Immediate rewrites (2 registry `cmp`, 7 Notes `cmp`, 26 item `movw`, item array size, 2 stack-frame `sub/add`) | **cannot be patched at all; needs hooks/overrides** | Immediates are compiled into the host x64; the API states plainly that "changing code bytes in guest memory does NOT change the recompiled game: use hooks for code" (forge_pc.h:229-231; forge-pc README.md:94-98). Each site must be replaced by a hook on the enclosing block start (or a `hook_override` of the enclosing function) plus `FORGE_HOOK_JUMP` into adapted guest ARM code or a recomputed return. |
| M5 | Table copies + 3 data-word redirects written by `module_init` | **port with `mem_alloc` + `mem_write`, same design** | `mem_alloc` gives guest memory "readable and writable by game code ... point registers at them, build structs there" (forge_pc.h:245-256); tables live there and the redirect words are ordinary guest data. Sizes are small (8 registry tables of 89..142 entries, 3 Notes tables with 123 + 4 = 127 entries) (module/build.py:124-133, 148-157). |
| M6 | Injected AArch32: new enemy class DTI + 286-slot vtable, shell subclasses, behaviour code | **port, but it must run as guest ARM on the JIT** | The export hands any PC with no recompiled block (including a module a mod loaded into guest memory) to a dynarmic A32 JIT, and hands control back to recompiled code when the PC is covered again (arm_recomp32.h:27-41; arm_recomp32.cpp:1063-1135). What is lost is rtld: the blob must be linked at the `mem_alloc` address or relocated by the plugin (link.ld currently relies on `.rel.dyn`, module/link.ld:20-30). |
| M7 | Data files read through `nn::fs` stubs at `app:/nativeNX/mh4uport/*.bin`; "installed" = `nn::fs::GetEntryType` on the arc | **port with an adaptation** | Guest `nn::fs` is main's own recompiled import stub (module/port.c:20-24, 26, 88-108, 151-165), and the export has an HLE file system, so it may work unchanged [INFERENCE]; the robust route is host-side `fopen` of `<mods>/<mod>/romfs/nativeNX/mh4uport/*.bin` in the plugin plus `mem_write` into a `mem_alloc` pool. |
| M8 | RomFS assets (arcs, BGM, quest, stage alias, gear models, data files) | **works as-is** | "File mods (`romfs`) and settings files work on both Windows and Linux as they are"; files are applied at game start (forge-pc README.md:36-42, 158-160). Without M2-M6 they are inert: nothing requests those arcs and nothing reads the data files. |
| M9 | Debug logging via `svc #0x27` (`svc_output_debug_string`) | **works** (or replace with `api->log`) | The export implements SVC 0x27 for 32-bit callers (svc.h:423-427; svc/svc_debug_string.cpp:73-79), and SVCs from JIT-run guest code are parked and serviced normally (arm_recomp32.h:30-34). |
| M10 | Save tally / quest board / gear and item table patches done in handlers | **port: pure hook work** | All of it is register/table manipulation reachable from the 113 hook sites; nothing depends on being a separate module. |
| M11 | Online co-op expectation ("everyone should have the same monsters installed", README.md:24) | **unchanged** | The port is local-side; the export has its own LDN/online path and the plugin does not change it. |
| M12 | Hot reload / unload discipline | **constraint, not a blocker** | Forge warns that plugins which leave guest code and patched words in guest memory "should not be reloaded" (forge_pc.h:212-215), which is exactly this port; ship with `hot_reload = false`. |

Also note the version coupling: the module is built for main build id `FB08F1D2…` (module/build.py:43), and Forge PC
also hardcodes MHGU 1.4.0 main offsets (src/loader.cpp:60-64), so the same game version is on both sides. The module
hardcodes `MAIN_BASE 0x704000` (module/game.h:6); a plugin must derive the base at runtime from
`api->mem_getModuleBase("main")` (forge_pc.h:188-191) instead of trusting the constant.

Overall: the mod cannot run as-is on the native export. Its content (M8) is already usable; its code must be rebuilt
as a Forge plugin (M2, M4, M7, M10 are hook work; M5 is data writes; M6 is the same AArch32 code executed on the
JIT). Nothing in the mod needs a capability the export lacks — the two mechanisms that do not survive, the ExeFS
module (M1) and code-byte patches (M4), both have a documented Forge equivalent (hook callbacks and guest-memory
data writes). The open risk is not feasibility but cost and speed (M3 experiment, M6 relinking, JIT throughput).

### 5. Rough size of a Forge port (question 5)

Hook count, per the build:
- 113 `hook_create` sites (65 core + 45 generated gear-table load exits + 3 item obtained hooks).
- 16 literal rewrites in the same handler set become `mem_write` one-liners (module/build.py:138-149, 422-428) —
  no hooks.
- ~38 code-immediate sites cannot be patched and need a hook or `hook_override` at an enclosing block start:
  2 registry `cmp`, 7 Notes `cmp` (module/build.py:164-175), 26 item `movw`, 1 item array size, 2 stack-frame
  immediates (module/build.py:181-192). Some are loop heads (block starts), some need an override of the whole
  function.
- 11 tables to relocate into `mem_alloc` memory: 8 registry tables (one of them, `reg_mapicon`, is keyed by em
  number) plus 3 Notes tables (module/build.py:120-133, 148-152), and the three redirect data words.

Injected ARM32: the port cannot recompile this to x64, because the game calls it through guest vtables. The ARM32
volume is the existing module source:
- Core: `module/` holds 10 C files plus `crt0.S`; the ones with published sizes are `items.c` 11,521 B,
  `gear.c` 10,970 B, `port.c` 10,502 B, `main.c` 9,184 B, `bgm.c` 5,989 B, `notes.c` 4,593 B, `quest.c` 3,551 B,
  plus `util.c`, `stage_alias.c`, and `game.h` 22,421 B / `port.h` 4,538 B (GitHub tree API, blobs).
- Per-monster code: `monsters/dalamadur/module/` (about 40 files, e.g. `em090_act7.c` 32,496 B,
  `em090_act.c` 17,862 B), `monsters/gogmazios/module/` (about 25 files; `em089_act.c` alone is 164,594 B,
  `em089.c` 8,117 B), `monsters/tigerstripe_zamtrios/module/` (5 files) (GitHub tree API, blobs).
- So the guest ARM32 image is a few hundred KB of compiled code [INFERENCE — the repo does not commit the built
  `subsdk1`; `build.py` only prints its size].

Plugin-side x64 work, roughly: a loader that builds the guest pool and copied tables, 113 thin hook shims that
marshal `ForgeCpu*` <-> the module's `regs[]` convention, the immediate-site replacements, host file loading, and a
build step that emits the ARM32 blob plus its relocations at the `mem_alloc` base. That is a small C++ layer over
the existing C code, but the risky parts are M3 (literal behaviour), M6 (relinking without rtld) and JIT speed.

Licence constraint: the code side is GPL-3.0-or-later (README.md:34), so a Forge port that reuses it must itself be
GPL and must stay a separate mod; only facts/addresses taken from this study can go into MIT repos.

---

## In progress

Ordered verification plan for a Forge port, none of it done here (this study wrote no code):

1. For each of the 113 sites, log `api->is_block(main_base + offset)` and drop the sites that are not block starts;
   for those, walk back to the enclosing function entry and plan an override.
2. Prove M3: in a test plugin, `mem_write` one of the 16 literal words (e.g. 0x50BD0) and check that the game's
   id->index search reads the module copy.
3. Prove M6 end to end: assemble a 16-instruction ARM32 blob into `mem_alloc` memory, call it from a hook, have it
   call one game function and return; measure the per-call cost.
4. Build the existing module as a flat blob (no rtld): link at 0, record `.rel.dyn`, apply the relocations in the
   plugin after `mem_alloc`, run `module_init` from `forge_onInit`.
5. Replace `nn::fs` data loading with host reads + `mem_write` (module/port.c:88-108 is the reference parser).
6. Re-run the repo's own acceptance path (`notes/porting-guide.md:286-296`) on the export: boot, table patches in
   the log, then one ported quest.

---

## Unknown

- **Literal loads (M3)**: whether the recompiled main re-reads literal-pool words from guest memory (then `mem_write`
  works) or bakes them at compile time (then all 16 become hook sites). Not provable from the repo or docs; needs
  experiment 2.
- **Cross-engine calls (M7)**: whether JIT-run guest code can call main's recompiled `nn::fs` import stubs and come
  back. The export documents both directions of the hand-off and counts `jit_to_static` transitions
  (arm_recomp32.cpp:1063-1135, arm_recomp.cpp:190-192), but nothing states that a mod's `nn::fs` call is serviced.
- **Performance**: no measurement of how expensive it is to run the whole monster class (including the 164 KB
  `em089_act.c` behaviour code) on the dynarmic A32 JIT every frame, versus recompiled game code.
- **Module base**: whether `mem_getModuleBase("main")` returns 0x704000 in this export. Forge's own MHGU offsets
  assume only that offsets match, not the base value (src/loader.cpp:60-64, 1365-1377); the module's hardcoded
  0x704000 (module/game.h:6) may not hold.
- **mem_alloc capacity and lifetime**: the port needs several hundred KB for code plus ~100 KB for tables and the
  256 KB data pool (module/port.c:27, 29). The API documents `mem_alloc` but not the region size (forge_pc.h:245-256).
- **Built module size**: the actual `subsdk1` byte count is not in the repo (build.py only prints "built N bytes",
  module/build.py:436), so the ARM32 image size is an estimate.
- **Hot reload / re-entry**: whether the port's guest code and patched words survive a mod reload cleanly; the API
  advises not reloading such a plugin (forge_pc.h:212-215), which the port should simply obey.
