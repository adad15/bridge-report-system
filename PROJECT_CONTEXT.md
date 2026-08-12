# PROJECT_CONTEXT

更新时间：2026-08-10

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
当前应该在分支 codex/06-5-interaction-redesign。
模块 01～06.5 已完成；下一步先由用户决定是否开始模块 07，不要自动进入模块 07 编码。
请先读取 PROJECT_CONTEXT.md、docs/superpowers/specs/modules/06-5-bridge-centric-interaction-redesign.md、docs/superpowers/specs/2026-07-16-import-record-deletion-and-review-navigation-design.md、docs/superpowers/plans/2026-07-17-import-record-deletion-and-review-navigation-implementation-plan.md，以及模块 05、06 规格。模块 07 未完成需求确认前不要大规模编码。
```

## 当前进度

- 模块 1 `01-tech-stack-and-project-skeleton` 已完成实施并提交到 GitHub。
- 模块 2 `02-postgresql-schema-and-file-archive` 已完成实施：数据库迁移、系统编号工具、归档路径工具和数据库 smoke test 已通过。
- 模块 2 设计文档提交号：`52ee0ab docs: add module 02 schema and archive design`。
- 模块 3 `03-bridge-annual-inspection-data-contract` 已完成实施并推送到 GitHub。
- 模块 3 最初建立了 `BridgeAnnualInspectionData` 跨端契约；当前运行时已统一升级为 4.0，仅保留导入和校对候选，删除 Word 扣分、导入评分、人工二选一状态以及照片级 `match_status` / `review_status`，Python/C++/TypeScript 均严格拒绝旧版本。
- 当前分支为 `codex/06-5-interaction-redesign`。
- 模块 4 `04-word-importer-prototype` 已完成并推送到 GitHub：支持 `.docx`、`rule_profile="辽宁国省干线"`、第二章三张病害检查表、病害照片抽取匹配和模块 3 契约输出；评分表解析现已删除。
- 绕阳河二号桥真实软件报告基线为：病害候选 25 条、病害照片候选 31 条、临时图片 36 个、可归档照片 31 张；Word 中原有评分不再进入导入 JSON。
- 模块 5 `05-review-workspace` 已完成实施：后端确认入库事务（C++）与前端校对工作台（React）已落地，读取 `import_records.parsed_result_json`，按 warning/error 分组人工校对，保存草稿，五个操作按钮（保存草稿/批量确认普通候选/入库前检查/确认年度事实入库/取消导入）全部接后端，修订版确认弹窗和确认后只读态已实现。已完成端到端手工验收：编辑保存、批量确认、入库前检查解锁确认、首次确认入库写入四张事实表、同桥同年二次导入的修订版确认路径（含 409 拒绝校验）、取消导入均通过。
- 模块 5 已推送到 GitHub；分支 `feature/05-review-workspace` 与远端同步，提交 `52b9772` 为模块 05 当前末端。
- 模块 6 `06-component-defect-archive` 的病害档案、线索建议和绑定/重绑事务继续保留；其中早期 Word 评分解析、三端重复评分公式和评分差异校对已经由系统自主评定架构取代。
- 现行评定架构将 JTG/T H21—2011 与 JTG 5120—2021 分为独立、版本化规范包；桥梁锁定项目规范组合和已确认构件台账，H21 evaluator 负责试算与正式评定，正式运行保存输入摘要、包校验和、台账版本、各级结果和结构化轨迹。
- 用户创建桥梁时录入构件数量，系统生成实际构件编号并允许修改；病害必须填写构件类别、构件编号、病害位置、病害类型和病害描述，正式确认前必须关联最新已确认台账中的实际构件。结构部位不在校对页面显示。
- 合同 1.2 和模块 6 早期评分方案仅保留在历史规格/迁移记录中；运行时代码、当前样例和数据库最终态均以 4.0 与系统评分为准。
- 2026-08-10 已取消照片级确认状态：照片候选只保存编号、关联病害、归档文件、来源、置信度和警告；病害组确认是照片关系的唯一确认动作。正式入库时，与确认病害关联且归档完整的照片写入 `defect_photos`，未关联照片跳过并保留提示；数据库 `defect_photos.match_status` 已由迁移 025 删除。
- 变更 002 已实施（2026-07-15，见 `docs/superpowers/specs/changes/2026-07-15-change-002-accounts-and-post-confirm-reopen.md`）：轻量账号体系（users/user_sessions、登录页、会话 token、写端点鉴权，默认账号 admin/admin123 与 user/user123 由后端启动播种）；已确认导入记录支持"重开校对 + 修订版入库"（warnings_only=任何登录用户仅改带警告病害，full=仅管理员全改；放弃修改可还原重开快照；重开态禁止取消导入）；校对页只读态照片查看不再被禁用（逐控件禁用取代 fieldset 一揽子禁用）；迁移 004。
- 模块 06 验收修复中的独占租约编辑锁、重开范围控制和异常关闭策略继续有效；早期 Word 扣分复算及“接受 Word 值/采用复算值”路径已删除，评分完全由系统 evaluator 生成。
- 模块 06.5 已完成实施（2026-07-15）：系统入口改为桥梁档案列表；进入桥梁默认显示“最新正式结论 → 待办 → 历年技术状况 → 病害概况”；年度检测采用左侧年份栏和右侧年度工作台；支持桥梁内并发安全创建年度、受控上传/归档 Word、调用现有解析并进入模块 05 全屏校对；校对退出返回原年度；构件档案保留线索整理但不作为一级导航。当前仍只有 Word 格式，不实现多来源合并。2026-08-10 起，解析或契约校验失败会自动删除本次导入记录、来源引用和独占归档，不再留下“解析失败”卡片；错误仍在导入弹窗中显示，用户修正后重新发起导入。
- 模块 06.5 管理员年度删除已实施（2026-07-15，见 `docs/superpowers/specs/modules/06-5-admin-delete-inspection-year.md`）：采用 C1 语义永久删除同桥同年的 V1/V2 等全部版本；前端实时影响预览、原因和精确确认文字三重确认；普通用户无入口且后端强制管理员鉴权；活动编辑锁阻断；影响令牌防止预览后数据变化；事务内删除年度事实并重算跨年病害线索；共享归档文件保留，独占文件经可重试队列物理清理；永久保存删除审计。
- 模块 06.5 管理员桥梁维护已实施（2026-07-16，见 `docs/superpowers/specs/2026-07-16-bridge-administration-design.md`）：管理员可在桥梁档案页精简新增桥梁，也可用复选框批量预览并逐座永久删除整桥档案；普通用户无入口且后端 403；活动编辑锁、逐桥影响令牌和独立事务保证批量部分成功；永久审计保留桥梁/操作者/原因/数量快照；独占文件进入持久清理队列，共享文件保留；清理器支持立即、启动、每 5 分钟重试、`SKIP LOCKED` 领取、退避和陈旧领取恢复。
- 模块 06.5 导入删除、无照片语义与校对定位补充已实施（2026-07-17，见 `docs/superpowers/specs/2026-07-16-import-record-deletion-and-review-navigation-design.md`）：管理员可在年度资料卡片永久删除尚未形成正式事实的单条导入记录，删除前预览影响并填写原因和精确确认文字；已确认/正式事实引用/活动编辑锁/陈旧影响令牌阻断；删除审计永久保存，独占归档照片、临时 Word 和解析工作目录进入可重试清理队列，共享文件保留；解析中删除后迟到结果不会复活记录。无照片编号病害不再告警，实际引用缺图仍告警；病害显示导入内序号，“需要处理”可按业务标签跳转并高亮病害、字段、照片和评分。
- 2026-07-17 至 2026-07-20 已完成版本化规范、实际构件台账、合同 2.0、全账号新增/删除病害、范围尺寸、H21 试算与正式系统评定。Task 18 也已完成：旧评分解析、三端重复公式、评分校对 UI 和旧数据库字段已删除，迁移 014 要求评分投影必须关联正式 assessment run；仍未开始模块 07。
- Task 18 最终验证：Python 96 项通过、1 项环境门控跳过；真实 Word 回归 1 项通过（25 条病害、31 个照片候选、36 个 Word 图片、31 张可归档照片，导入评分为 0）；前端 218 项通过且生产构建通过；C++ + PostgreSQL 354 项通过且 Debug 全量链接通过；数据库 14 个迁移连续应用两遍、10 个 smoke 全部通过。管理员和普通账号界面验收均可新增/删除病害、不显示结构部位，完整心跳周期内聚焦字段保持稳定。

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
- 所有自动抽取和生成内容都要保留来源与置信度；需要业务决策的病害等对象保留人工确认状态。照片关系不再维护独立确认状态，由所属病害组统一确认。

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
- 顶层结构包含 `contract`、`import_context`、`bridge_check`、`inspection`、`defects`、`photos`、`comparison_candidates`、`report_text_candidates`、`warnings`、`errors`；不存在 `ratings`。
- JSON key 使用英文 `snake_case`，业务值和报告原文保留中文。
- 第一版只读取 Word 中可信结构化病害区域：第二章结构病害检查表及病害照片；不读取第四章评分。
- 非正式软件报告中的其他正文多为模板文字，第一版不抽取、不作为事实来源，也不作为报告生成参考。
- 正式报告第一版也先聚焦第二章病害检查表和第四章评定表。
- 后续可能从正式报告抽取特定章节文本，统一预留在 `report_text_candidates`，但文本候选不能直接创建病害事实、不能覆盖数据库事实。
- 病害尺寸必须保留原文 `measurement_text`，结构化尺寸 `measurements[]` 尽量解析，解析不稳时写 warning。
- 照片编号以病害检查表中的照片编号列为主依据，图片区标题或说明作为校验依据。
- 技术状况评定不属于导入契约。病害事实确认后，系统按桥梁锁定的技术状况评定规范包和构件台账自主计算构件、部件、结构分部及全桥结果。
- 病害标度可来自 Word 或用户手工录入；规范包把病害类型和标度映射为扣分值，H21 evaluator 再按规范规则计算并保存可追溯轨迹。
- 用户自行将系统评定结果与原报告对比；程序不读取、存储或确认 Word 评分。
- 等级最小单元是结构分部，即上部结构、下部结构、桥面系；`evaluation_parts[]` 不设置等级。
- 对比候选不是 Python 从 Word 抽取的结果，而是在第 N 年事实确认入库后，由 C++ 读取数据库第 N-1 年事实生成。

## 模块 4 已完成状态

- 模块 4 名称：`04-word-importer-prototype`。
- 完成分支：`feature/04-word-importer-prototype`。
- 第一版放在 Python 工具层，实现 Word 病害表格和图片解析原型；旧评分解析已删除。
- 第一版只支持 `.docx`。
- Python 接收 C++ 传入的 `rule_profile`，不自动识别模板；当前已实现 `辽宁国省干线`。
- 软件生成 Word 和正式 Word 当前只抽取第二章结构病害检查表及病害照片；第四章评分表不识别、不告警。
- 辽宁国省干线规则从 `表2.1-1`、`表2.2-1`、`表2.3-1` 抽取构件类别、构件编号、病害位置、病害类型、描述、标度、尺寸和照片编号。
- 真实样例中的 `表4.1-2` 矩阵布局、图片下方表格题注、`照片2.11` 紧凑编号、`S=0.6×0.1m²` 与 `长度：5m` 等尺寸表达已纳入规则。
- 输出必须是模块 3 的 `BridgeAnnualInspectionData` JSON 契约。
- 模块 4 不直接写数据库，不负责人工校对页面，不负责历史病害对比算法。
- 年度常规流程不要求上传上一年正式 Word；上一年事实优先来自 PostgreSQL。
- 正式 Word 在第一版中的主要用途是首次建档或历史补录：当数据库没有上一年度事实时，从正式报告第二章抽取历史基线病害候选，人工确认后入库。

## 模块 5 已实现状态

- 模块 5 名称：`05-review-workspace`。
- 模块 05 完成分支：`feature/05-review-workspace`，已推送并与远端同步。
- 第一版采用“完整校对入库闭环”：桥梁 -> 年度检测任务 -> 导入记录 -> 校对工作台 -> 保存草稿 -> 入库前检查 -> 确认年度事实入库。
- 页面入口按桥梁年度组织，不单独做全系统待校对任务中心。
- 页面按“需要处理 / 病害与照片 / 技术状况评定 / 原始 JSON”组织；来源证据通过按钮弹窗查看。
- 病害按构件类别分色，每条病害保留构件类别、构件编号、位置、类型、数量、尺寸原文、照片编号和校对状态。
- 照片由 C++ 归档到文件系统并写入 `archived_files/import_record_files`，前端只通过受控内容接口读取。
- 病害与照片按组校对，支持添加、删除、上传和重新关联照片；照片不再逐张确认，整组确认同时确认当前病害与照片关系。
- 导入记录确认时在同一数据库事务内锁定并读取最新草稿，执行校验、预检、归档文件解析和正式事实写入。
- 已确认、已取消和旧版终态记录为只读；保存草稿使用 revision 防止旧请求覆盖新编辑状态。
- 辽宁国省干线真实 Word 基线：25 条病害、31 个照片候选、36 个临时图片、31 个归档照片；原 15 个评分项不再输出。
- 用户校对核心业务字段，不直接编辑全量 JSON。
- 普通候选允许批量确认，但确认入库前必须由 C++ 后端重新校验。
- 同桥同年已有当前有效事实时，必须显式作为修订版确认，不允许静默覆盖。
- 模块 5 不生成历史病害对比候选；对比算法和对比确认页放到后续模块。

## 模块 6 已确认设计

- 模块名称：`06-component-defect-archive`。
- 当前分支：`feature/06-component-defect-archive`。
- 主页面只读优先：左侧构件列表，右侧构件档案详情。
- 构件只要在任一年度当前有效版本中存在正式病害，就进入默认列表；最新年度未出现也不能消失。
- 默认只读各年度当前有效版本，旧修订版从独立历史入口查看且不参与统计。
- 详情以病害为一级单位，以年度为二级单位；例如 `2-1#板 -> 蜂窝、麻面 -> 2025 / 2024`。
- 病害线索保存标准详细位置，年度观测保留当年实际位置原文；如 `0#台顶处`、`小桩号立面`、`左侧端部`。
- 系统可按同构件、病害类型、详细位置给出候选，但只能由用户绑定已有线索、创建新线索或保持不确定。
- 模块 06 只写病害线索和绑定关系，不修改年度病害事实，不生成对比结论。
- 构件年度评分只显示系统正式结果、对应 assessment run 和计算证据。
- 模块 07 `07-defect-comparison-engine` 再基于已整理线索判断发展、减轻、修复、新增等变化。

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

选中一座桥后，模块 06 第一版展示所有曾在当前有效年度版本中出现正式病害的构件。维护记录和“已经修复”的结论待后续模块接入。点选构件后展示：

- 按病害线索组织的历年观测记录
- 线索标准详细位置和年度实际位置
- 照片
- 尺寸变化
- 构件年度系统评分与计算证据
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

1. 完成 Task 18 旧评分链路清理的全量自动回归、真实 Word 回归和模块 06.5 界面验收。
2. 用户明确同意后再进入模块 07 `07-defect-comparison-engine`：基于已整理的病害线索与相邻年度观测生成对比候选，人工确认后写入 `defect_comparisons`。
3. 模块 07 设计时注意：模块 06 的绑定事务已在数据库层拦截"重新绑定被人工已确认对比引用的观测"，撤销对比结论的入口应由模块 07 提供。

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

模块 5 人工校对工作台见：

`docs/superpowers/specs/modules/05-review-workspace.md`

构件评分与病害详细位置跨模块变更提案见：

`docs/superpowers/specs/changes/2026-07-13-change-001-component-rating-and-defect-location.md`

模块 6 构件病害档案与病害线索整理见：

`docs/superpowers/specs/modules/06-component-defect-archive.md`

模块 5 本次讨论设计记录见：

`docs/superpowers/specs/2026-07-07-review-workspace-design.md`

辽宁国省干线规则与真实样例适配见：

`docs/superpowers/specs/2026-07-07-liaoning-trunk-word-rules-design.md`

`docs/superpowers/specs/2026-07-07-liaoning-trunk-real-sample-adaptation-design.md`
