# PROJECT_CONTEXT

更新时间：2026-09-04

## 项目一句话

`bridge-report-system` 是一个本地网页系统，用于维护桥梁及完整构件档案、导入并确认年度病害事实、执行可追溯的系统技术状况评定、整理跨年病害线索，并最终按可管理模板即时生成正式 Word 报告。

## 新窗口启动提示

开始工作时优先读取：

1. `PROJECT_CONTEXT.md`
2. `docs/superpowers/specs/2026-09-04-report-template-word-generation-design.md`
3. 与当前任务直接相关的 `docs/superpowers/specs/modules/` 模块规格
4. 当前任务对应的较新日期设计文档

推荐首条提示：

```text
继续 bridge-report-system 项目。仓库路径：D:\vs2022 code\bridge-report-system。
当前分支应为 07-begin-word。运行时 BridgeAnnualInspectionData 合同为 5.0。
模块 01～06 主体和后续构件台账、系统评定、导入解析关系化、批量病害线索整理、UI 统一已经实施。
当前已确认“报告模板管理与 Word 即时生成”设计，但尚未写实施计划、尚未编码。
先读取 PROJECT_CONTEXT.md 和 docs/superpowers/specs/2026-09-04-report-template-word-generation-design.md；
不要恢复旧的 Word 评分导入、报告版本管理或生成报告永久归档方案。
```

## 当前分支与进度

- 当前分支：`07-begin-word`
- 报告设计初稿提交：`a6dbbe6 docs: design report template word generation`
- 数据库迁移：`001`～`029`
- 运行时合同：`BridgeAnnualInspectionData 5.0`
- 已完成主体：模块 01～06，以及 06 之后的构件台账、版本化规范、系统自主评定、导入解析关系化、批量病害线索整理和 UI 统一工作
- 当前下一步：编写报告模块实施计划；先完成模板契约和 Word/WPS 字段更新原型阶段门，再进入数据表和业务编码

## 已完成能力

### 工程底座

- C++20 + Drogon 主后端，默认 `127.0.0.1:18080`
- Python 3.11+ + FastAPI 工具服务，默认 `127.0.0.1:18081`
- React 18 + TypeScript + Vite + Ant Design 6，默认 `127.0.0.1:5173`
- PostgreSQL 事实主库
- SQL 文件迁移和数据库 smoke test
- 本地归档文件系统，数据库保存受控相对路径

### 桥梁与年度工作流

- 登录、普通用户与管理员权限
- 工作台、桥梁档案、桥梁概览、构件台账、年度检测、构件病害档案和评定树页面
- 管理员新增和批量删除桥梁
- 创建年度检查、管理员永久删除年度及其所有修订版
- Word 或来源数据库导入、解析、草稿校对、入库前检查和正式确认
- 导入记录删除、临时 Word 生命周期和可重试文件清理

### 构件档案

- 每座桥维护版本化完整构件档案，不再只沉淀出现病害的构件
- 用户按桥型和数量生成实际构件编号，可修改、停用并确认修订版
- 正式病害必须绑定所选已确认构件档案中的实际构件
- 正式评定运行锁定构件档案修订版

### 导入和校对

- 当前合同 5.0 只承载来源事实和一般校对事实
- 构件解析、解析目标、展开实例和评分树解析保存在关系表中
- Python Word 导入只抽取可信的第二章病害表及病害照片，不读取或保存 Word 评分
- 导入支持手工新增/删除病害、照片关联、构件绑定、区间展开、两侧构件绑定和评分树选择
- C++ 是唯一正式事实写入入口；Python 不连接 PostgreSQL

### 系统自主评定

- JTG/T H21—2011 技术状况规范包和 JTG 5120—2021 养护规范包独立版本化
- 桥梁锁定规范组合、评定树版本和构件档案修订版
- H21 evaluator 提供试算和正式评定
- 正式运行保存输入摘要、规范校验值、各级结果和结构化计算轨迹
- `condition_ratings` 的正式结果必须关联成功的 `assessment_run`
- Word 中的评分、扣分和所谓“来源分/复算分二选一”均不再进入运行时合同

### 构件病害档案与跨年线索

- 构件档案默认只读，按线索纵向展示各年度正式观测
- 支持正式照片和来源证据查看
- 批量线索整理工作台处理大规模未绑定观测，并保留异常簇人工判断
- 线索身份以评分树节点等正式语义为基础，不只依赖原始病害文字
- 已确认对比引用保护仍保留，但模块 07 尚未提供对比确认和撤销入口

### 已有年度条数对比

桥梁概览已经能够读取最近两个已确认年度，按构件、构件类型和病害类型统计病害观测条数差，并明确说明该结果不代表逐条病害身份关系。

报告复用这项“只写增加、减少或持平”的展示能力，但不能直接复用其 `count(defect_observations)` 口径：范围拆分会把一条来源病害展开成多条观测。报告第一版使用 `source_defect_count_delta_v1`，按来源病害去重，并允许用户手动选择 `inspection_years.previous_inspection_id`；相应通用查询仍需新增。

## 尚未完成或明确延期

### 模块 07：病害语义对比

尚未实现：

- 跨年病害身份候选；
- 新增、持续、发展、减轻、消失、修复等语义；
- 人工确认、修改、驳回和撤销；
- `defect_comparisons` 写入 API 和确认页面。

`defect_comparisons` 当前只有基础表结构、引用保护和删除清理逻辑。

### 报告模板与 Word 即时生成

设计已确认，尚未实施：

- 报告模板管理；
- 报告人员库、检测设备库；
- 年度报告配置；
- 临时报告生成任务；
- `ReportContext`；
- Python Docx Builder；
- Word/WPS 字段更新器；
- 生成前检查、下载和过期清理页面。

当前设计文档：

`docs/superpowers/specs/2026-09-04-report-template-word-generation-design.md`

### 后续扩展

- 第一章需要的更多桥梁档案字段
- 第三章业务数据
- 第五章业务数据
- 附录二桥梁基本状况卡片
- 维修养护记录及维修效果确认
- 电子签名
- Milvus、AI 润色和事实校验

## 当前报告生成决策

1. 管理员维护多套当前模板，并设置一个默认模板。
2. 用户生成时可选择任意已启用模板。
3. 新建干净模板；正式旧报告只作为参考，不直接反复修改。
4. 模板用语义锚点控制内容位置和章节顺序，生成器不写死章节号。
5. 只保留 `TOC`、`PAGE`、`NUMPAGES` 等必要字段；不使用业务 `SEQ`、`REF`、`STYLEREF`。
6. 病害表只输出有病害的实际构件。
7. 病害照片沿用随病害组确认入库的编号和标题，固定两栏、等框、按比例完整显示、不裁剪。
8. 历史对比第一版按来源病害去重，只写记录条数增加、减少或持平；禁止解释为病害新增、消失或修复。
9. 第六章由正式评定和受控规则确定性生成，不使用 AI 自由写作。
10. 第一版标准模板的第三章、第五章和附录二保留标题及版式，正文为空。
11. 生成结果只是临时下载文件；系统不保存报告版本或永久归档 Word。
12. Microsoft Word 和 WPS Writer 均需通过字段更新和交叉打开验收。

## 当前主流程

```text
创建桥梁并确认完整构件档案
  -> 创建年度检查
  -> 导入 Word 或来源数据库
  -> 校对来源事实、构件解析和评分树解析
  -> 确认正式病害与照片
  -> 系统执行正式技术状况评定
  -> 整理跨年病害线索（按需）
  -> 配置模板、历史检查、人员和设备（待实施）
  -> 即时生成并下载 Word（待实施）
```

完整模块 07 的语义对比不是报告第一版的前置条件。报告先使用条数差，后续通过 `confirmed_defect_comparison_v2` 替换对比数据提供器。

## 关键架构原则

- PostgreSQL 是结构化事实主库。
- C++ 主服务是唯一事实写入和业务编排入口。
- Python 工具服务通过本地 HTTP 被 C++ 调用，不直接写数据库。
- 前端只调用 C++ 主服务。
- 输入格式通过 importer 适配；当前支持 Word 和来源数据库导入。
- 候选、解析状态、正式事实和正式评定分层存储。
- 构件存在性只来自已确认构件档案，报告生成不得猜测构件。
- AI 不能创建、修改或判定病害事实。
- 当前生成报告不进入永久文件归档。

## 本地运行与验证

从仓库根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/start-all.ps1
powershell -ExecutionPolicy Bypass -File scripts/dev/check-health.ps1
powershell -ExecutionPolicy Bypass -File scripts/dev/check-database.ps1
powershell -ExecutionPolicy Bypass -File scripts/dev/check-backend-tests.ps1
```

前端：

```powershell
Set-Location frontend
npm run test
npm run build
```

Python：

```powershell
Set-Location tools-python
uv run pytest
```

## 当前有效文档优先级

出现冲突时按以下顺序判断：

1. 当前代码、数据库迁移和自动化测试
2. `PROJECT_CONTEXT.md`
3. 已经用户确认的最新日期设计或变更文档
4. `docs/superpowers/specs/modules/` 中已同步到当前版本的模块规格
5. 早期总设计和历史实施计划

早期总设计与实施计划保留决策历史，不再自动覆盖后续已经确认并实施的变更。

## 文档维护提醒

- 运行时合同升级时，同时更新 Schema、Python、C++、TypeScript、样例、`contracts/README.md`、`README.md` 和模块 03。
- 跨模块设计实施后，把结论归并回对应模块规格，并更新状态栏。
- 报告生成实施后，补充本文件的迁移编号、API、页面入口、测试基线和 Word/WPS 验收结果。
