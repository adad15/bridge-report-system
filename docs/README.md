# 项目文档索引

更新日期：2026-09-23。本索引覆盖此前梳理的 102 份既有文字文档，并加入运行指南、审查修复记录及后续新增技术方案。历史计划和审查正文保留原日期、原测试数字及决策背景；本次新增状态说明不表示重新实施或重新审查全部历史条目。

## 先读什么

1. [README](../README.md)：功能、服务边界和入口。
2. [PROJECT_CONTEXT](../PROJECT_CONTEXT.md)：当前实现、报告约定和未完成事项。
3. [运行与验证](../development.md)：测试命令、隔离数据库、Office 恢复与部署边界。
4. [本轮审查修复状态](2026-09-17-review-remediation-status.md)：4 条 P1、冗余清理、A/B 批次 P2 修复及分项验证。
5. 下列对应模块的现行说明、报告设计或相关历史决策。

## 当前统一基线

- 数据库迁移 001～037；运行时导入合同 5.0。
- 报告管理、年度配置、临时 Word 生成及桥梁图件已实施。
- 前端候选 ID 工厂已移除，手工新增由后端命令创建。
- 原审查 4 条 P1 与 7 条 P2 已分别修复；后续架构优化和验证限制见本轮修复状态。
- 文档中的“目标/要求”不等于“已经实现”，历史验收不等于本轮完整回归。
- 分支、进程、数据库及测试状态具有时效性，接手任务先核对实际环境。

代码、迁移和测试用于判断当前实现；最新确认设计用于判断目标。发生差距时记录缺口，不用旧计划覆盖新实现，也不根据计划勾选推断完成。

## 现行入口与约定

- [PROJECT_CONTEXT](<../PROJECT_CONTEXT.md>)
- [Bridge Report System](<../README.md>)
- [Contracts](<../contracts/README.md>)
- [本地运行、验证与故障恢复](<../development.md>)
- [2026-09-17 审查修复状态](<2026-09-17-review-remediation-status.md>)
- [桥梁检测报告系统 UI 设计规范](<ui-design-system.md>)
- [版本化桥梁规范包](<../standards/README.md>)

## 移动端业务与技术设计

- [移动端构件复查、整桥同步与服务器评分设计](superpowers/specs/2026-09-21-mobile-component-reinspection-design.md)：设计稿 v1.1，包含两级构件分组、历史复查、本地持久化、整桥同步、服务器评分和 APP 结果展示；不规定界面布局，尚未实施正式移动端。

## 当前验收重点

- [2026来源数据库报告输出验收](2026-09-23-source-report-acceptance-validation.md)：100页、430条正式病害、455张照片逐项对账；人工独立封面配置与横向附录连续页码。
- [变更提案010](superpowers/specs/changes/2026-09-23-change-010-source-report-output-acceptance.md)：报告输出验收边界和修复方案。
- [2026来源数据库系统评分独立核验](2026-09-23-source-assessment-audit.md)：5,174件构件、15类部件、3个结构分与全桥81.11分/2类逐层一致；范围不计分与双侧护栏分别核验。
- [变更提案009](superpowers/specs/changes/2026-09-23-change-009-source-database-assessment-audit.md)：只读导出、独立复算和执行记录。
- [来源数据库正式绑定与入库验收](2026-09-21-source-confirmation-acceptance-validation.md)：2026年度430条观测、607条尺寸、455张照片通过独立核验；评分核验见上，报告输出结果见本节最新验收。
- [变更提案008](superpowers/specs/changes/2026-09-21-change-008-source-database-confirmation-acceptance.md)：现有绑定功能、校对、预检和正式入库。

- [来源构件绑定对账与尺寸修复](2026-09-21-source-binding-and-fidelity-validation.md)：5,174件已确认台账、974条实际C++匹配、41条处理建议和修复回归。
- [变更提案007](superpowers/specs/changes/2026-09-21-change-007-source-database-binding-and-fidelity.md)：来源台账调查、绑定对账与尺寸保真。

- [来源软件数据库验收记录](2026-09-21-source-database-acceptance-validation.md)：三年度真实导入、字段和照片对账、台账与正式评分未覆盖项。Word 文件导入路线暂缓。

## 框架优化方案与验证

- [变更提案 003：框架优化 A 批次](superpowers/specs/changes/2026-09-18-change-003-framework-optimization-batch-a.md)：启动可靠性、构建可复现性和验证基线；工程实现已落地；验收证据和未测项见下一条。
- [框架优化 A 批次验证记录](2026-09-18-framework-batch-a-validation.md)：依赖复建、全量回归、启动身份和故障清理，以及尚未覆盖的性能/真机项。

- [变更提案 004：报告与地图可靠性 B 批次](superpowers/specs/changes/2026-09-18-change-004-report-map-reliability-batch-b.md)：清理重试、模板快照、图片/XML、地图并发与重试。
- [B 批次验证记录](2026-09-19-framework-batch-b-validation.md)：完整回归、迁移验证、Office 与合成装配采样，部署和验收边界。

- [变更提案 005：前端加载与依赖整理 C 批次](superpowers/specs/changes/2026-09-19-change-005-frontend-loading-batch-c.md)：页面、图表、面板边界及失败恢复。
- [C 批次验收](2026-09-20-framework-batch-c-validation.md)：626 项前端回归、静态依赖门禁、生产浏览器请求和体积对比。

- [变更提案 006：真实业务链路验收](superpowers/specs/changes/2026-09-20-change-006-integrated-business-acceptance.md)
- [集成验收记录](2026-09-20-integrated-business-acceptance-validation.md)：两处导入修复、小样全流程、真实解析、恢复、性能及版式保留项。

## 历史审查与调查

- [2026-08-28-component-rating-resolution-refactor-code-review](<2026-08-28-component-rating-resolution-refactor-code-review.txt>)
- [2026-08-28-component-rating-resolution-refactor-follow-up-review](<2026-08-28-component-rating-resolution-refactor-follow-up-review.txt>)
- [code-lock-bug-severity-ranking](<code-lock-bug-severity-ranking.txt>)
- [百股大桥2024年系统评分与检测报告评分差异调查报告](<superpowers/2026-08-20-baigu-score-discrepancy-investigation-report.md>)
- [2026-08-16-component-inventory-aggregate-api-design-review](<superpowers/specs/2026-08-16-component-inventory-aggregate-api-design-review.txt>)
- [2026-08-17-component-binding-on-demand-lookup-design-rereview](<superpowers/specs/2026-08-17-component-binding-on-demand-lookup-design-rereview.txt>)
- [2026-08-17-component-binding-on-demand-lookup-design-review-3](<superpowers/specs/2026-08-17-component-binding-on-demand-lookup-design-review-3.txt>)
- [2026-08-17-component-binding-on-demand-lookup-design-review](<superpowers/specs/2026-08-17-component-binding-on-demand-lookup-design-review.txt>)
- [2026-08-18-confirmed-inventory-resolution-consistency-design-rereview](<superpowers/specs/2026-08-18-confirmed-inventory-resolution-consistency-design-rereview.txt>)
- [2026-08-18-confirmed-inventory-resolution-consistency-design-review](<superpowers/specs/2026-08-18-confirmed-inventory-resolution-consistency-design-review.txt>)
- [2026-08-18-confirmed-inventory-resolution-consistency-design-third-review](<superpowers/specs/2026-08-18-confirmed-inventory-resolution-consistency-design-third-review.txt>)
- [2026-08-25-defect-thread-batch-triage-design-review](<superpowers/specs/2026-08-25-defect-thread-batch-triage-design-review.txt>)
- [2026-09-04-report-template-word-generation-design-review](<superpowers/specs/2026-09-04-report-template-word-generation-design-review.txt>)

## 历史实施计划

- [Tech Stack and Project Skeleton Implementation Plan](<superpowers/plans/01-tech-stack-and-project-skeleton-implementation-plan.md>)
- [Bridge Annual Inspection Data Contract Implementation Plan](<superpowers/plans/2026-07-03-bridge-annual-inspection-data-contract-implementation-plan.md>)
- [PostgreSQL Schema and File Archive Implementation Plan](<superpowers/plans/2026-07-03-postgresql-schema-and-file-archive-implementation-plan.md>)
- [Word Importer Prototype Implementation Plan](<superpowers/plans/2026-07-06-word-importer-prototype-implementation-plan.md>)
- [Liaoning Trunk Word Rules Implementation Plan](<superpowers/plans/2026-07-07-liaoning-trunk-word-rules-implementation-plan.md>)
- [Review Workspace Implementation Plan](<superpowers/plans/2026-07-07-review-workspace-implementation-plan.md>)
- [Module 05 Defect Photo Review Hardening Implementation Plan](<superpowers/plans/2026-07-10-review-workspace-hardening-implementation-plan.md>)
- [模块 06 实施计划：合同 1.2 跨模块升级 + 构件病害档案](<superpowers/plans/2026-07-13-component-defect-archive-implementation-plan.md>)
- [管理员永久删除年度检测实施计划](<superpowers/plans/2026-07-15-admin-delete-inspection-year-implementation-plan.md>)
- [账号体系 + 已确认记录的重开校对与照片查看](<superpowers/plans/2026-07-15-eventual-petting-tome.md>)
- [模块 06.5 实施计划：桥梁档案、年度工作台与导入入口重组](<superpowers/plans/2026-07-15-module06-5-bridge-workspace-implementation-plan.md>)
- [模块 06 验收问题修复实施计划](<superpowers/plans/2026-07-15-module06-acceptance-fixes-implementation-plan.md>)
- [管理员新增与批量永久删除桥梁档案实施计划](<superpowers/plans/2026-07-16-bridge-administration-implementation-plan.md>)
- [导入记录删除、无照片规则与校对定位实施计划](<superpowers/plans/2026-07-17-import-record-deletion-and-review-navigation-implementation-plan.md>)
- [版本化桥梁规范模块与系统自主评定实施计划](<superpowers/plans/2026-07-17-versioned-bridge-standards-and-system-assessment-implementation-plan.md>)
- [全桥型构件台账标准编号 + 向导重写 + 旧路径退休 实施计划](<superpowers/plans/2026-07-21-all-bridge-type-inventory-wizard-implementation-plan.md>)
- [构件台账标准化编号引擎 实施计划](<superpowers/plans/2026-07-21-inventory-numbering-engine.md>)
- [导入绑定界面 + 进入校对前置 + 校对页构件只读 实施计划](<superpowers/plans/2026-07-22-import-binding-workspace-implementation-plan.md>)
- [导入保真 + 部件名称→类别对照 + 匹配器改精确匹配 实施计划](<superpowers/plans/2026-07-22-import-fidelity-and-category-matcher-implementation-plan.md>)
- [构件绑定 · 批量查找替换 实施计划](<superpowers/plans/2026-07-24-bulk-binding-replace-implementation-plan.md>)
- [构件绑定 · 用户选择式范围拆分 实施计划](<superpowers/plans/2026-07-24-user-selected-component-range-splitting-implementation-plan.md>)
- [病害与照片两阶段校对实施计划](<superpowers/plans/2026-07-27-defect-photo-two-stage-review-implementation-plan.md>)
- [桥梁有效评定树实施计划](<superpowers/plans/2026-07-28-effective-rating-tree-implementation-plan.md>)
- [病害照片操作模型重构实施计划](<superpowers/plans/2026-07-31-defect-photo-actions-redesign-implementation-plan.md>)
- [实施计划：用来源软件的病害描述模板建立受控映射](<superpowers/plans/2026-08-03-defect-template-mapping-implementation-plan.md>)
- [实施计划：从来源软件的离线库直接导入](<superpowers/plans/2026-08-05-source-system-direct-import-implementation-plan.md>)
- [实施计划：仿照来源软件的桥梁评定树 2.0](<superpowers/plans/2026-08-07-source-faithful-bridge-rating-tree-2.0-implementation-plan.md>)
- [实施计划：病害照片确认状态简化](<superpowers/plans/2026-08-10-photo-review-state-simplification-implementation-plan.md>)
- [实施计划：构件范围拆分预览性能优化](<superpowers/plans/2026-08-12-component-range-split-preview-performance-implementation-plan.md>)
- [河床构件台账生成与规范包版本化 实施计划](<superpowers/plans/2026-08-14-riverbed-inventory-generation-implementation-plan.md>)
- [实施计划：构件台账聚合接口](<superpowers/plans/2026-08-16-component-inventory-aggregate-api-implementation-plan.md>)
- [两侧构件绑定实施计划](<superpowers/plans/2026-08-20-two-sided-component-binding-implementation-plan.md>)
- [病害线索整理工作台实施计划](<superpowers/plans/2026-08-25-defect-thread-triage-implementation-plan.md>)

## 设计与变更历史

- [桥梁报告系统设计文档](<superpowers/specs/2026-07-01-bridge-report-system-design.md>)
- [桥梁报告系统分块开发与技术文档评审机制](<superpowers/specs/2026-07-01-modular-technical-doc-review-design.md>)
- [项目目录结构与技术架构梳理](<superpowers/specs/2026-07-02-模块 1框架.md>)
- [模块 2：核心表流程框架图](<superpowers/specs/2026-07-02-模块 2 核心表流程框架图.md>)
- [辽宁国省干线真实样例适配设计](<superpowers/specs/2026-07-07-liaoning-trunk-real-sample-adaptation-design.md>)
- [辽宁国省干线 Word 规则模块化设计](<superpowers/specs/2026-07-07-liaoning-trunk-word-rules-design.md>)
- [模块 05 人工校对工作台设计记录](<superpowers/specs/2026-07-07-review-workspace-design.md>)
- [模块 05 病害与照片联合校对及确认链路加固设计](<superpowers/specs/2026-07-10-review-workspace-hardening-design.md>)
- [模块 05 校对工作台布局优化设计](<superpowers/specs/2026-07-12-review-workspace-layout-design.md>)
- [模块 06 验收问题修复设计](<superpowers/specs/2026-07-15-module06-acceptance-fixes-design.md>)
- [模块 06.5 补充设计：管理员新增与批量永久删除桥梁档案](<superpowers/specs/2026-07-16-bridge-administration-design.md>)
- [模块 06.5 补充设计：导入记录删除、无照片规则与校对定位](<superpowers/specs/2026-07-16-import-record-deletion-and-review-navigation-design.md>)
- [评分表可选字段与 Python 错误反馈设计](<superpowers/specs/2026-07-16-optional-rating-fields-and-python-error-feedback-design.md>)
- [临时 Word 来源文件生命周期设计](<superpowers/specs/2026-07-16-temporary-word-source-lifecycle-design.md>)
- [版本化桥梁规范模块与系统自主评定设计](<superpowers/specs/2026-07-17-versioned-bridge-standards-and-system-assessment-design.md>)
- [2026-07-21 缺陷修复记录：校对页性能与删除文件清理](<superpowers/specs/2026-07-21-bugfix-review-performance-and-file-cleanup.md>)
- [构件台账标准化编号与导入绑定 重设计](<superpowers/specs/2026-07-21-component-inventory-standard-numbering-and-binding-design.md>)
- [2026-07-23 性能记录：后端 Debug 与 Release 的差距及处理](<superpowers/specs/2026-07-23-backend-debug-vs-release-performance.md>)
- [2026-07-23 变更记录：支座编号规则落地、可选下部构件复选、台账加载性能](<superpowers/specs/2026-07-23-bugfix-bearing-numbering-and-inventory-load-performance.md>)
- [2026-07-23 缺陷修复记录：台账重复加载与导入弹窗停滞](<superpowers/specs/2026-07-23-bugfix-inventory-route-and-import-dialog.md>)
- [2026-07-23 缺陷修复记录：页签切换重复加载](<superpowers/specs/2026-07-23-workspace-navigation-caching.md>)
- [2026-07-24 设计：构件绑定的批量查找替换](<superpowers/specs/2026-07-24-bulk-binding-replace-design.md>)
- [2026-07-24 设计：构件绑定中的用户选择式范围拆分](<superpowers/specs/2026-07-24-user-selected-component-range-splitting-design.md>)
- [“需要处理”提示精确去重设计](<superpowers/specs/2026-07-27-attention-issue-deduplication-design.md>)
- [病害与照片两阶段校对设计](<superpowers/specs/2026-07-27-defect-photo-two-stage-review-design.md>)
- [桥梁有效评定树与现有规范融合设计](<superpowers/specs/2026-07-28-effective-rating-tree-design.md>)
- [病害与评定树分层确定性匹配优化设计](<superpowers/specs/2026-07-30-defect-rating-tree-matching-optimization-design.md>)
- [病害照片操作模型重构设计](<superpowers/specs/2026-07-31-defect-photo-actions-redesign-design.md>)
- [用来源软件的病害描述模板建立受控映射](<superpowers/specs/2026-08-03-defect-template-mapping-design.md>)
- [从来源软件的离线库直接导入检测数据](<superpowers/specs/2026-08-05-source-system-direct-import-design.md>)
- [仿照来源软件的桥梁评定树 2.0 设计](<superpowers/specs/2026-08-07-source-faithful-bridge-rating-tree-2.0-design.md>)
- [病害照片确认状态简化设计](<superpowers/specs/2026-08-10-photo-review-state-simplification-design.md>)
- [2026-08-12 设计：构件范围拆分预览性能优化](<superpowers/specs/2026-08-12-component-range-split-preview-performance-design.md>)
- [河床构件台账生成与规范包版本化设计](<superpowers/specs/2026-08-14-riverbed-inventory-generation-design.md>)
- [构件台账聚合接口设计](<superpowers/specs/2026-08-16-component-inventory-aggregate-api-design.md>)
- [构件绑定改用按需检索](<superpowers/specs/2026-08-17-component-binding-on-demand-lookup-design.md>)
- [已确认台账版本解析的一致性](<superpowers/specs/2026-08-18-confirmed-inventory-resolution-consistency-design.md>)
- [两侧构件绑定（人行道、护栏）](<superpowers/specs/2026-08-20-two-sided-component-binding-design.md>)
- [2026-08-25 设计：病害线索整理工作台](<superpowers/specs/2026-08-25-defect-thread-batch-triage-design.md>)
- [导入构件解析与评分树解析状态分离设计](<superpowers/specs/2026-08-27-import-component-rating-resolution-separation-design.md>)
- [变更提案 001：构件评分与病害详细位置进入年度事实链](<superpowers/specs/changes/2026-07-13-change-001-component-rating-and-defect-location.md>)
- [变更提案 002：轻量账号体系与已确认记录的重开校对](<superpowers/specs/changes/2026-07-15-change-002-accounts-and-post-confirm-reopen.md>)

## 报告设计：持续同步

- [报告模板管理与 Word 即时生成设计](<superpowers/specs/2026-09-04-report-template-word-generation-design.md>)

## 模块规格：先读当前适用说明

- [01 技术栈与项目骨架](<superpowers/specs/modules/01-tech-stack-and-project-skeleton.md>)
- [02 PostgreSQL 核心表与文件归档](<superpowers/specs/modules/02-postgresql-schema-and-file-archive.md>)
- [03 桥梁年度检测数据 JSON 契约](<superpowers/specs/modules/03-bridge-annual-inspection-data-contract.md>)
- [04 Word 导入原型](<superpowers/specs/modules/04-word-importer-prototype.md>)
- [05 人工校对工作台](<superpowers/specs/modules/05-review-workspace.md>)
- [模块 06.5 补充设计：管理员永久删除年度检测](<superpowers/specs/modules/06-5-admin-delete-inspection-year.md>)
- [模块 06.5：以桥梁档案为中心的交互与界面重组](<superpowers/specs/modules/06-5-bridge-centric-interaction-redesign.md>)
- [模块 06：构件病害档案与病害线索整理](<superpowers/specs/modules/06-component-defect-archive.md>)

## 图示与来源材料

- [模块 03 与解析关系化图示](superpowers/specs/diagrams)：03-01～03-06 图保留早期候选/评分设计背景，不能代替合同 5.0 Schema；2026-08-27 解析关系化图配合对应设计阅读。
- [评定树来源截图](superpowers/评定树) 与 [病害类型截图](superpowers/病害类型)：来源证据，保持原样，不用文档更新改写截图内容。
- 前端 _old_pin_test.txt、_moved_preflight_cases.txt 是代码片段，不属于使用或设计文档；本次未修改。
- 依赖目录、构建产物、临时文件及其他 worktree 副本不属于本索引维护范围。

## 维护规则

修改业务行为时同步项目上下文、对应模块和最新设计；合同变动还要同步跨端模型、Schema 与样例。只修改文档时检查链接、路径、配置和状态的一致性，不伪造新测试结果。历史正文仅在必要位置注明后续替代，不批量改写旧测试数字或计划勾选状态。
