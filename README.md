# Bridge Report System

本地部署的桥梁定期检测业务系统。当前已经支持完整构件档案、年度资料导入与校对、正式病害和照片入库、系统自主技术状况评定、构件病害档案及跨年病害线索整理。

报告模板管理与 Word 即时生成已经完成设计，尚未进入实施。

## 当前技术栈

- C++20 + Drogon：主服务，默认 `127.0.0.1:18080`
- Python 3.11+ + FastAPI：Word/来源数据库导入工具，默认 `127.0.0.1:18081`
- React 18 + TypeScript + Vite + Ant Design 6：前端，默认 `127.0.0.1:5173`
- PostgreSQL：结构化事实主库
- 本地文件归档：原始 Word、病害照片及其他受控输入文件

前端只调用 C++ 主服务。C++ 负责权限、业务编排和正式事实写入；Python 工具不直接连接 PostgreSQL。

## 当前业务能力

- 登录、普通用户与管理员权限
- 工作台和桥梁档案
- 版本化完整构件档案及构件编号生成
- 年度检查创建、Word 导入和来源数据库导入
- 合同 5.0 候选数据校对
- 关系化构件解析、区间展开、两侧构件绑定和评分树解析
- 正式病害、尺寸和照片确认入库
- 版本化 JTG/T H21—2011、JTG 5120—2021 规范和评定树
- 系统试算及正式技术状况评定
- 构件病害档案、跨年病害线索和批量整理工作台
- 最近两个正式年度的病害记录条数对比
- 管理员桥梁、年度和导入记录删除及可重试文件清理

尚未实现：病害语义对比确认、报告模板管理、人员设备管理、Word 生成、维修记录和 AI/Milvus 能力。

## 首次阅读

按顺序阅读：

1. `PROJECT_CONTEXT.md`
2. `docs/superpowers/specs/2026-09-04-report-template-word-generation-design.md`
3. 当前任务对应的 `docs/superpowers/specs/modules/` 规格
4. 当前任务对应的较新日期设计或实施计划

`docs/superpowers/specs/2026-07-01-bridge-report-system-design.md` 是早期总体设想。与较新的已确认设计冲突时，以 `PROJECT_CONTEXT.md` 和较新设计为准。

## 快速启动

先确保 PostgreSQL 已启动并配置数据库连接。默认开发连接由脚本和本地配置说明提供；自定义连接可设置：

```powershell
$env:BRIDGE_REPORT_DATABASE_URL = "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
```

从仓库根目录启动：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/start-all.ps1
powershell -ExecutionPolicy Bypass -File scripts/dev/check-health.ps1
```

也可以分别启动：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/start-python-tools.ps1
powershell -ExecutionPolicy Bypass -File scripts/dev/start-cpp-backend.ps1
powershell -ExecutionPolicy Bypass -File scripts/dev/start-frontend.ps1
```

## 数据库

当前迁移范围为 `database/migrations/001_*.sql` 至 `029_*.sql`。完整检查会按名称顺序应用全部迁移两遍，再运行数据库 smoke tests：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-database.ps1
```

`002_core_schema_and_archive.sql` 只是最初基础结构，不代表当前完整数据库。后续迁移已经增加账号、编辑锁、删除审计、临时 Word、版本化规范、构件档案修订、系统评定、评分树、导入解析关系表和线索身份等能力。

## BridgeAnnualInspectionData 5.0

跨 Python、C++ 和 TypeScript 的唯一运行时合同是 `5.0`：

- Schema：`contracts/bridge_annual_inspection_data.schema.json`
- Python：`tools-python/bridge_report_tools/contracts/annual_inspection.py`
- C++：`backend-cpp/src/contracts/AnnualInspectionContract.cpp`
- TypeScript：`frontend/src/contracts/annualInspection.ts`
- 样例：`samples/contracts/bridge_annual_inspection_data.v5.valid.json`

合同 5.0 只承载来源事实和一般校对事实。构件解析、展开实例及评分树解析保存在 PostgreSQL 关系表中；Word 评分和扣分不进入合同。正式评分由系统 evaluator 根据锁定规范、评定树和构件档案计算。

## Word 导入

Python 工具入口：

```text
POST http://127.0.0.1:18081/imports/word/parse
```

当前 Word 规则配置：

```json
{
  "rule_profile": "辽宁国省干线"
}
```

当前只抽取可信的第二章病害检查表及病害照片。第四章 Word 评分不读取、不保存，也不会因为缺少评分表导致解析失败。

来源数据库导入也输出同一份 5.0 候选合同。所有正式事实仍由 C++ 在校对和预检后写入。

## 主要前端入口

```text
/workbench
/bridges
/rating-trees
/bridges/:bridgeId
/bridges/:bridgeId/inventory
/bridges/:bridgeId/inspections
/bridges/:bridgeId/inspections/:inspectionYearId
/bridges/:bridgeId/components
/bridges/:bridgeId/components/:componentId
/bridges/:bridgeId/defect-threads/triage
/bridges/:bridgeId/inspections/:inspectionYearId/imports/:importRecordId/review
```

旧 `/defect-threads/review` 地址仅保留显式重定向，不再是实际工作台。

## 测试与构建

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

C++ 与 PostgreSQL：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-backend-tests.ps1
powershell -ExecutionPolicy Bypass -File scripts/dev/check-database.ps1
```

布局检查：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-layout.ps1
```

## 当前开发方向

下一阶段是报告模板管理与 Word 即时生成：

- 多模板管理和默认模板
- 模板语义锚点及模板自定义章节顺序
- 报告人员、检测设备和年度报告配置
- 只输出有病害构件的病害表
- 保留入库照片编号的两栏照片布局
- `source_defect_count_delta_v1` 来源病害去重后的条数对比
- 正式评定、附录一和确定性第六章
- Microsoft Word/WPS 字段更新
- 临时生成和下载，不保存报告版本或永久报告文件

设计依据：`docs/superpowers/specs/2026-09-04-report-template-word-generation-design.md`。
