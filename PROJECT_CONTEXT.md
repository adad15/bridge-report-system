# PROJECT_CONTEXT

更新时间：2026-07-16

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
- 模块 3 已定义并实现 `BridgeAnnualInspectionData` 1.2 JSON 契约、JSON Schema、Python Pydantic 模型、C++ JsonCpp 校验器和前端 TypeScript 类型/运行时校验；1.2 增加病害详细位置、标度、扣分和构件评分双值校验，四端已同步实施。
- 当前分支为 `codex/06-5-interaction-redesign`。
- 模块 4 `04-word-importer-prototype` 已完成并推送到 GitHub：第一版支持 `.docx`、`rule_profile="辽宁国省干线"`、第二章三张病害检查表、病害照片抽取匹配、第四章评分表和模块 3 契约输出。
- 绕阳河二号桥真实软件报告本地验收已覆盖：病害候选 25 条、病害照片候选 31 条、临时图片 36 个、第四章总分 85.61/2类、尺寸低置信误报清零。
- 模块 5 `05-review-workspace` 已完成实施：后端确认入库事务（C++）与前端校对工作台（React）已落地，读取 `import_records.parsed_result_json`，按 warning/error 分组人工校对，保存草稿，五个操作按钮（保存草稿/批量确认普通候选/入库前检查/确认年度事实入库/取消导入）全部接后端，修订版确认弹窗和确认后只读态已实现。已完成端到端手工验收：编辑保存、批量确认、入库前检查解锁确认、首次确认入库写入四张事实表、同桥同年二次导入的修订版确认路径（含 409 拒绝校验）、取消导入均通过。
- 模块 5 已推送到 GitHub；分支 `feature/05-review-workspace` 与远端同步，提交 `52b9772` 为模块 05 当前末端。
- 模块 6 `06-component-defect-archive` 已完成实施：合同 1.2 四端升级（Python/JSON Schema/C++/TypeScript）、表 2.x-1 标度/扣分/构件评分抽取与构件组传播、JTG/T H21-2011 4.1.1 三语言评分纯函数（共享夹具 `samples/scoring/component_score_cases.json`）、模块 05 评分差异校对与 C++ 入库前独立复算、迁移 003（`condition_ratings` 三值校验列 + 构件级唯一约束 + severity 误写 scale 纠错）、构件级 `condition_ratings` 事务写入、模块 06 只读档案查询 API、线索建议纯函数、线索创建/绑定/重绑事务（`updated_at` 乐观令牌 + 已确认对比引用拦截）与前端 A1 档案页/线索整理页。
- 真实 Word 端到端已验证：重解析 1.1 旧草稿为 1.2、25/31/36/31 基线保持、23 条构件评分候选（17 条自动一致）、确认入库后 `1-1#板/1-2#板=65`、`2-1#板=55.81` 全部 `一致`，上部承重构件 86.62、全桥 85.61，档案页与线索创建/绑定在浏览器实操通过。
- 待校对的 1.0/1.1 旧草稿标记 `legacy_pending_reparse` 只读，必须经 `POST /api/import-records/{id}/parse-word` 重新解析为 1.2；已确认 1.1 事实保持可读，档案页显示"历史数据缺少评分校验明细"，不做猜测性回填。
- 合同 1.2 变更提案见 `docs/superpowers/specs/changes/2026-07-13-change-001-component-rating-and-defect-location.md`（已实施）；实施计划见 `docs/superpowers/plans/2026-07-13-component-defect-archive-implementation-plan.md`。
- 变更 002 已实施（2026-07-15，见 `docs/superpowers/specs/changes/2026-07-15-change-002-accounts-and-post-confirm-reopen.md`）：轻量账号体系（users/user_sessions、登录页、会话 token、写端点鉴权，默认账号 admin/admin123 与 user/user123 由后端启动播种）；已确认导入记录支持"重开校对 + 修订版入库"（warnings_only=任何登录用户仅改带警告病害，full=仅管理员全改；放弃修改可还原重开快照；重开态禁止取消导入）；校对页只读态照片查看不再被禁用（逐控件禁用取代 fieldset 一揽子禁用）；迁移 004。
- 模块 06 验收修复已实施（2026-07-15）：待校对导入记录采用独占租约编辑锁（30 秒心跳、2 分钟过期、同账号其他会话只读、管理员带原因强制解锁并留审计，迁移 005）；扣分 `0` 按合法值参与 JTG/T H21-2011 构件评分复算；入库前检查将最终分严格绑定到“一致/采用复算值/接受 Word 值”的相应来源；`warnings_only` 由后端字段白名单冻结非警告候选、照片、证据和其他评分，仅接受病害改动及其确定性评分复算结果。异常关闭不保存本地草稿，未保存修改按已确认方案直接丢失。
- 模块 06.5 已完成实施（2026-07-15）：系统入口改为桥梁档案列表；进入桥梁默认显示“最新正式结论 → 待办 → 历年技术状况 → 病害概况”；年度检测采用左侧年份栏和右侧年度工作台；支持桥梁内并发安全创建年度、受控上传/归档 Word、调用现有解析并进入模块 05 全屏校对；解析失败保留原 Word 并可重试；校对退出返回原年度；构件档案保留线索整理但不作为一级导航。当前仍只有 Word 格式，不实现多来源合并。
- 模块 06.5 管理员年度删除已实施（2026-07-15，见 `docs/superpowers/specs/modules/06-5-admin-delete-inspection-year.md`）：采用 C1 语义永久删除同桥同年的 V1/V2 等全部版本；前端实时影响预览、原因和精确确认文字三重确认；普通用户无入口且后端强制管理员鉴权；活动编辑锁阻断；影响令牌防止预览后数据变化；事务内删除年度事实并重算跨年病害线索；共享归档文件保留，独占文件经可重试队列物理清理；永久保存删除审计。
- 模块 06.5 管理员桥梁维护已实施（2026-07-16，见 `docs/superpowers/specs/2026-07-16-bridge-administration-design.md`）：管理员可在桥梁档案页精简新增桥梁，也可用复选框批量预览并逐座永久删除整桥档案；普通用户无入口且后端 403；活动编辑锁、逐桥影响令牌和独立事务保证批量部分成功；永久审计保留桥梁/操作者/原因/数量快照；独占文件进入持久清理队列，共享文件保留；清理器支持立即、启动、每 5 分钟重试、`SKIP LOCKED` 领取、退避和陈旧领取恢复。
- 模块 06.5 导入删除、无照片语义与校对定位补充已实施（2026-07-17，见 `docs/superpowers/specs/2026-07-16-import-record-deletion-and-review-navigation-design.md`）：管理员可在年度资料卡片永久删除尚未形成正式事实的单条导入记录，删除前预览影响并填写原因和精确确认文字；已确认/正式事实引用/活动编辑锁/陈旧影响令牌阻断；删除审计永久保存，独占归档照片、临时 Word 和解析工作目录进入可重试清理队列，共享文件保留；解析中删除后迟到结果不会复活记录。无照片编号病害不再告警，实际引用缺图仍告警；病害显示导入内序号，“需要处理”可按业务标签跳转并高亮病害、字段、照片和评分。
- 模块 06.5 最新验收：Python 118 项通过、1 项环境门控跳过，真实 Word 回归 1 项通过（25/31/36/31/15 基线保持）；前端 260 项通过；C++ + PostgreSQL 338 项通过；数据库 002—009 迁移/烟雾测试、C++ Debug 全量链接和前端生产构建通过。

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
- 技术状况评定原 1.1 按表 4.1-2 建模为 `overall`、`structure_parts`、`evaluation_parts`；1.2 增加第二章具体构件的 `component_ratings[]`。
- 具体构件（如 `2-1#板`）有自己的年度构件评分；评价部件（如上部承重构件）仍保留部件评分，二者不是同一层级。
- 构件评分依据 JTG/T H21-2011 第 4.1.1 条，按多个病害扣分累计计算，不是简单取单个病害的最低分。
- 1.2 保存 Word 来源分、规范复算分、最终确认分和校验状态；第一版只使用 Word 已给出的 DP，不从病害标度反推 DP。
- 等级最小单元是结构分部，即上部结构、下部结构、桥面系；`evaluation_parts[]` 不设置等级。
- 对比候选不是 Python 从 Word 抽取的结果，而是在第 N 年事实确认入库后，由 C++ 读取数据库第 N-1 年事实生成。

## 模块 4 已完成状态

- 模块 4 名称：`04-word-importer-prototype`。
- 完成分支：`feature/04-word-importer-prototype`。
- 第一版放在 Python 工具层，实现 Word 表格、图片和评分解析原型。
- 第一版只支持 `.docx`。
- Python 接收 C++ 传入的 `rule_profile`，不自动识别模板；当前已实现 `辽宁国省干线`。
- 软件生成 Word 第一版只抽取第二章结构病害检查表、病害照片和第四章全桥技术状况综合评定。
- 正式 Word 第一版也先聚焦第二章结构病害检查表、病害照片和第四章评定表，后续再扩展正式报告特定章节文本抽取。
- 辽宁国省干线当前实现从 `表2.1-1`、`表2.2-1`、`表2.3-1` 抽取病害，从 `表4.1-1`、`表4.1-2` 识别评分上下文；合同 1.2 实施时还要从表 2.x-1 抽取详细位置、标度、病害扣分和具体构件评分。
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
- 病害与照片按组校对，支持逐张确认、重新关联、确认无关、忽略、人工确认缺图和整组确认。
- 导入记录确认时在同一数据库事务内锁定并读取最新草稿，执行校验、预检、归档文件解析和正式事实写入。
- 已确认、已取消和旧版终态记录为只读；保存草稿使用 revision 防止旧请求覆盖新编辑状态。
- 辽宁国省干线真实 Word 已验证：25 条病害、31 个照片候选、36 个临时图片、31 个归档照片、15 个评分项。
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
- 构件年度评分显示来源值、规范复算值、最终确认值、校验状态和计算证据。
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
- 构件年度评分与校验证据
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

1. 先由用户体验并确认模块 06.5 的桥梁概览、年度工作台和 Word 导入流程；必要时继续做 6.5 界面优化。
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
