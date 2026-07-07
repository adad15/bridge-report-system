# PROJECT_CONTEXT

更新时间：2026-07-07

## 项目一句话

`bridge-report-system` 是一个独立的本地网页系统，用于按桥梁、按年份沉淀定期检测报告、病害、构件和维修记录，并自动生成正式桥梁检测 Word 报告。

## 新窗口启动提示

在新的 Codex 窗口开始工作时，先读取本文件和设计文档：

- `PROJECT_CONTEXT.md`
- `docs/superpowers/specs/2026-07-01-bridge-report-system-design.md`
- `docs/superpowers/specs/2026-07-01-modular-technical-doc-review-design.md`
- `docs/superpowers/specs/modules/01-tech-stack-and-project-skeleton.md`
- `docs/superpowers/specs/modules/02-postgresql-schema-and-file-archive.md`
- `docs/superpowers/specs/modules/03-bridge-annual-inspection-data-contract.md`

推荐首条提示：

```text
继续 bridge-report-system 项目。仓库路径：D:\vs2022 code\bridge-report-system。
当前应该在分支 feature/04-word-importer-prototype。
模块 01、02、03 已完成，现在开始讨论并编写模块 04：Word 导入原型。
请先读取 PROJECT_CONTEXT.md、docs/superpowers/specs/2026-07-01-bridge-report-system-design.md、docs/superpowers/specs/modules/02-postgresql-schema-and-file-archive.md、docs/superpowers/specs/modules/03-bridge-annual-inspection-data-contract.md，然后开始模块 04 的需求讨论和实施计划。先不要写代码。
```

## 当前进度

- 模块 1 `01-tech-stack-and-project-skeleton` 已完成实施并提交到 GitHub。
- 模块 2 `02-postgresql-schema-and-file-archive` 已完成实施：数据库迁移、系统编号工具、归档路径工具和数据库 smoke test 已通过。
- 模块 2 设计文档提交号：`52ee0ab docs: add module 02 schema and archive design`。
- 模块 3 `03-bridge-annual-inspection-data-contract` 已完成实施并推送到 GitHub。
- 模块 3 已定义并实现 `BridgeAnnualInspectionData` JSON 契约、JSON Schema、Python Pydantic 模型、C++ JsonCpp 校验器和前端 TypeScript 类型/运行时校验。
- 当前分支为 `feature/04-word-importer-prototype`。
- 模块 4 `04-word-importer-prototype` 已进入 Python Word 导入原型实现与真实样例适配阶段：第一版支持 `.docx`、`rule_profile="辽宁国省干线"`、第二章三张病害检查表、病害照片抽取匹配、第四章评分表和模块 3 契约输出。
- 绕阳河二号桥真实软件报告本地验收已覆盖：病害候选 25 条、病害照片候选 31 条、临时图片 36 个、第四章总分 85.61/2类、尺寸低置信误报清零。

## 已确认方向

- 新建独立项目，不放进 `auto_cad`。
- 第一版是本地网页系统，先单机使用，数据和服务边界按以后多人协作预留。
- 最终目标是生成完整正式 Word 报告，不只是生成片段。
- 第一阶段主流程：
  1. 创建或选择桥梁。
  2. 创建年度检测任务。
  3. 导入第 N 年 Word 数据源。
  4. 抽取第 N 年病害表、照片和第四章综合评定。
  5. 人工校对第 N 年病害事实。
  6. C++ 主服务写入第 N 年正式事实。
  7. 从 PostgreSQL 读取第 N-1 年事实，生成历史病害对比候选。
  8. 人工确认历史病害对比。
  9. 生成章节草稿。
  10. 生成完整正式 Word。
  11. 归档第 N 年资料，作为下一年历史数据。

## 关键架构原则

- 输入形式可以换，年度结构化数据模型要稳定。
- 今年数据导入必须通过 `Importers` 适配器抽象。
- 第一版实现 `SoftwareWordReportImporter` 和 `FormalWordReportImporter`。
- 模块 4 第一版重点实现 Word 导入原型，不直接写 PostgreSQL，只输出模块 3 的 `BridgeAnnualInspectionData` 候选 JSON。
- 以后可扩展 `ExcelInspectionImporter`、`ApiInspectionImporter`、`StructuredJsonImporter`、`DatabaseSyncImporter`、`ManualEntryImporter`。
- PostgreSQL 是事实主库。
- Word、图片、模板、附件和生成报告放在文件归档目录。
- C++ 主服务是唯一事实写入入口，Python 工具服务不直接写 PostgreSQL。
- Python 工具服务通过本地 HTTP JSON API 被 C++ 调用。
- 前端只直接调用 C++ 主服务，不直接调用 Python 工具服务。
- Milvus 只做相似报告段落、相似病害和历史写法检索，不存事实。
- AI 只能润色文字或给出参考写法，不能创造、修改或判断病害事实。
- 所有自动抽取和生成内容都要保留来源、置信度和人工确认状态。

## 模块 1 已确认工程底座

- C++ 主后端：Drogon。
- C++ 构建：CMake + Visual Studio 2022 生成器。
- C++ 依赖管理：vcpkg，当前使用 `D:\vcpkg`。
- Python 工具服务：FastAPI。
- Python 依赖管理：uv + `pyproject.toml`。
- 前端：React + TypeScript + Vite。
- 数据库：PostgreSQL。
- 数据库迁移：第一版使用明确 SQL 文件，不引入 SQLAlchemy/Alembic。
- 本地端口默认：
  - C++ 主服务：`127.0.0.1:18080`
  - Python 工具服务：`127.0.0.1:18081`
  - Vite 前端：`127.0.0.1:5173`
  - PostgreSQL：`127.0.0.1:5432`
- 前端开发服务跨端口调用 C++ 主服务时，需要 C++ 返回本地开发 CORS 头。

## 模块 2 已确认数据原则

- PostgreSQL 核心表第一期共 14 张：
  - 桥梁表
  - 桥梁别名表
  - 年度检测表
  - 归档文件表
  - 导入记录表
  - 导入文件关联表
  - 桥梁构件表
  - 构件别名表
  - 病害观测表
  - 病害尺寸表
  - 病害照片表
  - 技术状况评定表
  - 病害线索表
  - 病害对比表
- 桥梁基本信息以系统数据库为准，Word 不作为桥梁基础档案来源。
- 第一期导入流程必须先选择已有桥梁，再上传 Word。
- 桥梁构件表是正式表，但第一期不要求导入前录完整全桥构件清单；Word 中出现病害的构件，经人工确认后逐步沉淀。
- 第一阶段不单独建候选表，Word 解析结果先存 `导入记录表.解析结果JSON`。
- 人工校对确认后，C++ 主服务再把候选 JSON 写入正式业务表。
- 用户导入 Word 前手动全选并按 F9 刷新照片编号域。
- 病害检查表中的照片编号列作为照片关联主依据，后面照片区编号作为校验依据。
- 同一座桥同一个检测年度可以有多条导入记录，但只能有一份当前有效的年度检测数据。
- 同桥同年已确认后再次导入，不自动覆盖；确认修订版后新版本号递增，旧版标为 `已被修订`。
- 文件归档默认根目录为 `archive/`，后续可配置到项目外路径。
- 数据库存归档相对路径，不存写死绝对路径。
- 正式报告文件名可保留科室要求格式，例如 `Q202604001-JZ-019黑山县S213库盘线袁海亮桥定期检测报告-2类.docx`。
- 第一阶段暂不单独设计维护记录表、章节草稿表、报告模板表、生成报告表。

## 模块 3 已确认数据契约原则

- `BridgeAnnualInspectionData` 是候选数据，不是事实数据。
- Python 工具服务负责从 Word 可信区域抽取候选 JSON。
- C++ 主服务保存候选 JSON，前端校对候选 JSON，用户确认后 C++ 再写入正式业务表。
- 顶层结构包含 `contract`、`import_context`、`bridge_check`、`inspection`、`defects`、`photos`、`ratings`、`comparison_candidates`、`report_text_candidates`、`warnings`、`errors`。
- JSON key 使用英文 `snake_case`，业务值和报告原文保留中文。
- 第一版只读取 Word 中可信结构化区域：第二章结构病害检查表、第四章总体技术状况评定表。
- 非正式软件报告中的其他正文多为模板文字，第一版不抽取、不作为事实来源，也不作为报告生成参考。
- 正式报告第一版也先聚焦第二章病害检查表和第四章评定表。
- 后续可能从正式报告抽取特定章节文本，统一预留在 `report_text_candidates`，但文本候选不能直接创建病害事实、不能覆盖数据库事实。
- 病害尺寸必须保留原文 `measurement_text`，结构化尺寸 `measurements[]` 尽量解析，解析不稳时写 warning。
- 照片编号以病害检查表中的照片编号列为主依据，图片区标题或说明作为校验依据。
- 技术状况评定按表 4.1-2 建模为 `overall`、`structure_parts`、`evaluation_parts`。
- 评分最小单元是评价部件，例如上部承重构件、上部一般构件、支座、翼墙、耳墙等。
- 等级最小单元是结构分部，即上部结构、下部结构、桥面系；`evaluation_parts[]` 不设置等级。
- 对比候选不是 Python 从 Word 抽取的结果，而是在第 N 年事实确认入库后，由 C++ 读取数据库第 N-1 年事实生成。

## 模块 4 当前实现状态

- 模块 4 名称：`04-word-importer-prototype`。
- 当前分支：`feature/04-word-importer-prototype`。
- 第一版放在 Python 工具层，实现 Word 表格、图片和评分解析原型。
- 第一版只支持 `.docx`。
- Python 接收 C++ 传入的 `rule_profile`，不自动识别模板；当前已实现 `辽宁国省干线`。
- 软件生成 Word 第一版只抽取第二章结构病害检查表、病害照片和第四章全桥技术状况综合评定。
- 正式 Word 第一版也先聚焦第二章结构病害检查表、病害照片和第四章评定表，后续再扩展正式报告特定章节文本抽取。
- 辽宁国省干线当前只从 `表2.1-1`、`表2.2-1`、`表2.3-1` 抽取病害，从 `表4.1-1`、`表4.1-2` 识别评分上下文，其中评分主数据来自 `表4.1-2`。
- 真实样例中的 `表4.1-2` 矩阵布局、图片下方表格题注、`照片2.11` 紧凑编号、`S=0.6×0.1m²` 与 `长度：5m` 等尺寸表达已纳入规则。
- 输出必须是模块 3 的 `BridgeAnnualInspectionData` JSON 契约。
- 模块 4 不直接写数据库，不负责人工校对页面，不负责历史病害对比算法。
- 年度常规流程不要求上传上一年正式 Word；上一年事实优先来自 PostgreSQL。
- 正式 Word 在第一版中的主要用途是首次建档或历史补录：当数据库没有上一年度事实时，从正式报告第二章抽取历史基线病害候选，人工确认后入库。

## 核心模块

- `Importers`：数据源适配器，把 Word、Excel、API、JSON 等输入转换为统一年度检测数据。
- `BridgeAnnualInspectionData`：稳定中间模型，后续校对、对比和报告生成只依赖它。
- `Review Workspace`：人工校对工作台。
- `PostgreSQL`：结构化事实主库。
- `File Archive`：文件归档。
- `Comparison Engine`：历史病害对比引擎。
- `Text Rule Engine`：规则文本生成引擎。
- `Section Drafts`：章节草稿层。
- `Milvus + AI Assistant`：相似写法检索和可控润色。
- `Template Manager`：统一模板和桥级模板管理。
- `Docx Builder`：正式 Word 装配引擎。

## 核心页面

- 桥梁档案
- 构件病害档案
- 年度检测任务
- 导入任务
- 病害校对
- 历史对比确认
- 章节草稿
- 报告生成
- 模板管理

## 构件病害档案页

这是核心页面，不是附加功能。

选中一座桥后，系统应展示所有曾经出现过病害或维护记录的构件，包括已经修复的构件。点选构件后展示：

- 病害时间轴
- 维护时间轴
- 历年观测记录
- 照片
- 尺寸变化
- 对比状态
- 报告引用
- 原始来源

第一阶段只维护“出现过病害或维护记录的构件”，不强行建立完整构件树。

## 重要数据模型

- `Bridge`
- `BridgeAlias`
- `InspectionYear`
- `ReportSource`
- `BridgeComponent`
- `ComponentAlias`
- `DefectObservation`
- `DefectMeasurement`
- `DefectPhoto`
- `DefectThread`
- `DefectComparison`
- `MaintenanceRecord`
- `ConditionRating`
- `SectionDraft`
- `Template`
- `GeneratedReport`

特别注意：

- `DefectObservation` 是某一年报告里的一条病害观测。
- `DefectThread` 是跨年份追踪的同一处或同一类持续病害。
- `DefectComparison` 是上一年和今年病害之间的对比关系。
- `MaintenanceRecord` 是后续扩展模型，模块 2 第一阶段不单独建维护记录表。

## 第一阶段必须生成的章节

- `1.4.1 历年检测情况`
- `本桥上次检测时主要存在以下病害`
- `2.1.2 上部结构与最近一次检查结果对比`
- `2.2.2 下部结构与最近一次检查结果对比`
- `2.3.2 桥面系与最近一次检查结果对比`
- `5.1.1 桥梁外观检查结论`

## 第一阶段不做

- 完整多人账号权限和审签流。
- 手机端外业采集。
- 外部桥检系统 API 正式对接。
- 自动签章 PDF。
- 不经人工确认的一键终稿。
- AI 自动决定病害对比关系。
- 完整桥梁全构件台账自动生成。

## 样例资料

设计讨论中使用的样例桥梁为 `绕阳河二号桥`。

已分析过两份 Word：

- 软件自动生成报告：`绕阳河二号桥报告b8e246e8-cd4d-4202-8d4d-49c56dd34389.docx`
- 正式报告：`Q202605001-JZ-024-S319辽小线绕阳河二号桥定期检测报告（2类）.docx`

关键发现：

- 软件报告可用内容主要是第二章病害检查表/图片、第四章全桥技术状况综合评定。
- 正式报告包含完整报告结构、历史检测情况、与最近一次检查结果对比、结论和正式措辞。
- 目前最耗时的是人工统计今年与去年的病害变化，并更新历史、对比和外观检查结论。

## 下一步建议

推荐下一步：

1. 继续用更多真实 `.docx` 验证 `辽宁国省干线` 规则。
2. 对模块 4 当前改动做代码审查、整理提交并推送当前分支。
3. 后续进入模块 05 前，确认前端校对页如何展示普通候选、带 warning 候选、导入级 warning/error。
4. 若需要支持吉林国省干线、辽宁鹤大高速等模板，按 `word_rules` 规则集接口新增独立规则模块，不改主流程。

## 设计文档

完整规格见：

`docs/superpowers/specs/2026-07-01-bridge-report-system-design.md`

分块开发与技术文档评审机制见：

`docs/superpowers/specs/2026-07-01-modular-technical-doc-review-design.md`

模块 1 技术栈与项目骨架见：

`docs/superpowers/specs/modules/01-tech-stack-and-project-skeleton.md`

模块 2 PostgreSQL 核心表与文件归档见：

`docs/superpowers/specs/modules/02-postgresql-schema-and-file-archive.md`

模块 3 桥梁年度检测数据 JSON 契约见：

`docs/superpowers/specs/modules/03-bridge-annual-inspection-data-contract.md`

模块 4 Word 导入原型见：

`docs/superpowers/specs/modules/04-word-importer-prototype.md`

辽宁国省干线规则与真实样例适配见：

`docs/superpowers/specs/2026-07-07-liaoning-trunk-word-rules-design.md`

`docs/superpowers/specs/2026-07-07-liaoning-trunk-real-sample-adaptation-design.md`
