# 河床构件台账生成与规范包版本化 实施计划

> 日期：2026-08-14
>
> 设计依据：`docs/superpowers/specs/2026-08-14-riverbed-inventory-generation-design.md`
>
> 状态：已实施

## 目标

在不修改历史已发布包的前提下，将河床正式纳入初始构件台账生成，恢复 taxonomy `generatable` 作为唯一准入真值，并发布兼容的新 H21 与评定树版本。

## 已完成任务

- [x] 从 H21 1.0.3 派生 1.0.4，仅将河床 `generatable` 改为 `true`，更新 manifest 版本和稳定摘要。
- [x] 将评定树生成器默认 H21 升级为 1.0.4、默认输出升级为 2.0.3，并按传入 H21 路径生成来源引用。
- [x] 从受控来源生成 `organization-bridge/2.0.3`，锁定 H21 1.0.4。
- [x] 在产品部件目录中保留 `lower.riverbed`，使用 `{name}` 生成一个全桥级条目。
- [x] 删除 `CatalogPart::manually_selectable` 和路由绕过逻辑。
- [x] 新建桥梁时按版本降序加载规范包，默认最新，并在多个包的下拉项中显示版本号。
- [x] 增加 H21 1.0.4、评定树 2.0.3、严格 `generatable`、河床生成及前端默认版本测试。
- [x] 更新构件编号设计、历史实施计划、规范包 README 和 `PROJECT_CONTEXT.md`。

## 发布验收

- 规范包摘要与 manifest 一致。
- 评定树生成器 `--check` 无差异。
- 旧版本目录无改动。
- C++、Python、前端针对性测试通过。
- `git diff --check` 通过。
