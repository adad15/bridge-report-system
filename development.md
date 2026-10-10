# 本地运行、验证与故障恢复

更新日期：2026-09-21。项目入口见 [README](README.md)，已知缺陷见 [审查修复状态](docs/2026-09-17-review-remediation-status.md)。

## 环境与配置

- Windows、Visual Studio C++ 工具链、CMake/vcpkg、PostgreSQL 客户端与服务器、Node.js/npm、Python 3.11+ 和 uv。
- C++ 构建定义见 [CMakeLists.txt](backend-cpp/CMakeLists.txt) 和 [预设](backend-cpp/CMakePresets.json)。公共预设通过 VCPKG_ROOT 定位工具链；本机覆盖使用 CMakeUserPresets.json。
- 本地配置字段以 [local.example.json](config/local.example.json) 为准。前端调用 C++，C++ 调用本机 Python 工具服务。
- Word/WPS 自动化须在已登录的交互式 Windows 桌面会话运行。不要把当前实现直接作为无桌面的 Windows Service 或 Linux 字段更新服务。
- Python 字段更新队列在进程内唯一；部署保持单个 Python 服务进程，多 worker 不共享这把锁。

报告临时目录及清理参数：

| 配置键（temporary_storage 下） | 默认值 | 含义 |
| --- | --- | --- |
| report_root | runtime/temp/report-jobs | 任务上下文、中间文件和下载文件 |
| report_retention_hours | 24 | 报告文件保留时间 |
| report_job_history_days | 7 | 终态任务历史保留配置 |
| report_field_update_timeout_seconds | 900 | 单次字段更新时间上限 |

字段更新队列等待上限在 Python 中为 1800 秒，与字段更新执行超时不同。当前报告清理失败后仍可能丢失文件路径，不能把上述保留参数理解为已实现可靠的删除重试。

地图配置分浏览器 JS key、安全码和服务端 Web Service key。坐标以 WGS-84 入库；浏览器地图及 Python 静态取图各自在调用高德时转换。没有坐标或服务端 key 时不自动取图，取图失败不阻断报告。手工位置图优先；自动结果落库时复查坐标、原文件和位置图修订号，取图期间发生手工修改则放弃该次自动写入。浏览器地图失败可点击重试，静态图继续提供降级显示。

## 启动和构建

A 批次启动入口兼容 Windows PowerShell 5.1 / PowerShell 7。以下命令从仓库根运行；从其他目录调用时使用脚本绝对路径，脚本内部按自身位置定位项目。

首次准备或依赖输入改变时：

```powershell
$env:VCPKG_ROOT = "D:/vcpkg" # 改成本机实际路径
powershell -ExecutionPolicy Bypass -File scripts/dev/prepare-dependencies.ps1
powershell -ExecutionPolicy Bypass -File scripts/dev/check-environment.ps1 -OutputPath logs/environment.json
```

已验证版本记录在 [toolchain.json](scripts/dev/toolchain.json)：Node 24.15.0、npm 11.12.1、Python 3.13.9、uv 0.9.6。准备入口执行 npm ci 和 uv sync --locked --extra dev，保存输入摘要及工具版本；Python 解释器按记录版本选择。其他版本需独立验证后更新记录。镜像可使用本机 npm 配置或 -NpmRegistry；锁文件中的 resolved 地址仍影响下载来源。

配置优先级：-ConfigPath → config/local.json → config/local.example.json。显式路径错误、配置对象/端口错误或服务地址不一致时直接失败。数据库连接使用 postgres 节；应用启动不支持 BRIDGE_REPORT_DATABASE_URL，检测到该变量会停止并说明原因。维护脚本和测试专用连接变量见下文。

使用新的构建目录启动：

```powershell
$env:VCPKG_ROOT = "D:/vcpkg"
powershell -ExecutionPolicy Bypass -File scripts/dev/start-all.ps1 -BuildDirectory backend-cpp/build/batch-a -Configuration Release
powershell -ExecutionPolicy Bypass -File scripts/dev/check-health.ps1
```

默认参数仍为 backend-cpp/build/vs-debug 和 Debug；产物按“构建目录 / Configuration / Target”选择。缓存若使用旧 toolchain、triplet 或公共预设之外的依赖目录，会要求选择新目录或明确的本机预设。共享安装目录参考 [用户预设示例](backend-cpp/CMakeUserPresets.example.json)，保存为已忽略的 CMakeUserPresets.json。

统一入口依次检查端口、依赖、数据库，配置并构建 C++，再启动 Python、C++、前端。所有原生命令检查退出码，失败不运行旧 EXE。Python/C++ 核对 launch_id、instance_id 和源码/构建摘要；C++ 另核对数据库及 Python 连通性；前端核对进程、严格端口和 HTTP 响应。每个服务就绪等待默认最多 60 秒，可用 -ReadyTimeoutSeconds 调整。

日常启动不安装依赖；需要一次完成时显式加 -PrepareDependencies。分别启动使用 start-python-tools.ps1、start-cpp-backend.ps1、start-frontend.ps1，在当前终端运行服务。Python 保持单 worker，无热重载。

统一启动的服务在后台运行，日志与启动记录位于 logs/startup/<launch_id>/。可用 -OpenBrowser 打开页面。停止时使用启动输出中的记录路径：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/stop-all.ps1 -StartupRecord "logs/startup/<launch_id>/startup.json"
```

停止入口核对 PID、创建时间和映像；失败清理持有进程句柄，并核对父子创建时间。父进程已退出而无法确认后代时会警告并保留，不按名称批量终止。日常停止前应等待报告/导入任务完成，该开发工具不提供业务任务排空功能。

运行中的 EXE 导致 LNK1168 时构建失败，不自动结束已有实例。Python 源码变化后须重启；健康接口保留启动时摘要。单独 check-health 检查身份字段与依赖；若要比较指定构建，传入 ExpectedBuildId、ExpectedPythonDigest 和 LaunchId，统一入口已自动传入。

## 前端

在 frontend 目录：

```powershell
npm run test -- --run
npm run build
```

不带 `--run` 的测试命令进入 Vitest 监听模式。生产构建执行 `tsc -b && vite build`，包括 `strict`、`noUnusedLocals`、`noUnusedParameters` 检查。

C 批次已完成页面、图表和重点面板按需加载。运行 npm run build:analyze 可构建并检查静态依赖边界，报告位于 logs/baselines/batch-c/build-report.json。入口和独立图表块仍超过 Vite 的 500 kB 提示阈值，未提高阈值隐藏提示。浏览器合成验证方法及 626 项前端回归见 [C 批次验收](docs/2026-09-20-framework-batch-c-validation.md)。模块失败先局部重试；确认无未保存内容后再手动重载，不自动刷新。修改 Ant Design 界面时按 [UI 规范](docs/ui-design-system.md) 执行相关测试及 lint。

## Python 与 Office

在 tools-python 目录：

```powershell
uv run --locked pytest
uv run --locked pytest tests/reports/test_office_process_ownership.py tests/reports/test_field_update_queue.py
```

默认跳过受环境开关控制的真实 Office 用例。需要真机字段更新验证时，在交互式会话中执行：

```powershell
$env:BRIDGE_REPORT_OFFICE_TESTS = "1"
uv run --locked pytest tests/reports/test_field_updater.py -k "fills_toc or input_document or many_sections"
Remove-Item Env:BRIDGE_REPORT_OFFICE_TESTS
```

这组选择覆盖临时文档的目录、输入保留及多节排版，不代表所有 Office 验收用例。应确认 ProgID 对应的实际应用；本机 Word.Application 和 KWPS.Application 均指向 WPS，仍缺真正 Microsoft Word 的交叉打开验证。

## C++ 与 PostgreSQL

先构建与测试一致的配置，再从仓库根目录执行（以下使用 Release）：

```powershell
$env:VCPKG_ROOT = "D:/vcpkg"
cmake --build backend-cpp/build/batch-a --config Release --target bridge_report_backend_tests
$env:BRIDGE_REPORT_TEST_SCHEMA = "bridge_report_test_docs_check"
powershell -ExecutionPolicy Bypass -File scripts/dev/check-backend-tests.ps1 -BuildDirectory backend-cpp/build/batch-a -Configuration Release
Remove-Item Env:BRIDGE_REPORT_TEST_SCHEMA
```

脚本读取 `BRIDGE_REPORT_TEST_DATABASE_URL`（未设置时使用脚本中的本地开发连接），要求 schema 名匹配 `bridge_report_test[a-z0-9_]*`。它会重建指定测试 schema，顺序应用全部 37 个迁移两遍、运行 18 个 SQL smoke 文件和 C++ 测试，成功后删除测试 schema；失败时保留现场。仅给它指定可丢弃的测试 schema。

测试中的 legacy fixture 放行选项仅限测试进程；迁移 smoke 在默认严格模式运行，不应复制这些开关到业务后端。

另一个命令：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-database.ps1
```

它读取 `BRIDGE_REPORT_DATABASE_URL` 并直接应用迁移与运行 smoke，没有创建隔离 schema。用于有意更新/检查目标开发数据库，不替代隔离测试。A 批次只执行隔离 schema 回归，未对业务 schema 运行这个维护入口。

## B 批次升级与清理台账

新版本要求迁移 037。升级前停止受理新报告并等待已有任务结束，备份目标数据库；在明确选择的目标库顺序应用尚未执行的迁移，再启动同一版本 C++ 和 Python。B 批次开发验证只更新了隔离测试 schema，没有迁移业务库。

报告任务在入队前复制模板并固定配置、摘要和校验结果。下载到期立即失效；任务行清空下载字段，独立的 report_job_artifacts 保存目录核对路径，定时重试清理。一次最多领取 25 项；失败等待 30 × 尝试次数秒、最长一小时；领取超过 15 分钟可重试。只有物理清理成功才回收快照，未清理成功的任务历史不会自动删除。

迁移对旧 failed 任务采用 blocked，避免猜测此前 Office 是否仍在写入。工具通信中断、刷域超时、恢复阻断及服务重启时仍在文档处理阶段的任务也进入 blocked。不要直接清空台账或扫描删除所有 UUID 目录。

管理员排障先查询：

~~~sql
select job_id, cleanup_state, attempts, next_attempt_at,
       expected_output_path, last_error
from report_job_artifacts
where cleanup_state <> 'cleaned'
order by created_at;
~~~

对照指定 job_id 的日志、目录及自动化实例，确认没有 C++/Python/Office 继续写入后，才可对这一个任务解除阻断。下面是维护示例，须替换为已核实的 UUID，并在正确 schema 执行：

~~~sql
update report_job_artifacts
set cleanup_state='pending', next_attempt_at=now(), last_error=null
where job_id='<已核实的任务UUID>'::uuid and cleanup_state='blocked'
  and not exists (
    select 1 from report_generation_jobs j
    where j.id=report_job_artifacts.job_id
      and j.status in ('queued','validating_data','assembling_docx','updating_fields','validating_docx')
  )
returning job_id;
~~~

根目录配置改变而旧文件未搬迁时，路径核对会失败并保留台账；应先核对原受控根目录、恢复配置或制定明确搬迁方案。不要为让重试通过而随意改写 expected_output_path。目录链接/联接会被拒绝。

回退应用前同样要停任务、核对 pending/blocked 台账；新增表列可保留，不能丢弃仍追踪文件的台账。旧版本不理解新的清理和快照语义，不能仅替换 EXE 并继续受理任务。

## 故障恢复

### REPORT_FIELD_UPDATE_RECOVERY_REQUIRED

Office 进程归属或退出状态无法确认时，队列停止放行当前等待及后续请求。核对任务日志与实际自动化实例，确认并关闭残留自动化实例后，重启 Python 工具服务再重试。

清理仅针对由 COM 窗口认领、创建时间与映像校验通过的本次进程，使用持续持有的进程句柄。不要使用进程列表差集或按名称批量结束 Word/WPS 作为恢复步骤。

### bridge_media_commit_uncertain

图件上传或删除返回 HTTP 503 表示数据库提交结果不能确认，程序保留相关新旧文件。先刷新图件列表并核对日志、数据库引用和实际文件，再判断是否需要重试；不能把 503 当成已确定回滚并手工删除文件。

相同内容重复上传也会生成不同物理文件。独立路径防止失败上传误删旧图；旧归档路径仍可读，不需迁移文件名。

## 文档与验证记录

C 批次结果见 [前端加载验证记录](docs/2026-09-20-framework-batch-c-validation.md)。B 批次结果见 [报告与地图验证记录](docs/2026-09-19-framework-batch-b-validation.md)。A 批次结果及未测项见 [框架验证记录](docs/2026-09-18-framework-batch-a-validation.md)。此前结果见 [审查修复状态](docs/2026-09-17-review-remediation-status.md)。只更新文档时检查相对链接、代码路径、配置名及差异格式，不把既有测试结果写成文档更新后新跑的全量测试。


## 隔离业务集成验收

按 [脚本说明](scripts/acceptance/README.md) 建立私有 PostgreSQL 集群及服务端口，使用真实 API 与生产前端。中断也应运行 stop_environment.ps1 按本轮 manifest 停止服务并保留证据。不要对业务库运行这些场景。结果见 [集成验收](docs/2026-09-20-integrated-business-acceptance-validation.md)。

遇到 docx_open_failed，检查是否为标准 DOCX；旧 DOC 仅改扩展名无效，应在 Word/WPS 另存未加密 DOCX。系统支持 DOCX 中 DrawingML/VML 嵌入图片，没有自动转换旧 DOC 功能。

当前优先来源软件数据库，按脚本说明中的来源库章节创建只读快照并进行 UI 导入和独立对账。三年度结果与未覆盖项见 [来源数据库验收](docs/2026-09-21-source-database-acceptance-validation.md)。

来源构件只读对账可直接导出指定已确认台账，用当前C++生产匹配函数离线检查，不需启动应用。命令见脚本说明；最新 [绑定对账记录](docs/2026-09-21-source-binding-and-fidelity-validation.md) 区分匹配结果、人工建议和尚未执行的业务操作。


2026来源数据库绑定与正式入库已在私有验收环境完成，见 [验收记录](docs/2026-09-21-source-confirmation-acceptance-validation.md)。复跑命令见 scripts/acceptance/README.md；verify_source_confirmation.py 强制只读会话和固定隔离数据库身份，不对业务库执行入库。

2026来源数据库正式评分的只读导出、离线复算及结果见[评分核验记录](docs/2026-09-23-source-assessment-audit.md)；后续隔离报告输出已完成，见[报告输出验收](docs/2026-09-23-source-report-acceptance-validation.md)和[验收脚本说明](scripts/acceptance/README.md)。报告封面四项从年度报告配置独立填写；模板末节横向附录页码已修复。隔离验收不启动或写入业务库。
