# 2026-09-17 审查修复状态

本记录汇总本次前后端审查、已授权代码修改及验证范围。状态截至 2026-09-20，分支为 `07-begin-word`；修改尚在工作区，未提交或部署。项目当前能力见 [PROJECT_CONTEXT](../PROJECT_CONTEXT.md)，命令及恢复步骤见 [运行与验证](../development.md)。

## 4 条 P1：已修复

| 原编号 | 问题 | 当前处理 |
| --- | --- | --- |
| 1 | Office 超时按 PID 差集清理，可能误杀期间新开的用户实例 | COM 窗口确认 PID，校验映像和创建时间，持有进程句柄；归属或退出不明时阻断队列 |
| 2 | 报告页跨年度迟到响应导致配置串写 | 按桥梁/年度重建状态，活动状态及请求序号校验，旧保存/生成/轮询响应不再写回，加载时禁用提交 |
| 3 | 图件仓储未等提交确认就返回，文件层提前删除旧图 | 桥梁行锁、提交回调同步等待；不确定提交返回 503，保留新旧文件 |
| 4 | 相同图片复用哈希路径，失败回滚删除旧文件 | 每次上传 UUID + 哈希独立文件名，暂存后发布，旧路径兼容读取 |

代码入口：

- [Office 所有权](../tools-python/bridge_report_tools/reports/office_process.py)、[字段更新器](../tools-python/bridge_report_tools/reports/field_updater.py)、[FIFO 队列](../tools-python/bridge_report_tools/reports/field_update_queue.py)。
- [年度报告页](../frontend/src/pages/ReportGenerationPage.tsx)。
- [图件仓储](../backend-cpp/src/db/BridgeMediaRepository.cpp)、[文件与事务协调](../backend-cpp/src/report/BridgeMediaStore.cpp)、[图件归档](../backend-cpp/src/archive/BridgeMediaArchive.cpp)。

Office worker 使用 daemon 线程与 Future，超时返回不等待卡住的 COM 线程无限结束。WPS 不支持应用 Hwnd 时，通过本次创建的隐藏空白文档窗口认领进程；该标记文档保留到 Quit，避免 WPS 在最后一份文档关闭时提前退出。不能验证归属时不调用未知实例的 Quit/强杀，队列进入需人工核实状态。

图件提交结果不确定不是已确认回滚。错误码 `bridge_media_commit_uncertain` 返回 503，日志留存核对信息；只有确认提交成功才清理旧文件，明确的提交前失败只清理本次独占新文件。

## 冗余代码：已清理

14 处 TypeScript 未使用声明已清理：构件编辑器的 searchLoading、桥梁概览外层 pageOffset、校对页 bindingOverview、线索页 Button 导入、绑定工作台测试的 inventory 夹具、DefectsSection 的 CSSProperties/FormEvent 导入与 STRUCTURE_PART_LABELS/matchedAt/selectedMapping，以及 reviewDraft 的 RatingTreeMatchMethod 导入、nextUnusedCandidateId、candidateIdFactory、issuedCandidateIds。

同步删除失去用途的相关导入、类型、默认候选 ID 工厂和 reducer 工厂包装。仍在实际使用的子组件 pageOffset 保留；异步检索、绑定状态读取与业务行为保留。

[reviewDraft.ts](../frontend/src/review/reviewDraft.ts) 直接导出纯 `reviewDraftReducer(state, action)`。手工病害创建仍走 C++ `manual-defects` 专用命令；前端拿到返回草稿后通过 `replace_draft` 更新，不在 reducer 内生成 ID。测试改用同一 reducer，原断言保留。

[tsconfig.json](../frontend/tsconfig.json) 已开启 `noUnusedLocals` 与 `noUnusedParameters`，后续构建会拦截同类冗余。

## P2：原第 5～10 条已在 B 批次修复，第 11 条已在 A 批次修复

第 5～10 条已按 [B 批次技术方案](superpowers/specs/changes/2026-09-18-change-004-report-map-reliability-batch-b.md) 实施，工作区代码和测试已更新；实测与限制见 [B 批次验证](2026-09-19-framework-batch-b-validation.md)。

| 原编号 | 问题及触发条件 | 已实施处理 |
| --- | --- | --- |
| 5 | 任务读取当前归档模板；生成期间替换模板可删掉其文件，文件与配置版本可能不一致 | 任务独占模板副本；短事务/行锁下读取并校验文件、配置、版本和摘要，后续预检与上下文使用同一份快照 |
| 6 | 报告到期删文件失败仍清空路径，后续无法重试 | 下载到期与物理删除分离；失败保留路径和错误记录，已过期任务也重试，清理成功后才移除追踪信息 |
| 7 | 小 WebP 未触发缩放而直接返回原图，python-docx 无法嵌入 | 格式转换与缩放分别判断；WebP 始终转兼容格式，透明图 PNG、非透明图 JPEG，处理 EXIF，仅写任务缓存 |
| 8 | 使用正则读取 DOCX .rels，可被引号、命名空间或编码变化绕过 | 直接解析 XML 字节和标准命名空间，严格限制关系类型，拒绝 DTD/实体和非法 XML，先执行 ZIP 大小限制 |
| 9 | 地图初次失败移除容器，重试时实例没有可用挂载点 | 调整容器与实例生命周期，验证失败后重试和卸载 |
| 10 | 自动取图网络请求前检查手工图，期间上传手工图可被自动结果覆盖 | 落库时重新检查来源/版本，使用条件写入保护并发手工修改 |
| 11 | 后端启动脚本不检查 CMake 原生退出码 | **2026-09-18 已修复**：配置/构建失败停止，禁止旧 EXE 回退；见 [A 批次验证](2026-09-18-framework-batch-a-validation.md) |

对应代码：[任务执行器](../backend-cpp/src/report/ReportGenerationRunner.cpp)、[任务清理仓储](../backend-cpp/src/db/ReportGenerationJobRepository.cpp)、[图片重采样](../tools-python/bridge_report_tools/reports/photo_resampler.py)、[包关系检查](../tools-python/bridge_report_tools/reports/package_guard.py)、[地图卡片](../frontend/src/bridges/BridgeLocationCard.tsx)、[自动取图](../backend-cpp/src/report/LocationMapGenerator.cpp)、[后端启动脚本](../scripts/dev/start-cpp-backend.ps1)。

B 批次已补充文件占用与重试、历史删除后台账保留、模板复制失败与并发锁、图片与 XML 变体、自动地图手工修改/删除竞态、地图 Loader 重试与卸载测试。下载资格与独立清理台账分离，写入不明时保留目录待核实。

## 框架与优化结论

C++ 负责事实与编排、Python 处理文档、React 呈现界面的职责边界可以继续沿用。本轮未发现 React 18 与当前 Ant Design 6 之间的直接依赖冲突证据，不需要为了本轮修复替换框架。

C 批次已完成前端按需加载与依赖整理，详见 [验收记录](2026-09-20-framework-batch-c-validation.md)。当前状态及后续工作：

- 未使用的 `@ant-design/pro-components` 已移除，锁文件同步，保留依赖版本未升级。
- 已安装 Vite 为 5.4.21；工具链升级需要另行评估。
- 页面、图表及重点面板已按需加载，入口静态 JS 减少 74.5%；入口和图表块仍超过 500 kB，可后续专项优化。
- 大型文件拆分、同步数据库操作对 HTTP 循环的影响需要专项处理。CMake/vcpkg 可移植性已纳入 2026-09-18 A 批次工程改造。
- Ant Design lint 仍提示 ThreadTriagePage 的既有 Alert.onClose 弃用用法。

## C 批次最新验证

前端 83 文件、626 项通过，类型检查、生产构建依赖门禁和本批 5 文件 lint 通过；24 个生产浏览器合成样本及延迟/失败恢复通过。仅验证前端，后端与 Office 结果沿用 B 批次历史记录。

## B 批次验证（历史）

完整回归：前端 612 项通过，Python 507 项通过/30 项跳过，Release C++ 1001 项通过/2 项跳过；37 个迁移执行两遍、18 个 smoke 文件通过，隔离 schema 已删除。最后新增 API/Office 用例、本机 Office 及合成装配采样见 [B 批次验证记录](2026-09-19-framework-batch-b-validation.md)。新增迁移 037 尚未应用于业务库。

## P1 阶段已执行验证及其范围（历史记录）

| 验证 | 结果 | 范围与限制 |
| --- | --- | --- |
| 报告年度页 | 17 项通过 | 包含跨年度加载、保存、生成和轮询竞态 |
| 前端冗余清理 | 8 文件、178 项通过 | ComponentInventoryEditor、BridgeOverviewPage、ReviewWorkspacePage、ThreadTriagePage、ComponentBindingWorkspace、DefectsSection、reviewDraft、defectPhotoReviewModel |
| TypeScript 与前端生产构建 | 通过 | 开启未使用检查；Vite 主包约 3224 kB，gzip 996 kB，仍有 >500 kB 提示 |
| Python 完整测试（P1 修复过程中） | 444 通过、30 跳过 | 有 1 条 Starlette/httpx 弃用警告；最后 Office 调整后未再跑完整套件 |
| 最终 Office 所有权与 FIFO 定向测试 | 19 项通过 | 所有权、超时、迟到启动、队列阻断等模拟验证 |
| 真实 Office 临时文档用例 | 6 项通过 | 目录、输入保留、多节文档；本机实际 WPS，仍出现 COM/RPC 诊断输出，不等于真 Word 验收 |
| 图件 C++/PostgreSQL | 18 项通过 | 使用独立测试 schema，覆盖提交延迟/失败和文件完整性 |
| 数据库验证 | 36 迁移应用两遍、17 smoke 文件通过 | 同一独立 schema，完成后已删除；未向业务 schema 应用本轮迁移 |

原审查时的前端 597 项、Python 433 项及 C++ 559 通过/420 跳过属于更早基线，不能用来描述修复后的全量状态。表内各组测试存在不同执行时间和范围，不能简单相加作为统一全量报告。

既有后端 EXE 被运行进程占用，最终构建和相关测试采用独立输出目录；没有重启业务后端。P1 阶段源码修复未增加数据库迁移，也未改变生产数据；后续 B 批次新增迁移 037，仅在隔离 schema 验证。

## 历史报告验收边界

报告设计中 2026-09-11 的规模数据（59 页/166 图、101 页/455 图）和当时 12/13 条通过保留为历史记录。旧超时测试只覆盖“已有用户实例”，不能证明“测试期间新开的用户实例”安全；旧 PID 差集策略已被本轮所有权机制替代。

Microsoft Word 真机交叉打开、常规 34 页样本仍待补测。P2 修复状态以 A/B 批次代码与回归为准；这些历史主流程验收不覆盖新的异常路径。
