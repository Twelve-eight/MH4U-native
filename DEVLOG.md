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
