# DEVLOG

## 2026-10-24 建仓
- 用户要求：MH4U 移植到原生版单独建仓，方便管理；许可证用 GPL-3.0-or-later（与上游一致）。
- 导入 `docs/port-study.md`（DsvMh4u，dsv-dev wb2api-remote/deepseek-v4.1-flash:max）和 `docs/port-study-review.md`（DsvMh4uReview，同一路由）。两份原稿留在 `G:/omp works/.tmp/reports-mhgu/`。
- 复核后的结论：
  - 现成的 mod 不能直接用。romfs 部分照常可用；代码部分要改写成 Forge 插件；没有发现根本性障碍。
  - 原报告的规模统计：约 113 个 hook、16 处字面量改写。复核纠正了两项：立即数修改是 40 处（原报告写约 38），重定位表是 11 张（原报告有一处写 12）。
  - 导出版不加载 mod ExeFS，原因是启动时 `override_update=false`，而不是模块列表写死（复核第 8 条）。
  - `api->call` 里遇到系统调用就会失败，所以上游 `module_init` 的 `logf`（走 svc）要改成 `api->log`（复核第 7 条）。
  - 上游有个缺陷：`stats_hunted`（0x3A3930）在 build.py:59 被注释掉了，一直没注册。
- 下一步：写 `DEVELOP.md` 移植设计（加载器插件结构、hook 表、立即数站点的改写方式、模块注入与重定位），还要先跑 BoxProbe 确认 hook 块起点的判定。

## 2026-10-24 设计文档
- `DEVELOP.md`（DsvMh4uDesign，dsv-dev wb2api-remote/deepseek-v4.1-flash:max）；复核 DsvMh4uDesignReview 3 项必查 PASS，提出的 4 处契约缺陷已修。未构建、未运行；V1-V12 是实机待办。

## 2026-10-24 M0 探针
- 站点数为 **113**，与 DEVELOP.md 一致：build.py HOOKS 65 个、gear_hooks.txt 45 个（`build.py:119`），再加 3 个 item 替换 hook `obtained_set/clear/test`（`build.py:193-195`，条件是 `if ITEMS_APPENDED`，移植总会追加道具，所以一定注册）。113 个地址各不相同。`stats_hunted` 在注释里，不计入。（同日初稿误写成 110：当时只解析了 `HOOKS = [...]` 和 gear 表，漏了 :194 的追加；现在生成器会断言 HOOKS 只有这两处追加。）
- 上游本地克隆在 `.tmp/ext/MH4U-port-to-MHGU`（d43eec0）。`tools/gen_sites.mjs` 由它生成 `probe/sites.h`，原始指令字取自 1.4.0 flat 镜像。
- `probe/mh4uprobe.c` 只读，编译 /W4 零警告。伪宿主上 113/113 个指令字都对上；B1/B2/B3 分桶要实机才能定。主模块在导出里的基址是 0x00653000（BoxProbe 实测），上游假设的是 0x704000。
