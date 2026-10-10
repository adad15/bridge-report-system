# Bridge Report System

本地部署的桥梁定期检测业务系统。已支持完整构件档案、年度资料导入与校对、正式病害和照片入库、系统技术状况评定、跨年病害线索整理，以及按模板即时生成 Word 报告。

文档更新：2026-09-23。当前能力和已知问题见 [项目上下文](PROJECT_CONTEXT.md)，全部设计、计划和审查记录见 [文档索引](docs/README.md)。


真实业务验收已完成小样主流程和两份真实报告解析，修复旧格式文档提示与 VML 图片漏读；报告版式仍有待处理项。结果及性能见 [集成验收记录](docs/2026-09-20-integrated-business-acceptance-validation.md)。

当前优先来源软件数据库路线：三年度974条病害、874张照片已完成导入对账；2026年度已通过现有页面完成构件绑定、校对和隔离正式入库，430条观测、607条尺寸记录、455张照片逐项核验通过。系统评定已自动写入，并按锁定 H21 1.0.4 规则独立复算：全桥 81.10732993815813 分（年度显示 81.11）、2 类，逐层及 5,602 条轨迹对账无差异；来源数据库路线的2026年度报告输出已在隔离环境验收：100页、430条病害表行、455张照片，独立对账零差异；封面字段改为报告配置页人工独立填写，横向附录页码连续。2024、2025尚未正式入库。见 [入库验收](docs/2026-09-21-source-confirmation-acceptance-validation.md)、[评分核验](docs/2026-09-23-source-assessment-audit.md)和[报告输出验收](docs/2026-09-23-source-report-acceptance-validation.md)。

## 技术栈与服务边界

| 层 | 当前实现 | 默认地址 |
| --- | --- | --- |
| 主后端 | C++20、Drogon | 127.0.0.1:18080 |
| 工具服务 | Python 3.11+、FastAPI、python-docx、pywin32 | 127.0.0.1:18081 |
| 前端 | React 18、TypeScript、Vite 5、Ant Design 6 | 127.0.0.1:5173 |
| 结构化存储 | PostgreSQL | 127.0.0.1:5432 |
| 文件存储 | 受控归档目录及独立临时目录 | 见本地配置 |

前端只调用 C++。C++ 负责权限、业务编排、数据库读写和受控文件路径；Python 负责导入解析、地图取图、DOCX 装配与字段更新，不连接 PostgreSQL。Office 字段更新使用单进程 Python 服务中的全局 FIFO 队列。

## 已实现能力

- 登录、普通用户与管理员权限，工作台和桥梁档案。
- 版本化完整构件档案、编号生成、年度检查和资料导入。
- Word 与来源数据库导入，合同 5.0 候选数据校对。
- 关系化构件解析、区间展开、两侧构件绑定及评分树解析。
- 正式病害、尺寸和照片确认入库，系统试算与正式技术状况评定。
- 构件病害档案、跨年病害线索和批量整理。
- 模板及默认模板管理、报告人员与设备库、年度报告配置。
- 生成前检查、临时报告任务、Word/WPS 字段更新和下载。
- 桥梁概况、坐标与六槽位图件，报告中的概况文字和配图。
- 管理员桥梁、年度和导入记录删除，以及归档文件清理。

报告历史对比采用 `source_defect_count_delta_v1`，按来源病害去重统计记录条数变化；桥梁概览的年度对比仍统计正式观测条数，两者口径不同。跨年新增、持续、消失、修复等病害语义对比、维修记录、电子签名及 AI/Milvus 尚未实现。

前端已实施页面、评分图表及重点面板按需加载；入口静态 JS 从约 3.26 MB 降至 0.83 MB，未使用的 ProComponents 依赖已移除。范围和实测见 [C 批次验收](docs/2026-09-20-framework-batch-c-validation.md)。

## 快速启动

先准备 PostgreSQL、本地配置及依赖，配置字段见 [local.example.json](config/local.example.json)。数据库连接写入本地配置的 postgres 节。首次按锁文件准备依赖，再启动：

```powershell
$env:VCPKG_ROOT = "D:/vcpkg" # 改成本机实际路径
powershell -ExecutionPolicy Bypass -File scripts/dev/prepare-dependencies.ps1
powershell -ExecutionPolicy Bypass -File scripts/dev/start-all.ps1 -BuildDirectory backend-cpp/build/batch-a -Configuration Release
powershell -ExecutionPolicy Bypass -File scripts/dev/check-health.ps1
```

统一入口核对本次服务身份、依赖就绪与前端端口；日志位于 logs/startup，停止命令随成功输出提供。旧构建缓存迁移、本机预设和工具版本见 [运行与验证](development.md)。A 批次结果及未测项见 [验证记录](docs/2026-09-18-framework-batch-a-validation.md)。

也可以分别启动：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/start-python-tools.ps1
powershell -ExecutionPolicy Bypass -File scripts/dev/start-cpp-backend.ps1
powershell -ExecutionPolicy Bypass -File scripts/dev/start-frontend.ps1
```

生成报告的主机须为已登录交互式桌面会话的 Windows，并安装 Microsoft Word 或 WPS Writer。当前本机两个 Office ProgID 均指向 WPS，真正 Microsoft Word 的交叉打开验收仍待补测。运行要求及故障恢复见 [运行与验证](development.md)。

## 数据库与文件

当前迁移为 [database/migrations](database/migrations) 中的 `001`～`037`。迁移 030～036 包含报告对比年度字段改名、照片编号规则、报告管理及任务、交付文件名、桥梁概况、坐标和图件表。迁移 037 增加任务模板快照、独立清理台账与位置图修订号。

归档存储模板、正式照片和桥梁图件等受控输入；生成结果存放在 `temporary_storage.report_root`，不建立永久报告版本。默认可下载保留时间为 24 小时、任务终态记录保留配置为 7 天。任务固定模板副本与配置；清理失败保留台账并重试，写入状态不明时阻断删除。B 批次结果及升级要求见 [验证记录](docs/2026-09-19-framework-batch-b-validation.md) 和 [运行指南](development.md)。

数据库验证优先使用隔离 schema 的 `scripts/dev/check-backend-tests.ps1`。另一个 `check-database.ps1` 会直接向配置数据库应用全部迁移两遍并运行 smoke tests，不是只读检查。命令和使用范围见 [运行与验证](development.md)。

## BridgeAnnualInspectionData 5.0

唯一接受的运行时导入合同为 `5.0`：

- [JSON Schema](contracts/bridge_annual_inspection_data.schema.json)
- [Python 模型](tools-python/bridge_report_tools/contracts/annual_inspection.py)
- [C++ 校验器](backend-cpp/src/contracts/AnnualInspectionContract.cpp)
- [TypeScript 类型与校验](frontend/src/contracts/annualInspection.ts)
- [有效样例](samples/contracts/bridge_annual_inspection_data.v5.valid.json)

合同只承载来源事实和一般校对事实。构件解析、展开实例及评分树解析保存在 PostgreSQL 关系表；Word 评分与扣分不进入合同。评分由系统 evaluator 根据锁定规范、评定树和构件档案计算。详见 [合同说明](contracts/README.md) 和 [规范包说明](standards/README.md)。

Word 解析入口为 Python 内部 `POST /imports/word/parse`，由 C++ 调用，规则配置为 `{"rule_profile": "辽宁国省干线"}`。只提取可信的第二章病害表和病害照片，不读取第四章评分。手工新增病害通过 C++ 专用命令创建候选及解析状态，前端不再生成候选 ID。

## 主要前端入口

```text
/workbench
/bridges
/rating-trees
/settings/report-templates
/settings/report-directory
/bridges/:bridgeId
/bridges/:bridgeId/inventory
/bridges/:bridgeId/inspections
/bridges/:bridgeId/inspections/:inspectionYearId
/bridges/:bridgeId/inspections/:inspectionYearId/report
/bridges/:bridgeId/components
/bridges/:bridgeId/components/:componentId
/bridges/:bridgeId/defect-threads/triage
/bridges/:bridgeId/inspections/:inspectionYearId/imports/:importRecordId/review
```

旧 `/defect-threads/review` 只保留重定向。报告管理入口由管理员使用，年度生成入口位于年度工作台。

## 开发与验证

前端在 `frontend` 目录运行 `npm run test -- --run` 和 `npm run build`；Python 在 `tools-python` 目录运行 `uv run --locked pytest`。TypeScript 已启用 `noUnusedLocals` 与 `noUnusedParameters`。完整命令、Office 测试开关与数据库隔离要求见 [运行与验证](development.md)。

2026-09-17 已修复 4 条 P1，清理 14 处未使用声明及旧前端候选 ID 工厂。本轮结果是分项验证记录，不等于三端全量回归全部重跑；详见 [修复状态与验证范围](docs/2026-09-17-review-remediation-status.md)。后续重点为模板任务快照、报告清理失败重试、WebP 兼容及 DOCX 关系解析加固。
