# PROJECT_CONTEXT

更新时间：2026-07-03

## 项目一句话

`bridge-report-system` 是一个独立的本地网页系统，用于按桥梁、按年份沉淀定期检测报告、病害、构件和维修记录，并自动生成正式桥梁检测 Word 报告。

## 新窗口启动提示

在新的 Codex 窗口开始工作时，先读取本文件和设计文档：

- `PROJECT_CONTEXT.md`
- `docs/superpowers/specs/2026-07-01-bridge-report-system-design.md`
- `docs/superpowers/specs/2026-07-01-modular-technical-doc-review-design.md`
- `docs/superpowers/specs/modules/01-tech-stack-and-project-skeleton.md`
- `docs/superpowers/specs/modules/02-postgresql-schema-and-file-archive.md`

推荐首条提示：

```text
请先读取 PROJECT_CONTEXT.md、docs/superpowers/specs/2026-07-01-bridge-report-system-design.md、docs/superpowers/specs/modules/01-tech-stack-and-project-skeleton.md、docs/superpowers/specs/modules/02-postgresql-schema-and-file-archive.md，然后按照 Inline Execution，为第二模块 02-postgresql-schema-and-file-archive 编写实施计划。先不要写代码。
```

## 当前进度

- 模块 1 `01-tech-stack-and-project-skeleton` 已完成实施并提交到 GitHub。
- 模块 2 `02-postgresql-schema-and-file-archive` 已完成实施：数据库迁移、系统编号工具、归档路径工具和数据库 smoke test 已通过。
- 模块 2 设计文档提交号：`52ee0ab docs: add module 02 schema and archive design`。
- 下一步建议进入模块 3 `03-bridge-annual-inspection-data-contract`，先设计 C++ 与 Python 之间的年度检测数据 JSON 契约。
- 当前协作方式采用 Inline Execution，不使用 Subagent-Driven。

## 已确认方向

- 新建独立项目，不放进 `auto_cad`。
- 第一版是本地网页系统，先单机使用，数据和服务边界按以后多人协作预留。
- 最终目标是生成完整正式 Word 报告，不只是生成片段。
- 第一阶段主流程：
  1. 创建或选择桥梁。
  2. 创建年度检测任务。
  3. 导入今年数据源。
  4. 导入上一年正式报告，或选择系统已有上一年度数据。
  5. 抽取病害表、照片和第四章综合评定。
  6. 人工校对病害事实。
  7. 生成并确认历史病害对比。
  8. 生成章节草稿。
  9. 生成完整正式 Word。
  10. 归档今年资料，作为下一年历史数据。

## 关键架构原则

- 输入形式可以换，年度结构化数据模型要稳定。
- 今年数据导入必须通过 `Importers` 适配器抽象。
- 第一版实现 `SoftwareWordReportImporter` 和 `FormalWordReportImporter`。
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

进入新项目开发窗口后，先做模块 2 实施计划，不急着写代码。

推荐下一步：

1. 基于 `docs/superpowers/specs/modules/02-postgresql-schema-and-file-archive.md` 编写实施计划。
2. 实施计划采用 Inline Execution。
3. 第二模块实施优先做数据库迁移 SQL、系统编号生成、归档路径工具、基础 seed/test。
4. 计划写完并经用户确认后，再开始写代码。
5. 模块 2 完成后，再讨论 `03-bridge-annual-inspection-data-contract`。

## 设计文档

完整规格见：

`docs/superpowers/specs/2026-07-01-bridge-report-system-design.md`

分块开发与技术文档评审机制见：

`docs/superpowers/specs/2026-07-01-modular-technical-doc-review-design.md`

模块 1 技术栈与项目骨架见：

`docs/superpowers/specs/modules/01-tech-stack-and-project-skeleton.md`

模块 2 PostgreSQL 核心表与文件归档见：

`docs/superpowers/specs/modules/02-postgresql-schema-and-file-archive.md`
