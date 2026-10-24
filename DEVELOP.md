# MH4U-native 移植设计（DEVELOP.md）

状态：设计定稿，未实现，未实机验证。本文只描述设计，不含代码。
上游：`Scrimas/MH4U-port-to-MHGU`（GPL-3.0-or-later）。
目标平台：SUYU-GURecomp 原生 PC 导出（MHGU 1.4.0）+ Forge PC 0.7 插件（MIT）。

本文所有未经验证的判断用 `[I]` 标记：第 8 节给出风险表与实机验证计划（每条标 `TODO(user)`），附录 A 汇总全部 `[I]`。

## 0 引用约定

| 简写 | 指代 | 许可 |
|---|---|---|
| `build.py` / `main.c` / `link.ld` / `gear_hooks.txt` / `allocations.toml` / `notes/porting-guide.md` / `port.c` / `util.c` / `game.h` | 上游仓库文件，路径相对仓库根 | GPL-3.0-or-later |
| `forge_pc.h` / `loader.cpp` / `forge-pc/README.md` | `.tmp/ext/forge-pc/`（Forge PC 0.7） | MIT |
| `a32_to_c.h` / `a32_host_runtime.c` / `a32_runtime.h` / `a32disc.h` / `arm_recomp32.cpp` / `arm_recomp32.h` / `arm_dynarmic_32.cpp` / `mod_host.cpp` / `mod_host_api.h` / `memory.cpp` / `game_export.cpp` / `svc*` | `.tmp/ext/SUYU-GURecomp/src/...` 与 `tools/a32recomp/` | GPL-3.0 |
| `STUDY` | `docs/port-study.md`（可行性研究） | GPL-3.0-or-later（本仓库） |
| `REVIEW` | `docs/port-study-review.md`（研究的复核） | 同上 |

结论优先级：`REVIEW` 的更正覆盖 `STUDY`。已知三处更正：立即数站点 40（不是约 38）、重定位表 11 张（不是 12）、SVC 日志在 `api->call` 内会失败（`REVIEW:78-82, 87-91, 106-116`）。

上游文件的引用是转述其内容，不复制其注释原文；任何被吸收的上游代码保留原版权头（见 `NOTICE.md`）。

## 1 目标与非目标

### 1.1 目标

1. 让上游 mod 的**内容与行为**在原版 MHGU 1.4.0 的 PC 导出上可玩：三只移植怪物（Dalamadur、Gogmazios、Tigerstripe Zamtrios 变体）、它们的任务、装备、道具、BGM、Notes、地图图标、Stage 别名。
2. 交付形态是**一个 Forge PC 插件**（Windows `mh4u.dll`，Linux `mh4u.so`）加一个普通 romfs mod；不修改游戏 exe，不要求用户重新导出（重新导出只作为第 4.8 节的备选）。
3. 代码面 1:1 承载上游的 113 个 hook 站点、40 处立即数行为差异、11 张重定位表和 16 处字面量改写（数量依据见 3.1、4.1、5.2、5.3）。
4. 上游的 AArch32 模块（`subsdk1` 的等价物）**原样重编译**后作为 guest ARM 代码注入内存，在导出的 dynarmic A32 JIT 上运行（`arm_recomp32.h:20-39`；`forge_pc.h:211-213`）。
5. 许可与合规：插件与模块同为 GPL-3.0-or-later，作为独立 mod 分发；不携带任何游戏文件。

### 1.2 非目标

1. 不支持 Switch/Atmosphere 路径（上游已经支持；本仓库不改上游的 ExeFS/IPS 机制）。
2. 不新增内容（不改怪物行为设计、不加新任务、不做平衡性调整）。
3. 不改游戏引擎、不改存档格式（Notes 只用上游已用的 5 个空闲存档位，`STUDY:128-132`）。
4. 不动在线联机（导出自带的 NEX 路径不受影响，`STUDY:160` 的 M11）。
5. 不做 ImGui 界面；配置只用一个 ini（`forge_pc.h:283-307` 的 config API）。
6. 不修改 `mod_host_api.h` / 导出 exe；所有能力必须来自 Forge PC 0.7 的公开 API。
7. 不承诺 1.4.0 以外的版本；所有 `main` 偏移与 build id `FB08F1D20FD1204FE7070672777F869B` 绑定（`build.py:43`；`MHGU/DEVELOP.md:22`）。

### 1.3 平台常量

| 项 | 值 | 依据 |
|---|---|---|
| 游戏 | MHGU 1.4.0，title id `0100770008DD8000` | `MHGU/DEVELOP.md:21`，`INSTALL.md:97` |
| `main` build id | `FB08F1D20FD1204FE7070672777F869B` | `build.py:43`，`MHGU/DEVELOP.md:22` |
| `main` 段布局 | `.text 0x0-0x13f1dec`，`.rodata 0x13f2000-0x172ecfa`，`.data 0x172f000-0x184ad78`，`.bss 0x184aff8-0x2146780` | `MHGU/DEVELOP.md:23` |
| `main` 基址 | 运行时 `api->mem_getModuleBase("main")`；上游常量 `0x704000` 在本导出上不可信 `[I]` | `forge_pc.h:188-191`，`game.h:6` |
| Forge PC 版本 | 0.7（需要 `FORGE_HOOK_JUMP`、`is_block`、`call` 的 JIT 兜底） | `forge_pc.h:65-67, 150, 310-312` |
| hook 站点 | 113 = 65 core + 45 gear + 3 item | `STUDY:76-79`，`REVIEW:57-76` |
| 立即数站点 | 40 = 2 registry + 7 Notes + 28 item `movw` + 1 数组尺寸 + 2 栈帧 | `REVIEW:78-82` |
| 重定位表 | 11 = 8 registry + 3 Notes | `REVIEW:87-91` |
| 字面量改写 | 16 | `STUDY:103-107`，`build.py:138-151` |
| 直接数据字 | 3（`0x1839618`、`0x183fff0`、`0x183ec4c`） | `STUDY:111-112`，`main.c:42-45` |
| 追加道具数 `ITEMS_APPENDED` | 30（Dalamadur `items = [2991, 3020]`） | `allocations.toml:166, 201`，`REVIEW:72-74` |
| 注册表增长 `N_reg` | 3（每注册一只怪物 +1）`[I]` | `build.py:120-133` |
| Notes 表 | 123 + 4 = 127 项 | `STUDY:154`，`REVIEW:88-90` |
| guest 堆 | 32 / 16 / 8 MB，取第一个能映射的 | `mod_host.cpp:74-75` |
| dispatch 查找缓存 | 4096 项直接映射 | `arm_recomp32.cpp:1158` |
| 链预算 | 64 | `arm_recomp32.cpp:105` |
| `api->call` 上限 | 参数总数 32（r0-r3 之后转栈，栈上最多 28 个）、浮点 16、嵌套 4 层 | `forge_pc.h:206-217`（仅语义）；`mod_host.cpp:171`；`arm_recomp32.cpp:1068` |

## 2 总体架构

### 2.1 交付物与仓库布局

```
<mods>/0100770008DD8000/MH4U/
  plugins/mh4u.dll            # Windows 插件（Linux: mh4u.so）
  data/module.blob            # guest 镜像 + 符号 + 重定位 + 站点表 + 怪物名单（本仓库构建产物）
  mh4u.ini                    # 插件配置（forge_pc.h:283-307 的 config_open 解析到 mod 根目录）
  romfs/nativeNX/...          # 与上游完全一致的 romfs（第 7 节）
```

仓库侧（本仓库将来的目录，当前只存在于设计）：

```
MH4U-native/
  docs/DEVELOP.md             # 本文
  plugin/                     # x64 C++17，Forge PC 0.7 插件
    plugin.cpp                # 入口、生命周期、配置、日志转发
    blob.cpp                  # module.blob 解析与校验
    arena.cpp                 # mem_alloc 竞技场、bump 分配
    reloc.cpp                 # R_ARM_RELATIVE 应用与自检
    stubs.cpp                 # trampoline / stub 生成
    hooks.cpp                 # 113 站点安装、pre 回调、计数
    immediates.cpp            # 40 个立即数站点的 stub 装配
    tables.cpp                # 16 处字面量改写与回读校验
    hostfs.cpp                # romfs 数据文件的宿主读取
  guest/                      # ARM32，上游 module/ 的 fork（改动见 2.5）
  tools/
    mkblob.py                 # 链接产物 -> module.blob
    gen_sites.py              # 站点表 -> site_table.inc + data/sites.toml
    check_no_svc.py           # 构建期扫描：blob 的 .text 里不得有 SVC
```

`mh4u.ini` 放在 mod 根目录：`config_open` 接受相对名字（子目录也合法，`forge_pc.h:286-288`；只有绝对路径或含 `..` 的名字返回 NULL），本文统一放根目录以便用户找到；文件缺失时全部取默认值，`config_save` 会生成它。

### 2.2 进程内布局

```
harness（导出 exe，x64）
  +- 重编译的 main / sdk / rtld / subsdk0（x64，固定块表，不可改）
  +- Forge PC（forge.dll，x64）
  |    +- MH4U 插件（mh4u.dll，x64）           <- 本文的代码
  +- guest 地址空间（u32）
  |    +- main 模块（ARM32 字节仍在，但执行的是 x64 重编译代码）
  |    +- guest 堆（mem_alloc，32/16/8 MB，`mod_host.cpp:75`）
  |    |    +- arena A：模块镜像（.text/.rodata/.data/.bss；增长后的 11 张表在 bss 里）
  |    |    +- arena B：trampoline 与立即数 stub 池
  |    |    +- arena C：怪物 `.bin` 宿主副本 + 数据清单 + 日志环
  |    +- dynarmic A32 JIT（无重编译块的 PC 一律走这里，`arm_recomp32.h:20-39`）
  +- romfs（LayeredFS，游戏自己的文件系统）
```

三个执行域与两种切换：

| 方向 | 触发 | 代价 |
|---|---|---|
| x64 重编译块 -> JIT | PC 上没有重编译块（注入的模块代码、未翻译指令、Thumb） | 一次 guest 状态 marshal + 至多 4096 步 JIT（`STUDY:155` 的 M6 行；`arm_recomp32.cpp` 的 fallback 路径）`[I]` 具体步数与实测成本 |
| JIT -> x64 重编译块 | PC 重新落进块表 | 一次 marshal 返回 |
| x64 插件 -> guest | `api->call`（受限）或 `FORGE_HOOK_JUMP`（无限制） | 见 3.3 |

关键约束（第 8 节 R4）：JIT 的取指有**页缓存**，只有对齐页变化或 ISB 才重取（`arm_dynarmic_32.cpp:42-52`，`arm_dynarmic_32.h:38-40`）；A32 回调没有 icache 操作钩子。因此**注入的 guest 代码必须在首次执行前写完，运行期不得再改写**。所有 stub 的形态（是否 replay、replay 哪两个词、常量是多少）在 init 时一次性定稿。

### 2.3 启动时序

```
exe 构造 mod host（MappedGuestHeap: mod_host.cpp:257-283）
  -> mod host 加载 Forge（forge.dll）
  -> 游戏第一条 guest 指令前：ModHost::on_game_start（mod_host.cpp:407-408）
      -> Forge 自装 4 个 hook（kSystemCtor / kSystemDtor / kAppRun / kUpdate，loader.cpp:1370-1373）
  -> 游戏跑到 sApp::run，Forge 的 kAppRun pre-hook 调用各插件的 forge_onInit（loader.cpp:63, 1276-1293）
      -> 本插件：装载 blob、mem_alloc、重定位、写表、生成 stub、装 113 个 hook、装 init 入口 hook
  -> 下一帧的 kUpdate：init 入口 hook 用 JUMP 进入 module_init（第 6 节）
  -> module_init 返回后，正常游玩；插件在 forge_onUpdate 里排空日志环
```

要点：`forge_onInit` 早于 `sApp::run` 的函数体，而存档加载、道具表加载、任务板都在其后，所以 113 个站点里除极少数在更早阶段就被调用的以外都能装到 `[I]`（第 8 节 V1 会列出来）。

### 2.4 模块装载器（无 rtld）

导出不提供任何加载额外 exefs 模块的路径：mod 的 `exefs/` 不被套用是因为启动方用默认 `override_update = false` 构造 deconstructed loader，`PatchExeFS` 只在 `override_update` 为真时才套 mod 的 exefs（`REVIEW:26-30, 117-131`；`STUDY:150-152` 的 M1 行结论对、证据按复核更正）。所以上游的 `subsdk1` 由本插件自己装载。

1. **构建**：沿用上游链接步骤（`build.py:312-314, 404-407` 的 `clang --target=armv7a-none-eabihf -marm -fPIC` + `ld.lld -shared -T link.ld`，`-init=module_init`），产出一个 ET_DYN。区别是**不**做 `nso.elf2nso`（`build.py:404-407`）。
2. **打包**：`tools/mkblob.py` 把 ET_DYN 翻成 `module.blob`，去掉 ELF/NSO 解析负担：

   | 区段 | 内容 |
   |---|---|
   | header | magic `MH4UB`、版本、`arena_size`、`entry`（= `DT_INIT`）、段数/符号数/重定位数/站点数 |
   | sections | (name, vaddr, size, file_off, flags)，含 `.text/.rodata/.data/.bss` |
   | symbols | (name, vaddr)，至少含 `module_init`、`g_main_base`、`g_self_base`、各 `*_c` handler |
   | relocs | (r_offset, r_type, r_addend)，只允许 `R_ARM_RELATIVE` |
   | sites | 113 行站点表（第 3.2 节） |
   | manifest | 本 build 携带的怪物名单（与 `core.toml`/`allocations.toml` 同源），供第 7.3 节预装载 `.bin` |
   | strings | 名字表 |

3. **装载**（插件，`forge_onInit` 内，全是宿主侧内存操作，不执行 guest 代码）：
   - `arena = api->mem_alloc(align_up(arena_size + stub_pool + data_pool, 0x1000), 0x1000)`；`mem_alloc` 返回零填充、guest 可读写、对齐 >= 16 的区域（`forge_pc.h:245-251`），`.bss` 因此天然为 0。
   - 逐段 `mem_write(arena + vaddr, bytes, size)`；回读校验 `mem_read` 比对。
   - 重定位：对每条 `R_ARM_RELATIVE`，`word = addend + arena`，写回 `arena + r_offset`。出现 `R_ARM_ABS32 / GLOB_DAT / JUMP_SLOT` 一律装载失败并打印符号名；构建期用 `-nostdlib` + 无未定义符号保证这一点（`link.ld:15-32` 的 `.dynsym/.dynstr/.hash/.rel.dyn/.rel.plt/.dynamic` 说明上游是 rtld 装载；本插件取代 rtld）。
   - 写两个全局：`g_main_base = api->mem_getModuleBase("main")`，`g_self_base = arena`（offset 从符号表取）。上游 `game.h:6` 把 `MAIN_BASE` 写成常量 `0x704000`，本移植改成变量（第 6.4 节）。
   - 把 `module.blob` 的 `entry`（`DT_INIT`，上游用 `-init=module_init` 设定）记下，供第 6.2 节使用。
   - 按 blob 的 manifest 把各怪物 `.bin` 读进 arena C 并填数据清单（7.3），以及分配日志环；这两件事必须在放行 `module_init` 之前完成。
   - 任何一步失败：`api->log(FORGE_LOG_ERROR, ...)`、`mem_free`、置 `disabled` 标志，游戏按原样继续（所有 hook 回调先检查该标志）。

   为什么不在 arena 里直接跑上游 NSO：NSO 的段是 LZ4 压缩的，且 `MOD0` 头部与 `.dynamic` 假设 rtld 会介入（`link.ld:1-3`），本导出没有 rtld 可给 mod 用；blob 方案把"重定位"这一步搬到插件里，把"格式"这一步搬到构建脚本里，插件因此不需要 ELF/NSO 解析器。

4. **代码可执行性**：`mem_alloc` 给的是 guest 可读写的堆区，`forge_pc.h:211-213` 明确把"插件自己装进 `mem_alloc` 的模块代码"列为走 JIT 的对象，因此不需要（也无法）给该区域加可执行位。

### 2.5 与上游的分工

| 上游文件/机制 | 本移植的处理 |
|---|---|
| `module/build.py` 的 IPS 生成与 `install()`（`build.py:414-430, 443-479`） | 删除：hook 由插件注册 |
| `module/build.py` 生成的 trampoline（`build.py:348-372`） | 由 `plugin/stubs.cpp` 在运行期生成同样的指令序列（3.4），返回地址用运行期基址 |
| `module/build.py` 的 `LIT_PATCHES` / `WORD_PATCHES` / `item_patches`（`build.py:138-192`） | 字面量变成 16 次 `mem_write`（5.3）；立即数变成 40 个 stub（第 4 节） |
| `module/main.c` 的 `module_init`（`main.c:29-48`） | 保留（表复制、class 注册、3 个数据字重定向），入口改成插件 JUMP 进入 |
| `module/util.c` 的 `logf`（`util.c:28-35`，走 `svc_output_debug_string`） | 改成写日志环（6.3） |
| `module/port.c` 的 `nn::fs` 读取（`port.c:20-33, 88-108, 151-165`） | 改成读插件放好的 guest 缓冲（7.3） |
| `module/game.h` 的 `MAIN_BASE` 常量（`game.h:6-9`） | 改成运行期变量（6.4） |
| 其余 handler（`items.c`、`gear.c`、`bgm.c`、`notes.c`、`quest.c`、`stage_alias.c`、各 monster 代码） | 原样保留，只改 `MAIN_BASE` 的用法 |
| `module/core.toml` / `allocations.toml` 的清单（`core.toml:1-6`） | 变成 `module.blob` 的 sites/tables 与 `data/sites.toml`，由 `gen_sites.py` 生成 |

### 2.6 数据流

```
romfs/nativeNX/mh4uport/<name>.bin  --(宿主 fopen)--> arena C --> 模块解析器（guest）
romfs/nativeNX/arc/enemy/emNNN_VV.arc  --(LayeredFS)--> 游戏自己的 fs（不改）
main .data 的 8+3 张表  --(module_init, mem_read)--> 模块 bss 副本（增长后）--> 3 个数据字重定向
模块 handler <--(trampoline, JIT)-- 游戏 113 个站点 <--(pre hook, JUMP)-- 插件
```

## 3 Hook 表方案

### 3.1 113 个站点的构成

| 组 | 数量 | 注册来源 | 形态 |
|---|---|---|---|
| core（生成/注册/存档/任务/Notes/道具/装备/BGM/Stage/伤害/安全/两只怪） | 65 | `build.py:52-111` | JUMP + trampoline（replay 2 词） |
| gear 表加载出口 | 45 | `gear_hooks.txt`：14 个武器类 x 3 表 = 42，加 `gear_w_create`、`gear_w_process`、`gear_kireaji` | 同上 |
| item obtained（`obtained_set/clear/test`） | 3 | `build.py:177-192`，仅当有道具追加 | 同上 |
| 合计 | **113** | | 112 replay + 1 replace（`name_index`，`build.py:117-119` 的 `REPLACE_HOOKS`） |

65 个 core 站点按用途分：生成/注册/文件 5、存档/tally 2、任务列表 3、名字/Notes 2、道具/装备 12、BGM 3、Stage/场地 18、诊断/伤害 7、安全检查 1、Tigerstripe 变体 9、Dalamadur 3（明细见 `STUDY:84-101` 的表）。

上游缺陷必须在移植时一并处理：`stats_hunted`（`0x3A3930`）在 `build.py:59` 被行尾 `#` 注释掉，从未注册，尽管 `main.c:178` 有对应实现（`REVIEW:72-76`）。本移植**不**默认恢复它（保持与上游行为一致），只在站点表里留一条 `disabled=true` 的记录与注释，避免"移植悄悄修了上游 bug"导致行为差异无法归因。

### 3.2 站点描述符

`data/sites.toml`（由 `tools/gen_sites.py` 从上游 `build.py` + `gear_hooks.txt` 生成，再编译成 `site_table.inc`，二者同源，不允许手改生成物）：

```toml
[[site]]
id      = 12                     # 稳定编号，写进日志
name    = "notes_unlock_check"   # 上游 handler 名
main_off= 0x554DA4               # main 相对偏移（上游 IPS 的 site）
kind    = "replay"               # replay | replace
replay  = [0xE92D4FF0, 0xE24DD01C]  # site 与 site+4 的两个原始 ARM 词
entry   = "notes_unlock_check_c" # 模块内的 handler 符号
flags   = []                    # 例：["per_frame"], ["disabled"]
note    = ""                     # 上游注释/已知缺陷说明
```

插件侧结构（x64）：

```c
struct Site {
    uint32_t main_off;      /* site 的 main 相对偏移 */
    uint32_t tramp;         /* arena 里生成的 trampoline 地址 */
    uint32_t handler;       /* arena + handler 符号偏移 */
    uint32_t replay[2];
    uint8_t  kind;          /* REPLAY | REPLACE */
    uint8_t  installed;     /* hook_create 是否成功 */
    uint8_t  block;          /* is_block 结果 */
    uint32_t hits;           /* 命中计数（诊断） */
    const char* name;
};
```

`user` 指针直接指向 `Site`，因此**只有一个** pre 回调函数（3.3）。

### 3.3 两条路径：pre-hook CONTINUE 还是 JUMP 到 trampoline

| 方案 | 语义 | 能否复用上游 handler | 成本 | 结论 |
|---|---|---|---|---|
| C：pre-hook 返回 `FORGE_HOOK_CONTINUE` | 回调里改 `ForgeCpu` 的寄存器后，让重编译块照常跑 | **不能**。handler 是 ARM32，x64 回调无法直接调用它；`api->call` 虽可从 hook 里用（`forge_pc.h:206-217`），但内部系统调用会让 call 失败，且 hooks 在 call 内不触发 | 最低（无 JIT 往返） | 不作为默认 |
| J：pre-hook 设 `cpu->r[15] = tramp` 后返回 `FORGE_HOOK_JUMP` | 插件不碰寄存器，交给模块自己的 trampoline 做保存/调用/replay/返回 | **能**，与上游 1:1 | 每次命中一次 dispatcher 往返 + 一次 JIT 进出 | **113 个站点全部采用** |
| O：`hook_override` | x64 函数替代原函数 | 不能（同上） | 低 | 只用于第 4 节的 A 类（2 个站点）与极少数热点 |

方案 J 的语义细节（逐条核对过）：

- `FORGE_HOOK_JUMP`（0.7）表示"在 `cpu->r[15]` 继续，寄存器保持回调留下的值；bit 0 选 Thumb；lr 不使用"（`forge_pc.h:139-142, 150`）。实现上是 `SetReturnPc(mcpu, mcpu->r[15])`（`loader.cpp:1449-1457`）。
- 跳到 hook 自己的地址会被警告并按 CONTINUE 处理（`loader.cpp:1450-1456`），所以 trampoline 绝不能等于 site。
- 跳转后**该地址的 post hook 不运行**，但目标地址上的 hook 会运行（`forge_pc.h:141-142`）。本移植不使用 post hook，因此无影响；唯一例外是第 6.2 节的 init 入口跳一帧 Forge 自己的 `kUpdate` post hook（R13，无副作用）。
- 进入一个没有重编译块的 PC 时，导出把它交给 dynarmic A32（`arm_recomp32.h:20-39`），这正是 trampoline 的运行方式；trampoline 末尾跳回 `main+site+8` 后，若该地址是块起点就回到 x64。
- 回调运行在"模拟 CPU 核的宿主线程"上，可能并发（`forge_pc.h:41-44`）。`Site` 表 init 后只读；`hits` 用 relaxed 原子或干脆不统计（见 R8）。

### 3.4 trampoline 布局

每个站点 56 字节（14 词），由 `stubs.cpp` 在 init 时用 `mem_write` 写进 stub 池（**首次执行前**写完，见 R4）。指令序列与上游生成器一致（`build.py:348-372`），只把其中的绝对量换成运行期值：

```
  push  {r0-r5, r12, lr}       ; 8 词
  vpush {d0-d7}                ; 64 字节
  add   r0, sp, #64            ; r0 = &regs[0]（regs[8] 起是调用者的栈参数）
  ldr   ip, [pc, #-4]          ; 取 handler 绝对地址
  .word arena + handler_off
  blx   ip
  cmp   r0, #0
  vpop  {d0-d7}
  pop   {r0-r5, r12, lr}
  bxne  lr                     ; handler 返回非 0：直接返回调用者（replace）
  .word replay[0]              ; = site 处原始指令
  .word replay[1]              ; = site+4 处原始指令
  ldr   pc, [pc, #-4]
  .word main_base + site + 8   ; 返回重编译代码
```

说明：

- `r0 = sp + 64` 与上游一致：`push` 压了 8 个词（32 B），`vpush` 压了 64 B，因此 `regs[0..7] = r0,r1,r2,r3,r4,r5,r12,lr`，`regs[8]` 起是栈参数；d0-d7 位于 `regs[-16..-1]`（按 double 解释）。
- `bxne lr` 用的是 `push` 保存的调用者 lr，因此 REPLACE 语义与上游完全相同。
- replace 类型的站点把 `replay[0..1]` 填 `0xE7F000F0`（`udf #0`）：万一 handler 误返回 0，会立刻在日志/崩溃里暴露，而不是悄悄执行旧指令。
- `replay[0..1]` 必须满足上游对"被搬走的指令"的同一组限制：不含 pc 相对访存、不含分支（`build.py:324-346, 379-381` 在构建期断言，本移植把该断言搬进 `gen_sites.py`）。理由：这两个词在 arena 里执行，pc 相对量会算错。
- handler 都在同一块 arena 内，`ldr ip,[pc,#-4]` 是绝对地址，无 32MB 跳转范围限制；模块内部的 `bl` 也在 blob 内，范围安全。
- trampoline 与 stub 池整体 4 字节对齐、按 16 字节对齐分配，避免跨页取指。

### 3.5 块起点要求与缺口处置

`hook_create` / `hook_override` 只在"重编译块起点"上成功，否则返回空并打 `hook at %#x: not the start of a recompiled block`（`forge_pc.h:199-204`；`loader.cpp:584-589`）。注册成功会顺手 `mark(target)`，使链式分支回到 dispatcher（`loader.cpp:601`）。

因此 init 时对 113 个站点逐个 `api->is_block(main_base + off)` 探测（`forge_pc.h:310-312`，无日志噪声），分三桶：

| 桶 | 判定 | 处置 |
|---|---|---|
| B1 | `is_block` 为真 | 直接 `hook_create`，方案 J |
| B2 | 为假，但同一函数内有更早的块起点，且两者之间无分支/无 pc 相对访存 | hook 那个前驱块起点，JUMP 到"前缀重放 stub"（4.7 的 P3）：重放前缀指令 -> 进入该站点的 trampoline |
| B3 | 为假，且找不到可用前驱 | 两级：(1) 重新导出并附 `coverage/<build id>.txt` 把该偏移登记为发现根（`game_export.cpp:1963-1993`），使其成为块起点；(2) 若不能重导出，用 `hook_override` 覆盖所在函数（4.7 的 P4） |

决策顺序固定为 B1 -> B2 -> B3，原因是代价递增：B2 是纯数据（多写几条 stub 指令），B3-(1) 需要用户重导出一次（约 10-20 分钟，`INSTALL.md:99-111`），B3-(2) 需要逐个函数重写。`[I]` 每个桶的实际站点数未测：函数入口通常是块起点（发现阶段有 `push {...,lr}` 扫描与 BL 目标，`a32disc.h:250-260, 354-384`），但"只被函数指针/vtable 调用、且无 `push {lr}` 前导"的函数可能落在 B2/B3。

站点报告：init 结束把 `(id, name, main_off, block?, bucket, installed, error)` 写进 `forge.log`（INFO 一行汇总 + DEBUG 每站点一行），复制一份到 `<mod>/mh4u_sites.txt`，作为 V1 的判据。

### 3.6 站点表的地址约束

- 113 个站点必须是**113 个不同地址**：同一地址上的 pre hook 数组上限 24（`loader.cpp:1409`）。上游 IPS 对同一地址重复写也只是覆盖，所以本移植在 `gen_sites.py` 里对重复偏移直接报错。
- 不得与 Forge 自用的 4 个地址冲突（`loader.cpp:60-65`：`0x887810`、`0x88784C`、`0xB8692C`、`0x3DA8FC`）。已知 `0x3DA8FC` 在第 6.2 节被额外挂一个 init 入口 hook，属于**有意共享**；其余三个若出现在站点表里，`gen_sites.py` 报错。
- REPLACE 站点的 `replay[0..1]` 必须来自真实反汇编，不允许留 0。

### 3.7 性能含义

- hook 目标会被 `mark`，导致**经过该地址的链式分支回到 dispatcher**（`a32_runtime.h:97-113` 的 `a32_hooked` 检查；`REVIEW` 的 Q4 深挖）。链预算 64（`arm_recomp32.cpp:105`）意味着热点站点会显著缩短每次 dispatch 的连跑长度。
- 方案 J 每次命中还要多一次 JIT 进出（保存/恢复 8 个通用寄存器 + 8 个 VFP 寄存器、状态 marshal）。
- 因此：站点表里给每类站点标 `expect`（`cold` / `per_frame` / `per_hit`），M4 与 M7 用 `hits` 计数与 `user/recomp_report.txt` 的 JIT 计数交叉验证哪些站点真的热；只有被证实为热的观测型 handler 才考虑改写成 x64 的 CONTINUE 快速路径（3.3 的方案 C）。

## 4 40 个立即数站点

### 4.1 分类总表

上游把这几处叫作"单词补丁"，在本平台**无法**用改字节实现：立即数在导出时被烘焙进 x64（`a32_to_c.h:181-190` 的 MOVW 生成 `c->r[D] = 0x0000IIII;`；`forge_pc.h:229-230` 明确警告改代码字节无效）。每个站点都必须用 hook/stub 复现"改过的立即数"。

| 类 | 站点数 | 上游位置 | 立即数含义 | 新值 | 替换原语 |
|---|---|---|---|---|---|
| A registry `cmp` | 2 | `build.py:164-171` | 注册表条目上界 137 | `137 + N_reg` | P4（整函数 override） |
| B Notes `cmp` | 7 | `build.py:172-175` | Notes 表长度 123 | `123 + 4 = 127` | P2 / P3（循环头） |
| C item `movw` | 28 | `build.py:181-190` | 道具 id 上界 `0xbad/0xbae/0xbaf` = 2989/2990/2991 | 各 + `ITEMS_APPENDED`(30) -> `0xbcb/0xbcc/0xbcd` | P1 / P3 |
| D item 排序数组尺寸 | 1 | `build.py:191` | 记录数组容量 | + `ITEMS_APPENDED` `[I]` 具体算式见 `build.py:204-236` | P1 |
| E 栈帧 `sub/add sp` | 2 | `build.py:192-236` | 排序缓冲区帧大小 | 各 + `delta`（8 对齐） | P1（两处成对改） |
| 合计 | **40** | | | | |

上游把"立即数改写"记在 3 处列表里（`WORD_PATCHES` 9 条 + `item_patches` 31 条 = 40），`REVIEW:78-82` 已核对数量。`N_reg` 与 `delta` 是推导量，不是抄来的常量，故标 `[I]`。

### 4.2 A 类：registry `cmp`（2 处）

- 语义：把"id <-> index 对照表"的长度 137 改成 `137 + N_reg`。表本身由第 5 节的 `reg_pairs` 重定位承载，所以真正的动作是把这条比较的上界放宽。
- 实现：`hook_override(main + off, fn, site)`（`forge_pc.h:273-281`），用 x64 重写这两个函数（它们都是小循环：按 id 线性/二分查 index，或反向查 id）。正常路径不需要 `call_original`（原函数的上界是错的，任何复用都得再纠正一次），但保留 `call_original` 作为诊断开关。
- 为什么不用 stub：该比较通常不在块起点，而是循环体内；用 override 一次性把整个查找替换成读 `mem_read` 出来的新表，代码量最小、语义最清楚。
- 验收：查一个追加怪物的 registry slot（105..111，`STUDY:128-132`）与一个原版 slot，两者都能查到且 index 正确。

### 4.3 B 类：Notes `cmp`（7 处）

- 语义：把 Hunter's Notes 三张表的循环上界 123 放宽到 127（123 + 4 条目；`REVIEW:88-90`）。
- 实现优先级：(1) 若 `cmp` 本身是块起点 -> P2（`stub_cmp`，见 4.7）：stub 里 `movw rT,#127; cmp rN,rT`，然后按原条件跳转到原目标或落到原后续块；(2) 若 `cmp` 在循环头块内且前驱块起点可用 -> P3 前缀重放；(3) 若该函数是自包含的 `notes_unlock` 查找/累加（`main.c` 侧已有 `notes_init` 的复制逻辑），改 P4 override 更省事。
- 注意：7 处不一定都是 123；`[I]` 需反汇编逐条确认后填进 `data/immediates.toml`（每行：`site, orig_imm, new_imm, class, primitive, evidence`）。

### 4.4 C 类：item `movw` 边界（28 处）

- 语义：道具 id 上界从 2989/2990/2991 放宽到 +30 后的 3019/3020/3021（`0xbcb/0xbcc/0xbcd`）。上游断言每个站点都是 `movw #0xbad/0xbae/0xbaf`（`REVIEW:78-82`），`gen_sites.py` 也要在生成期断言一次。
- 实现：P1（`stub_movw`）：把站点处那条 `movw` 换成一个 3 词 stub（`movw rR,#new` + `ldr pc,[pc,#-4]` + `.word main+site+4`）。若 `site` 是块起点，pre hook JUMP 到 stub；stub 执行完跳到 `site+4`，若那里不是块起点，导出会让 JIT 跑完这个块再回 x64（`forge_pc.h:211-213`），正确但慢，对冷路径可接受，对热路径用 P3 把后续几条指令一并放进 stub。
- `[I]` 这 28 处里有多少是每帧热路径（例如物品栏/图标绘制）未知，需要 `hits` 计数与帧时间验证；若超过 ~4 处进热路径，考虑 4.8 的预打补丁路线。

### 4.5 D 类：item 排序数组尺寸（1 处）

- 语义：排序/暂存数组按道具总数分配，需要 +`ITEMS_APPENDED`。
- 实现：P1 同类，值由 `build.py:204-236` 的算式换算；`[I]` 相关单条记录的字节数需反汇编确认（上游表达式里含有记录尺寸常量）。
- 与 E 类联动：数组若在栈上（E 类是它的帧），D/E 必须用同一组常量，否则会越界写到栈上别的局部变量。

### 4.6 E 类：栈帧 `sub sp` / `add sp`（2 处）

- 语义：item 排序函数的栈缓冲区按新容量放大。上游同时改 `sub sp,#X` 与函数尾部的 `add sp,#X`，两边 +`delta`。
- 为什么必须成对：只改 `sub`（`#X+delta`）而 `add` 仍是 `#X`，函数返回后 sp 比入口低 `delta`，每次调用都下移 `delta` 字节，最终越过栈底；只改 `add` 则 sp 越过来时的返回地址，直接破坏调用者。
- 布局可行性：帧基址随 sp 下移 `delta`，所有 sp 相对的偏移量不变，数组上方（原本紧邻其它局部变量处）因此多出 `delta` 字节可写；前提是数组位于帧的低地址端且向上生长。`[I]` 该前提需反汇编确认；若不是，就只能走 P4（整函数 override，自带缓冲区放在 arena C）。
- `delta` 取 8 的倍数（AAPCS 栈对齐），具体值 = `ceil((new_count - 2991) * record_bytes / 8) * 8` `[I]`。
- 两处都要检查块起点：`sub sp` 在序言里（`push {...,lr}` 之后，通常与 push 同块，序言块起点在上一条），尾部 `add sp` 往往自成块起点。若不在块起点，P3。

### 4.7 四个替换原语

| 原语 | 用途 | 形态 | 约束 |
|---|---|---|---|
| P1 `stub_movw` | 单条 `movw` 常量 | `movw rR,#new` + `ldr pc,[pc,#-4]` + `.word 续点` | 站点须是块起点；续点若无块则走 JIT |
| P2 `stub_cmp` | `cmp rN,#imm` 及其后的条件分支 | `cmp rN,#imm`（可编码）或 `push {r12}` + `movw r12,#new` + `cmp rN,r12` + `pop {r12}`，再 `ldr<cond> pc,[pc,#-4]` + `.word taken` + `ldr pc,[pc,#-4]` + `.word fallthrough` | 需要原分支目标与 fall-through 两个绝对地址；都在 `data/immediates.toml` 里登记；形态选择见本节末 |
| P3 前缀重放 | 站点不在块起点但同函数内有前驱块起点 | 手写 ARM32 stub：重放前缀（不含 pc 相对访存/分支/push/pop）-> 进入该站点 trampoline | 前缀长度上限 24 条；含 pc 相对指令时把字面量搬成绝对 `ldr`（stub 自带常量区） |
| P4 整函数 override | A 类、B 类自包含查找、E 类的兜底 | `hook_override` + x64 重写，用 `mem_read`/`mem_write` 操作 arena 里的表 | 站点须是块起点；重写必须覆盖原函数的全部副作用（含错误分支） |

所有 P1/P2/P3 的 stub 由 `immediates.cpp` 在 init 时按 `data/immediates.toml` 装配并 `mem_write`，**首次执行前完成**（R4）。stub 与 trampoline 共用一个 bump 池，池大小在 `module.blob` 头部给出上限（按 128 B/站点 + 128 B/立即数 + 8 KB 余量估算，约 28 KB `[I]`）。

P2 的第二个操作数优先级：(1) 若新值能被 ARM 的 imm12（8 位值 + 2 位循环右移）编码，直接 `cmp rN,#imm`，不占寄存器、不动 sp、无活跃性假设；A 类（`137 + N_reg`）与 B 类（127）都落在这里。(2) 否则（例如 3019/3020/3021 这类道具上界）用 `push {r12}` + `movw r12,#new` + `cmp rN,r12` + `pop {r12}` 再条件跳转：`pop` 不改条件标志，sp 在两条出口前都已复原，因此不必假设 r12 在分支之后是否活跃；stub 内没有任何调用，4 字节的临时 misalign 无影响。生成器按此规则选择形态并把选择结果写进 `data/immediates.toml`。

### 4.8 备选路线：预打补丁 + 重新导出

若 C/D/E 三类的 stub 方案在实机上证明脆弱（或热路径成本过高），可改走"构建期解决"：

1. 用户把自己的 exefs 目录 dump 交给导出器：目录路径会被整份复制，若其中有 `exefs/` 子目录则用它（`game_export.cpp:3895-3908`）。
2. 导出器只认 dump 本身，**不**套 mod 的 LayeredExeFS、`.pchtxt`、IPS（`game_export.cpp:1158-1163` 的注释；`INSTALL.md:160-163`）。因此"先把 40 个立即数写进 NSO 副本，再导出"是有效的。
3. 我们只发布偏移+原值+新值（不发布游戏字节，GPL/合规上等价于上游 IPS 的做法，但更干净），由脚本对用户自己的 dump 打补丁。
4. 收益：40 个站点、`LIT_PATCHES` 之外的所有立即数问题一次性消失，重编译代码直接带上正确常量。代价：用户必须重导出（10-20 分钟，`INSTALL.md:99-111`），且 16 处字面量仍必须运行期改（它们指向 `mem_alloc` 里的表，地址只有运行时才知道）。
5. 结论：**不作为 v1 主线**，作为 M7 的降级方案保留；若采用，第 4 节的 stub 全部停用（`data/immediates.toml` 里标 `solved_at_build=true`），避免双重修改。

## 5 数据面：11 张表与 16 处字面量改写

### 5.1 为什么数据写是安全的（证据链）

命题：**改动 guest 数据（`.data`、`.rodata`、以及 `.text` 里的字面量池）会被重编译代码看到；改动 code 不会。**

1. `forge_pc.h:229-230`：改 guest 内存里的代码字节不改变重编译游戏，因为代码已被翻译成 x64。该句只针对 code。
2. 重编译代码的每次访存都是一次函数调用，不是烘焙常量：`a32_to_c.h:372-383` 把所有 LDR/STR 降到 `a32_read32(c, addr)` / `a32_write32(c, addr, v)`。
3. 字面量寻址只烘焙**地址**，不烘焙**值**：`a32_to_c.h:73-77` 的 `RAlign` 对 `Rn == 15` 生成 `PcConst((pc + 8) & ~3)`，`a32_to_c.h:346-349` 用它算 `addr`，随后仍然 `a32_read32(c, addr)`。因此 `main` 里 `ldr rX,[pc,#imm]` 读的那个字面量词，是在每次执行时从 guest 内存取的。
4. 取数路径每次走页表/宿主指针，不缓存数据值：`a32_host_runtime.c:14-64`（`host_ptr` + `same_page` 快路径，失败则走 `host->load`）。
5. `mem_write` 走 `Memory::WriteBlock`，它只检查地址是否被映射，不检查页的写权限（`memory.cpp:250-253, 322-333`），所以写 `main` 的 R-X `.text` 页也会成功 `[I]` 运行时确认（V2）。成功的是**写数据**：该页里的 x64 副本不受影响，代码行为只能靠 hook 改变（见 4.1）。

因此 16 处字面量改写是安全的（机制层面已由代码证明；运行期行为仍需 V2 确认）。反之，第 4 节的 40 处是 code 立即数，只能走 stub/override。

### 5.2 11 张表

| # | 表 | 内容/键 | 重定向方式 | 依据 |
|---|---|---|---|---|
| 1 | `reg_pairs` | id <-> index 对照 | 6 处字面量 | `build.py:121-133, 138-149` |
| 2 | `reg_dti` | 每个 index 的 class DTI | 1 处字面量 | 同上 |
| 3 | `reg_cro` | `roEmNNN.cro` 名字表 | 4 处字面量 | 同上 |
| 4 | `reg_manifest` | 51 词文件清单 | 直接数据字 `0x1839618` | `main.c:42-45` |
| 5 | `reg_flags` | 每 index 标志 | 1 处字面量 | `build.py:138-149` |
| 6 | `reg_icon` | UI 图标 id | 1 处字面量 | 同上 |
| 7 | `reg_u16` | u16 表 | 1 处字面量 | 同上 |
| 8 | `reg_mapicon` | 地图图标尺寸类别（按 em 号索引） | 直接数据字 `0x183fff0` | `STUDY:111-112, 154` |
| 9 | `notes_unlock` | Notes 解锁位 | 1 处字面量（Notes 2 处之一） | `build.py:148-157` |
| 10 | `notes_order` | Notes 排序 | 1 处字面量（另一处） | 同上 |
| 11 | `notes_list` | 由 `notes_init` 组装 | 直接数据字 `0x183ec4c` | `main.c:44-45` |

计数核对：8 张 registry（含 `reg_mapicon`）+ 3 张 Notes = 11（`REVIEW:87-91`）；字面量 14（registry 6+1+4+1+1+1）+ 2（Notes）= 16（`STUDY:103-107`）；另外 3 个直接数据字覆盖 `reg_manifest`、`reg_mapicon`、`notes_list`。三套数字互相闭合，不允许"再加一张 map icon 表"式的重复计数。

表项数：registry 89..142 项，Notes 123 项（+4 后 127）（`STUDY:154`）。`[I]` 每张表的具体项数需从 `main.c`/build 脚本的实际常量抄录进 `data/tables.toml`，本文不臆测单表数值。

### 5.3 16 处字面量改写（插件侧）

- 动作：init 时对 16 个 `(literal_addr, new_value = arena + table_off)` 逐个 `api->mem_write(main_base + literal_addr, &value, 4)`。
- 顺序：必须在任何读取这些表的游戏代码执行前完成。init 在 `sApp::run` 入口，早于存档/道具/装备表的实际使用 `[I]`（V5 验证）。
- 校验：每次写后 `mem_read` 回读比对，把 `(literal_addr, old, new, ok)` 写进日志；任何一个 `ok == false` 都置 `disabled`（表都不对时继续跑会崩在别处，更难查）。
- 幂等：写入是固定值，重复 init 不会叠加偏移。但热重载后这些字面量仍指向**上一个** arena，而 `forge_onUnload` 已 `mem_free` 它（R10、6.6），所以重载后必须重启游戏；本移植靠 `[plugins] hot_reload = false` 从源头避免。

### 5.4 3 个直接数据字（模块侧）

由 `module_init` 通过上游的 `redirect()`（`main.c:15-24, 42-45`）写入 `0x1839618`、`0x183fff0`、`0x183ec4c`。本移植不改这段逻辑，只保证两件事：

1. `MAIN_BASE` 已是运行期值，`redirect` 写到正确的基址（6.4）。
2. 这 3 个数据字的写入发生在 16 处字面量之后（插件先写字面量，再放行 `module_init`，见 6.5 的顺序表），因为两者可能指向同一张表的不同视图 `[I]`。

### 5.5 写入时序与原子性

```
forge_onInit:
  1 resolve bases                                               (mem_getModuleBase)
  2 load blob, mem_alloc, copy sections, relocs, verify readback
  3 write 16 literal words + readback                           (5.3)
  4 generate trampolines + immediate stubs                      (3.4, 4.7)
  5 install 113 hooks + init-entry hook                         (3.5, 6.2)
  6 state = Ready

module_init (guest, 第一次 kUpdate；6.2):
  7 复制 11 张表到模块 bss、按注册怪物增长、注册 class          (上游 main.c:29-48，原样保留)
  8 写 3 个重定向数据字                                         (5.4)
  9 由宿主装载的数据清单解析各怪物 .bin                        (7.3)
```

分工要点：第 7/8 步是**模块自己的代码**，插件不重复做（上游已实现并有验收路径），插件只保证第 3 步之后那些字面量与数据字指向的地址是模块 bss 的真实地址（基址由 6.4 的运行期变量提供）。模块 bss 在 arena A 内，随 blob 一起装载，因此增长后的表天然可用。

时序：第 1-6 步在游戏线程、`sApp::run` 入口完成；第 7-9 步在随后第一帧、同为游戏线程。两段之间游戏不会读取这些表 `[I]`（V5 验证）。运行期的表追加（道具/装备表在加载时通过 hook 追加）仍是上游原有逻辑，按上游的假设处理（`STUDY:133-134`）。

### 5.6 校验与回滚

- `data/tables.toml` 与 `data/immediates.toml` 是"契约文件"：每行带 `evidence`（上游文件:行或反汇编地址）。`tools/` 里两个生成脚本在生成 `site_table.inc` 时断言与上游常量一致（见 4.4）。
- 回滚：`mh4u.ini` 里 `[mh4u] enabled = false` 时，插件在 `forge_onLoad` 就返回，`forge_onInit` 什么都不做，游戏完全原样。运行期崩溃恢复靠 Forge 的 `[crash] auto_disable`（`forge-pc/README.md` 的崩溃处理节，`STUDY` 引用）。

## 6 模块初始化路径

### 6.1 为什么不能用 `api->call`

- `api->call` 只适用于"从不等待"的函数：内部出现系统调用就整体失败（返回 false，寄存器回滚，已写的内存保留）（`forge_pc.h:206-211`）。
- 上游 `module_init` 第一件事就是 `logf`（`main.c:31`），而 `logf` 最终是 `svc_output_debug_string`（`util.c:28-35`）。因此"在 `forge_onInit` 里用 `api->call` 跑 `module_init`"会在第一条日志处失败（`REVIEW:106-116`）。
- SVC 本身在"跑在模拟 CPU 上的 guest 代码"里是正常的：A32 侧 `a32_svc` 会挂起并交给内核服务（`a32_runtime.h` 的 `a32_svc` 契约；`svc.h:423-427`、`svc_debug_string.cpp:73-79`）。差别只在 `api->call` 这条受限路径上。

结论：**模块代码一律不在 `api->call` 里执行**，而是通过 hook + JUMP 进入，让它作为普通 guest 执行跑在 JIT 上。

### 6.2 用 hook 进入 `module_init`

```
forge_onInit 末尾:
  api->hook_create(main_base + 0x3DA8FC, OnInitTick, nullptr, nullptr)  /* kUpdate，逐帧命中；user 为 NULL：此 hook 不属于 113 站点 */
OnInitTick(cpu, user):
  if (state != Ready) return FORGE_HOOK_CONTINUE;          /* 失败/未装载：不干预 */
  if (guest_init_done) return FORGE_HOOK_CONTINUE;
  guest_init_done = true;
  cpu->r[15] = init_stub;                                  /* arena 里的 8 词 stub，见下 */
  return FORGE_HOOK_JUMP;
init_stub:  push {r4, lr} ; ldr ip,[pc,#-4] ; .word entry ; blx ip ; pop {r4, lr} ;
            ldr ip,[pc,#-4] ; .word main+0x3DA8FC ; bx ip
```

- 选 `0x3DA8FC` 的原因：它是 Forge 已用并文档化的逐帧 `update`（`loader.cpp:64`），保证 `module_init` 在 init 后的第一帧就跑；且该地址必然命中一次以上。`[I]` 若 `0x3DA8FC` 在某些场景（例如标题画面）不逐帧调用，退化为任意一个逐帧站点（例如 `dmg_sub`/`stage_frame` 之类，`STUDY:92-99`）。
- 代价：那一帧里 Forge 自己的 `kUpdate` post hook 不会运行（JUMP 后 post 不触发，`forge_pc.h:141-142`），因此插件的 `forge_onUpdate` 少一次回调；下帧恢复。见 R13。
- `init_stub` 用 `push {r4, lr}` / `pop {r4, lr}`（两个词，保持 sp 的 8 字节对齐），`module_init` 的 `bx lr` 因此回到 stub，然后跳回被 hook 的函数重新执行（此时 `guest_init_done` 已置位，`OnInitTick` 直接 CONTINUE）。这就是"用 JUMP 代替 call"的完整形式：不需要 `api->call`，也不改变被 hook 函数的语义。
- 失败处理：`module_init` 内部若崩溃（JIT 里除零/非法访存），`guest_init_done` 已置位，不会重试死循环；导出会打印异常与 PC，可据此定位。

### 6.3 `logf` 替换

- 上游：`logf(fmt, ...)` 组装字符串后 `svc #0x27`（`util.c:28-35`）。
- 本移植：`guest/logring.c` 提供同名 `logf`，把记录 `[u16 长度][字节]` 追加进 arena 里的 64 KB 环缓冲（单生产者为主、多个游戏线程可能同时写，因此用 `ldrex/strex` 原子取号 `[I]` 是否需要：上游的日志点是否会被多线程同时触发未验证）。
- 插件在 `forge_onUpdate(dt)`（每帧、游戏主线程，`forge_pc.h:41-44`）排空环，逐条 `api->log(FORGE_LOG_INFO, "mh4u", text)`。日志同时落在 Forge 的 game log 与 `Forge/forge.log`（`forge_pc.h:184-185`）。
- 构建期硬约束：`tools/check_no_svc.py` 扫描 `module.blob` 的 `.text`，出现 `svc`（`0xEF...`）即构建失败；唯一豁免是 C 库 `__aeabi_*` 里的已知实现（若存在，逐个登记）。这样"忘记改某个 SVC"不会拖到运行期才炸。
- `[I]` 上游是否还在别处用 SVC（例如睡眠/内存屏障）：需在构建期扫描结果里逐条确认；若存在"必须保留"的 SVC，它只能在 JIT 路径上运行（即不能在 `api->call` 内），本设计已保证这一点。

### 6.4 基址与 `game.h`

- 上游 `game.h:6-9` 把 `MAIN_BASE` 写死成 `0x704000`，所有地址是 `MAIN_BASE + offset`。
- 本移植：`game.h` 改为 `extern uint32_t g_main_base; extern uint32_t g_self_base;`，`#define MAIN_BASE g_main_base`（保持其余源码不变），两个变量由插件在装载后 `mem_write` 写入（偏移来自 blob 符号表）。
- 必须核对：`mem_getModuleBase("main")` 是否等于 `0x704000`。Forge 自己的 MHGU 偏移只假设"偏移一致"，不保证基址（`forge_pc.h:188-191`，`loader.cpp` 的模块表）。`[I]` 本导出上的实际基址（V1 打印）。
- 若基址不是 4 KB 对齐/或高地址，注意模块内的 `bl` 只在 blob 内（范围安全），跨到 `main` 的调用必须走绝对地址（`-fPIC` 下的 `ldr ip,[pc,#imm]; blx ip`），构建期用 `check_no_svc.py` 同族的检查确认 `.text` 里没有指向 `main` 的 `bl` 立即数 `[I]`（若链接器把 `extern` 函数调用编成 `bl`，装载时 base 一变就会跳错；发现即在源码里改成函数指针调用）。

### 6.5 初始化顺序与失败语义

| 步 | 动作 | 失败后果 | 处理 |
|---|---|---|---|
| 1 | 读 ini、定位 mod 目录、读 blob | 无 | 打日志，`disabled`，什么都不 hook |
| 2 | `mem_alloc` arena | 堆满（32 MB 上限，`mod_host.cpp:75`） | 打 ERROR 并上报所需字节；`disabled` |
| 3 | 段复制 + 回读 | 数据不一致 | 打 ERROR（附首个不一致地址）；`disabled` |
| 4 | 重定位（仅 `R_ARM_RELATIVE`） | 出现别的类型 | 打 ERROR + 符号名；`disabled` |
| 5 | 16 字面量 + 回读 | 写失败 | 打 ERROR；`disabled` |
| 6 | 装 hook（113） | 个别站点非块起点 | 该站点进 B2/B3（3.5），其余照装；汇总进站点报告 |
| 7 | init 入口 hook | 失败（例如地址冲突） | 打 WARN，退化为"不初始化"（表已写、hook 已装但模块不工作，此时应回滚为 `disabled` 并对每个 hook 调 `hook_enable(hook, false)`） |
| 8 | `module_init`（guest） | 崩溃/超时 | 见 6.2 的失败处理 |

原则：**任何一步失败都不把游戏留在半初始化状态**。`disabled` 后所有 pre hook 立即返回 CONTINUE（3.3 的回调第一行检查状态）。

### 6.6 卸载

- `forge_onUnload`：对每个 hook 调 `hook_enable(hook, false)`（`forge_pc.h:205`）、排空并停止日志环、`mem_free` 所有 arena（`forge_pc.h:49-51, 245-251`）。
- 不恢复已写的 16 个字面量与 3 个数据字：它们是"指向 arena"的指针，arena 释放后失效；因此卸载后必须重启游戏（与上游"删 mod 需重开"一致）。这点写进 README 与 `[plugins] hot_reload = false`（R10）。

## 7 romfs 部分（不改）

### 7.1 文件布局

与上游完全一致（`STUDY:116-123`）：

```
<mod>/romfs/nativeNX/mh4uport/<name>.bin        每只怪物的数据文件（ITEM/BUI/TBLA/TXTA/NOTE/NAME/SKIL/PWLF/FILE 段）
<mod>/romfs/nativeNX/arc/enemy/emNNN_VV.arc     怪物 arc（含 0xCC 字节 / 51 字段清单）
<mod>/romfs/nativeNX/...                        BGM、任务、stage 别名、地图 UI、装备模型、武器音效
```

### 7.2 谁读什么

| 内容 | 读者 | 本移植的处理 |
|---|---|---|
| arc、BGM、任务、stage 别名、地图 UI、装备模型、武器音效 | **游戏自己的** romfs（LayeredFS） | 完全不变：文件 mods 在导出上"照常工作"（`forge-pc/README.md:36-40`，`STUDY:157` 的 M8 行） |
| `<name>.bin` 数据文件 | **模块代码**（上游 `port.c` 的 `nn::fs` 读取，`port.c:20-33, 88-108`） | 改成宿主读取（7.3） |
| "怪物是否已安装"的判定 | 模块（`GetEntryType` 探 arc，`port.c:151-165`） | 改成宿主 `std::filesystem::exists`（7.4） |

### 7.3 数据文件的宿主读取

1. 插件从自身路径推出 mod 根：`api->mods_dir()` 给 `<mods>/<title id>`，插件路径给 `<mods>/<title id>/<mod>/plugins/mh4u.dll`，取两级父目录（`forge_pc.h:233-236`）。
2. 对 blob 清单里的每只怪物，读 `<mod>/romfs/nativeNX/mh4uport/<name>.bin`（`std::filesystem::u8path`，UTF-8 路径，Windows 走宽字符）。
3. 内容写进 arena C（`mem_write`），并把 `(name, ptr, size)` 填进同一 arena 里的"数据清单"表。
4. `module_init` 里的解析器（上游 `port.c` 的段解析）改成：从数据清单取指针（清单地址由 blob 符号表给出，插件 `mem_write` 填表），不再调 `nn::fs`。段格式不变，解析代码不变（只换"缓冲区从哪来"）。
5. 好处：不依赖导出的 HLE 文件系统能否服务 guest 的 `nn::fs`（这正是 `STUDY:156` 的 M7 里未验证的部分），把不确定性从"guest 跨引擎调用"降级为"宿主 fopen"。

### 7.4 "已安装"判定

上游用 arc 是否存在判定（`port.c:151-165`）。本移植：插件在 init 时检查 `<mod>/romfs/nativeNX/arc/enemy/emNNN_VV.arc` 是否存在，只把存在的怪物写进数据清单与注册流程。这与上游语义一致（arc 在 = 已安装），且不需要 guest 侧任何文件系统调用。

## 8 风险与实机验证计划

### 8.1 风险表

| ID | 风险 | 影响 | 缓解 | 验证 |
|---|---|---|---|---|
| R1 | 113 个站点中有一部分不是重编译块起点 | 该站点无法 hook，行为缺失（例如某武器类装备表不加载） | 3.5 的 B1->B2->B3；站点报告立即暴露 | V1 |
| R2 | 40 个立即数站点的块边界/前缀形态与预期不同 | 该处行为不正确（道具上限、Notes 页数、栈越界） | 逐个反汇编并登记进 `data/immediates.toml`；P3/P4 兜底；4.8 备选 | V6, V7 |
| R3 | 注入代码在 JIT 上的吞吐 | 移植怪物的行为代码每帧大量跨界调用，可能掉帧 | 先测量（V3/V10）再谈优化；必要时把热点 handler 改成 x64 快速路径（3.3 C） | V3, V10 |
| R4 | JIT 取指页缓存：运行期改代码无效（`arm_dynarmic_32.cpp:42-52`） | 任何"运行时补 stub"的设计都会静默失效 | 所有 guest 代码在首次执行前写完；stub 形态在 init 定稿；不做运行期代码改写 | 代码审查 + V4 |
| R5 | 数据写可见性只在源码层证明 | 若某处数据被烘焙（例如编译器把常量折叠进重编译代码），字面量改写会失效 | 16 处逐条回读 + 功能观察（怪物出现在列表里）；失效则该处改 hook | V2, V5 |
| R6 | `mem_alloc` 总容量 32 MB（`mod_host.cpp:75`），且是所有插件共享 | arena 分配失败 -> 全盘不工作 | init 前估算并上报（代码 + 表 + 数据池 + stub 池 + 环）；按需只装一个数据文件池 | V4 |
| R7 | 模块内残留 SVC | 在 JIT 路径上虽能服务，但成本高；一旦进入 `api->call` 就失败 | `check_no_svc.py` 构建期拦截；`logf` 改环缓冲 | V4 |
| R8 | 并发：hook 回调可能在多个游戏线程上并发（`forge_pc.h:41-44`） | 模块全局状态竞争（上游是单线程假设） | 站点表只读；日志环用原子取号；不在回调里分配；统计计数器用 relaxed 原子 | V6, V10 |
| R9 | 路径编码（中文用户名等） | 数据文件读不到，怪物"未安装" | 全程 UTF-8 -> 宽字符；日志打印解析后的绝对路径 | V1 |
| R10 | 热重载 | 注入代码与已写数据回不来 | `[plugins] hot_reload = false`（`forge_pc.h:50-52, 214-215`）；卸载后必须重启（6.6） | V11 |
| R11 | 版本耦合 | 换导出器版本/游戏补丁即全部偏移失效 | 启动时校验 build id 与导出器版本，不匹配就 `disabled` 并给出明确日志 | V1 |
| R12 | 许可 | 上游 GPL 代码混合进 MIT 仓库 | 插件与模块同为本仓库、GPL-3.0-or-later；`NOTICE.md` 记录；不复制上游注释到文档 | 发布检查 |
| R13 | init 入口 hook 与 Forge 自己的 `kUpdate` post hook 共享地址 | 首帧 `forge_onUpdate` 不触发 | 无副作用（只少一帧）；日志记录该帧 | V4 |
| R14 | 存档兼容 | Notes 用 5 个空闲存档位；registry 用 105..111 | 与上游一致；删 mod 后读档的注意事项写进 README `[I]` | V9 |

### 8.2 实机验证计划（全部 TODO，用户执行）

所有条目都未执行；每条给出"目的 / 操作 / 判据 / 产出"。

- **V1 基线与站点探测** `TODO(user)`：装 Forge + 只有探针逻辑的插件（不装载模块）。看 `Forge/forge.log`：`main` 基址、`mem_getModuleBase` 与 `user/recomp_report.txt` 的模块基址一致；`<mod>/mh4u_sites.txt` 列出 113 个站点的 `is_block` 结果与分桶计数。判据：>= 100 个 B1 `[I]` 预期值；任何 build id 不匹配必须在该文件里明确报出。
- **V2 数据写可见性** `TODO(user)`：对 `LIT_PATCHES` 的第一处字面量（地址、旧值、新值由 `data/tables.toml` 给出）`mem_write` 一个可观察的值并回读；进游戏后观察 id->index 搜索读到的是新表还是旧表（插件日志打印表首项）。判据：读到新表。
- **V3 JIT 基准** `TODO(user)`：从 hook JUMP 到 16 条指令的 stub，调用一个无副作用游戏函数并返回；计时 10 万次。判据：功能正确 + 单次成本落在可接受范围（用于推算 R3）。
- **V4 装载与初始化** `TODO(user)`：完整初始化。判据：`forge.log` 出现上游 `module_init` 的日志序列（经环缓冲转发）、`guest_init_done` 置位、没有 `guest call ... failed`、`recomp_report.txt` 无异常 JIT 激增。
- **V5 表重定位** `TODO(user)`：打开装备列表、Hunter's Notes、地图。判据：3 只怪的数据出现在列表尾部，翻页不崩，地图图标不串行。
- **V6 113 hook 全装 + 普通任务回归** `TODO(user)`：打一场原版任务。判据：无新崩溃；`recomp_report.txt` 的 missing block 计数不因本 mod 暴涨；站点命中计数合理（冷站点为 0）。
- **V7 第一只怪端到端**（建议 Gogmazios）`TODO(user)`：任务里 spawn、移动、攻击、受击、部位破坏、击杀、剥取。判据：全流程无崩溃；伤害数字/血量正常；剥取给正确素材。
- **V8 内容面** `TODO(user)`：三只怪 + 移植任务出现在任务板 + 完成与 clear 标记 + Notes 解锁 + 装备生产。判据：与上游 Switch 版行为一致（对照上游 `notes/porting-guide.md:286-296` 的验收路径）。
- **V9 存档** `TODO(user)`：保存 -> 退出 -> 重进。判据：registry slot、tally、Notes 位、任务 clear 标记都在。
- **V10 性能** `TODO(user)`：同一场景对比（原版 vs 移植怪）。判据：帧时间差在可接受范围；记录 JIT fallback 计数增量。
- **V11 禁用与卸载** `TODO(user)`：菜单关 mod / 重启 / 只删插件。判据：游戏正常、存档可读。
- **V12 Linux 冒烟** `TODO(user)`：同一源码出 `mh4u.so`，重复 V1 + V7 的最小路径。

### 8.3 停用开关

- `mh4u.ini`：`[mh4u] enabled`（总开关）、`[mh4u] log_ring`（是否转发环缓冲）、`[mh4u] sites = all|core`（只装 core 65 个站点，用于二分定位）。
- Forge：`[plugins] hot_reload = false`；崩溃时 `[crash] auto_disable`。
- 站点表：单个站点可 `disabled=true`（例如 R1/R2 未解决的站点），插件跳过安装并在日志里明示"degraded"。

## 9 里程碑

| 里程碑 | 内容 | 退出判据 | 依赖 |
|---|---|---|---|
| M0 | 探针插件：ini、日志、模块基址、113 站点 `is_block` 报告 | V1 通过；`mh4u_sites.txt` 有分桶结论 | 无 |
| M1 | blob 装载器：解析、`mem_alloc`、段复制、`R_ARM_RELATIVE`、符号、回读自检 | 装载后 arena 内容与 blob 逐字节一致；日志给出基址 | M0 |
| M2 | 模块初始化：`game.h` 运行期基址、日志环、init 入口 stub | V4 通过：`module_init` 完整跑完 | M1 |
| M3 | 数据面：11 张表 + 16 字面量 + 3 数据字 | V2、V5 通过；每处写有回读日志 | M2 |
| M4 | hook 面：113 trampoline + pre 回调 + 命中计数 + 分桶兜底 | V6 通过；未解决的站点在报告里列清 | M1 |
| M5 | 立即数面：A-E 五类按 4.7 实现 | V7 的道具/Notes/装备相关子项通过 | M3, M4 |
| M6 | 内容端到端：三只怪 + 任务 + 装备 + 存档 | V7、V8、V9 通过 | M4, M5 |
| M7 | 性能与发布：热点优化、README/NOTICE、Linux 构建、4.8 备选评估 | V10、V11、V12 通过 | M6 |

排序理由：M1-M2 先把"能不能跑"钉死（最不可回退的部分），M3 先于 M4，因为表不对时 hook 的观测全是噪声；M5 依赖 M3/M4 的日志与命中能力来定位每个立即数站点。

## 附录 A 未确认项（`[I]` 汇总）

1. `main` 在导出中的实际基址与 `0x704000` 的关系（1.3、6.4）。
2. 113 个站点里 B1/B2/B3 各多少（3.5）。
3. `N_reg` = 3 的推导（每注册一只怪 +1）（1.3、4.2）。
4. 7 处 Notes `cmp` 的原值是否都是 123、新值是否都是 127（4.3）。
5. 28 处 item `movw` 的原始常量分布与其中热点数量（4.4）。
6. item 记录字节数与 `delta` 的精确算式（4.5、4.6）。
7. 排序缓冲区是否位于帧低地址端、向上生长（4.6 的可行性前提）。
8. 16 处字面量在 `sApp::run` 之前是否已被读取（5.3）。
9. 3 个数据字与 16 字面量是否指向同一表的不同视图（5.4）。
10. `mem_write` 对 `main` R-X 页的实际行为（5.1，源码层看是允许的）。
11. 模块是否还残留 SVC（6.3）。
12. 上游日志点是否会被多线程同时触发（6.3 的环缓冲原子性）。
13. `0x3DA8FC` 是否在所有场景逐帧命中（6.2）。
14. 模块 `.text` 里是否存在指向 `main` 的 `bl` 立即数（6.4）。
15. arena 总需求是否装得进 32 MB（R6）。
16. 删 mod 后读档的具体后果（R14）。

## 附录 B 相关文档

- `docs/port-study.md`：可行性研究（机制映射 M1-M12、规模估算、上游缺陷）。
- `docs/port-study-review.md`：其复核（本文采用其更正：40 处立即数、11 张表、SVC 日志、loader 原因、查找缓存 4096）。
- `README.md`：本仓库定位与许可。
- `NOTICE.md`：第三方声明（上游 GPL、SUYU-GURecomp GPL、forge-pc MIT）。
- `MHGU/DEVELOP.md`：同平台的目标常量来源（build id 与段布局）。
