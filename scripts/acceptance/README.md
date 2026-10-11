# 集成验收脚本

使用真实 C++、Python、PostgreSQL 和前端生产产物。仅用于新建的隔离验收实例，不应指向业务配置。范围见 [变更提案 006](../../docs/superpowers/specs/changes/2026-09-20-change-006-integrated-business-acceptance.md)，结果见 [验收记录](../../docs/2026-09-20-integrated-business-acceptance-validation.md)。

## 前置条件

Windows、PostgreSQL 18 工具、项目锁定依赖、当前源码 Release 后端、Edge 和 Playwright。Office 检查需要已登录桌面及 Word/WPS。PLAYWRIGHT_PACKAGE 可设为外部 Playwright 包的绝对目录；不设置时按 Node 包解析查找 playwright。BROWSER_EXECUTABLE 可覆盖 Edge 路径；ACCEPTANCE_PG_BIN 可覆盖 D:/PostgreSQL/18/bin。PDF 渲染脚本使用含 pypdfium2 和 Pillow 的外部 Python，不修改项目依赖。

从仓库根目录运行。先按 development.md 准备依赖并构建当前源码，启动脚本默认读取 backend-cpp/build/batch-a/Release/bridge-report-backend.exe。本轮不复用业务数据库或业务归档。

## 建立环境

~~~powershell
& tools-python/.venv/Scripts/python.exe scripts/acceptance/prepare_environment.py
# 使用上一步返回的 environment 路径。
$acceptanceManifest = 'logs/acceptance/<本轮运行ID>/environment.json'
$acceptanceRun = Get-Content $acceptanceManifest -Raw -Encoding UTF8 | ConvertFrom-Json
$env:VITE_BACKEND_BASE_URL = ''
node frontend/scripts/analyze-build.mjs --outDir "$($acceptanceRun.run_directory)/frontend-dist" --report "$($acceptanceRun.log_directory)/frontend-build.json" --check
& scripts/acceptance/start_environment.ps1 -Manifest $acceptanceManifest
& tools-python/.venv/Scripts/python.exe scripts/acceptance/make_small_sample.py $acceptanceManifest
~~~

prepare 创建私有 PostgreSQL 集群、随机数据库密码、数据库和 001～038 迁移。每次新建运行目录，失败时保留环境记录并使用收尾脚本停止本轮集群。静态服务器将 /api 同源转发到本轮 C++，不伪造业务响应。

## 逐步执行

统一命令：

~~~powershell
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-login.mjs
~~~

依次替换最后一个文件名：

1. step-login.mjs：登录全新库管理员种子账户，并断言桥梁列表为空。
2. step-create.mjs、step-inventory-year.mjs：界面创建小样桥、确认台账和 2026 年度。
3. step-import-small.mjs、step-review-small.mjs：界面上传、校对、双页面锁、保存、预检查、正式入库。
4. step-report-setup.mjs：通过真实 API 准备模板、人员和设备。
5. step-report-generate.mjs、step-download.mjs：界面配置、生成、重复请求复用、刷新、下载。
6. convert_legacy_sample.py（Python），随后 step-real-imports.mjs：导入两份真实报告；旧格式原件错误提示由 step-format-error.mjs 验证。
7. step-isolation.mjs、step-real-verify.mjs：跨年度/桥梁/记录切换、真实响应延迟、草稿恢复、照片读取和未确认数据生成阻断。
8. 工具服务故障与恢复，随后 step-performance.mjs、step-module-failure.mjs。

步骤有前后依赖，不是每个都可无条件重跑：重复创建、保存、生成会新增记录或版本。失败后先查日志和 scenario.json，确认检查点再继续。step-report-generate 应在尚未配置的年度运行；重复性能生成使用专门脚本。

~~~powershell
& tools-python/.venv/Scripts/python.exe scripts/acceptance/convert_legacy_sample.py $acceptanceManifest
& tools-python/.venv/Scripts/python.exe scripts/acceptance/inspect_generated.py $acceptanceManifest
# 用含 pypdfium2、Pillow 的外部 Python 执行 render_report.py，参数仍为 manifest。
~~~

转换脚本仅处理 manifest 第二份旧格式样本，写独立 DOCX 副本并核对原件哈希；已有副本时拒绝覆盖。转换是验收准备步骤，应用没有自动转换 DOC 功能。检查脚本复算固定小样评分，使用经进程归属校验的新 Office 实例导出 PDF，不连接已有用户文档。

故障验证只停止本轮 Python，执行前要求没有活动报告任务：

~~~powershell
& scripts/acceptance/python_service.ps1 -Manifest $acceptanceManifest -Action Stop
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-tools-down.mjs
& scripts/acceptance/python_service.ps1 -Manifest $acceptanceManifest -Action Start
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-performance.mjs
~~~

模块失败通过中止真实静态资源请求注入。切换隔离通过暂缓真实 HTTP 响应交付验证，不伪造业务数据。导航使用浏览器 history 触发同一 SPA 路由切换。

## 证据和收尾

状态、会话及配置位于 runtime/acceptance/<ID>，证据位于 logs/acceptance/<ID>，均不应提交到 Git。配置含随机数据库密码，会话文件含验收令牌。

browser-step-*.json 留存成功/失败、异常和 API 耗时；早期记录没有 HTTP status，新记录已补齐。API 准备请求不在页面网络事件中，相应步骤另存业务结果和耗时。

~~~powershell
& tools-python/.venv/Scripts/python.exe scripts/acceptance/verify_environment.py $acceptanceManifest
& scripts/acceptance/stop_environment.ps1 -Manifest $acceptanceManifest
~~~

停止脚本核对运行目录、服务 PID/创建时间/映像、私有 PG 数据目录及端口；保留数据库和日志，不自动删除。中途中断也应执行收尾。重新做完整验收请建立新实例。

## 2026 来源数据库正式评分只读复算

在[正式入库](../../docs/2026-09-21-source-confirmation-acceptance-validation.md)完成后，可按[评分核验记录](../../docs/2026-09-23-source-assessment-audit.md)复核。脚本固定百股大桥2026隔离样本及私有 PostgreSQL 25432，不应指向业务库。先检查 manifest 的数据库名、端口与数据目录，再仅启动本轮私有 PostgreSQL；应用服务不必启动。导出脚本强制只读会话，并核对正式评定、来源导入及年度身份。导出原始证据后停止私有 PostgreSQL，后续复算可离线重跑：

~~~powershell
$assessmentRun = Get-Content $acceptanceManifest -Raw -Encoding UTF8 | ConvertFrom-Json
$assessmentDirectory = [IO.Path]::GetFullPath((Join-Path (Get-Location).Path ("runtime/acceptance/" + $assessmentRun.run_id)))
if ($assessmentRun.database -ne 'bridge_report_acceptance' -or $assessmentRun.ports.postgres -ne 25432 -or
    [IO.Path]::GetFullPath($assessmentRun.run_directory) -ne $assessmentDirectory) { throw '隔离库身份不符' }
& (Join-Path $assessmentRun.postgres_bin 'pg_ctl.exe') -D (Join-Path $assessmentDirectory 'postgres-data') -l (Join-Path $assessmentRun.log_directory 'postgres.log') -o '-h 127.0.0.1 -p 25432' -w start
& tools-python/.venv/Scripts/python.exe scripts/acceptance/export_source_assessment.py $acceptanceManifest
& scripts/acceptance/stop_environment.ps1 -Manifest $acceptanceManifest
$assessmentRaw = 'logs/acceptance/<本轮运行ID>/source-assessment-raw.json'
& tools-python/.venv/Scripts/python.exe scripts/acceptance/audit_source_assessment.py $assessmentRaw
~~~

原始导出和复算结果都位于被 Git 忽略的本轮证据目录，可能含业务事实。复算读取正式观测、已确认台账和版本化规范包，不调用生产评分器；当前固定预期为430条观测、5,174件构件、54条不计分观测和两组25件范围。如来源或年度变化，须重新确定样本预期，不能直接套用这次结论。

## 来源软件数据库路线（当前重点）

结果见 [来源数据库验收](../../docs/2026-09-21-source-database-acceptance-validation.md)。建立并启动隔离环境后，可单独执行以下场景，不必先跑 Word 导入。保持前述 Playwright 和 Edge 配置。

~~~powershell
& tools-python/.venv/Scripts/python.exe scripts/acceptance/snapshot_source_db.py
# 取上一步输出的 manifest，或给脚本提供来源 SQLite 路径作为首个参数。
$sourceManifest = 'logs/source-db-acceptance/<本轮来源运行ID>/source-environment.json'
$sourceRun = Get-Content $sourceManifest -Raw -Encoding UTF8 | ConvertFrom-Json
$env:SOURCE_DB_MANIFEST = (Resolve-Path $sourceManifest).Path
$env:BRIDGE_REPORT_SOURCE_DB_SNAPSHOT = $sourceRun.snapshot_path
& tools-python/.venv/Scripts/python.exe -m pytest tools-python/tests/importers/source_db -q -rs
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-source-db-import.mjs
& tools-python/.venv/Scripts/python.exe scripts/acceptance/audit_source_db.py $sourceManifest $acceptanceManifest
& scripts/acceptance/stop_environment.ps1 -Manifest $acceptanceManifest
~~~

该场景针对现有百股大桥三个固定任务 ID 与预期数量；其他来源库须先调整场景预期，不是任意库的通用批量导入器。使用隔离库管理员种子账号；2024 检查日期为空，脚本显式补入测试日期，不能视为自动提取。创建独立桥梁与三个年度，不自动确认台账或正式入库。

场景检查点位于 scenario.json 的 sourceDatabase；已验证年度重跑时跳过，不代表重新导入。换来源快照应新建验收环境。源库日志与快照位于 source-db-acceptance，界面证据仍位于服务环境的 acceptance 目录。独立对账必须在三个年度成功后执行。

## 来源构件与已确认台账的只读对账

[最新结果](../../docs/2026-09-21-source-binding-and-fidelity-validation.md)。以下工具只导出、解析和离线核验，不执行业务绑定。输出含业务构件资料，应留在被Git忽略的本机日志目录。

~~~powershell
& tools-python/.venv/Scripts/python.exe scripts/acceptance/profile_source_inventory.py $sourceManifest $acceptanceManifest
& tools-python/.venv/Scripts/python.exe scripts/acceptance/replay_source_fidelity.py $sourceManifest $acceptanceManifest
# 改为实际已确认台账版本ID，输出放在本轮证据目录。
$bindingLog = 'logs/source-db-acceptance/<本轮来源运行ID>/binding-and-fidelity'
& tools-python/.venv/Scripts/python.exe scripts/acceptance/export_confirmed_inventory.py config/local.json '<已确认台账版本UUID>' "$bindingLog/confirmed-inventory-current.json"
& tools-python/.venv/Scripts/python.exe scripts/acceptance/run_source_binding_probe.py "$bindingLog/confirmed-inventory-current.json" "$bindingLog/inventory-profile.json" "$bindingLog/binding-probe.json"
~~~

export_confirmed_inventory.py 强制 PostgreSQL 会话为只读，用一条 SELECT 导出台账、所属桥梁、年度引用与人工确认别名；不输出连接密码。--psql 可指定客户端路径。它仅检查版本已确认，不将来源构件节点升级为完整台账。

run_source_binding_probe.py 需要 MSVC x64 与本项目已安装的 jsoncpp，默认依赖位于 backend-cpp/build/batch-a/vcpkg_installed/x64-windows，可用 --dependencies 覆盖。构建产物仅写 runtime，可用 --build-dir 指定其子目录。它从当前源文件编译 ComponentMatcher、ComponentCategoryLexicon、ComponentRangeParser、SideComponentPair，无须启动应用服务。

探针对每个来源年度检查其引用的台账版本，再输出精确匹配、范围目标、两侧可选项和未匹配记录；其含义是“按当前规则可匹配”，不是已应用操作。年度引用不一致时直接失败，不能拿最新台账代替该年度锁定版本。构件范围与来源调查脚本的独立展开预期逐条核对。输入、源码、程序和输出哈希保存在 binding-probe-identity.json。

41条人工建议见当前验收记录及 binding-action-proposals.json。建议不会被工具自动写回或套用。新增台账或来源样本应重新调查，不能沿用本轮固定数量结论。


## 2026 来源数据绑定与正式入库

这是固定百股大桥样本的隔离验收，步骤会写入私有验收库，不能用于业务环境。先使用已有的三年度导入状态和已确认台账只读导出。当前脚本固定前端25173、私有PG25432及样本/台账证据路径；不是任意数据的通用导入器。结果见 [正式入库验收](../../docs/2026-09-21-source-confirmation-acceptance-validation.md)。

~~~powershell
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-source-confirm-prepare.mjs
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-source-confirm-bind.mjs
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-source-confirm-review.mjs
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-source-confirm-manual.mjs
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-source-confirm-commit.mjs
& tools-python/.venv/Scripts/python.exe scripts/acceptance/verify_source_confirmation.py $acceptanceManifest $sourceManifest
& scripts/acceptance/stop_environment.ps1 -Manifest $acceptanceManifest
~~~

sourceConfirmation 独立保存检查点，不覆盖 sourceDatabase 旧导入。prepare 通过 API 生成台账并逐件比对，界面确认台账及导入；bind/review/manual/commit 使用真实页面操作。manual 中的节点与冲突保留选择仅用于当前隔离样本，不得当作自动业务规则。已确认样本不应再次运行提交步骤；重新验收应准备独立环境和状态。验证脚本读取已有证据及正式事实，核验430条观测、所有尺寸和455张照片，以及范围/两侧的唯一照片持有者。

## 2026 来源数据库报告输出

在[正式入库](../../docs/2026-09-21-source-confirmation-acceptance-validation.md)和[评分核验](../../docs/2026-09-23-source-assessment-audit.md)后，按[变更提案010](../../docs/superpowers/specs/changes/2026-09-23-change-010-source-report-output-acceptance.md)执行。下面脚本只适用于本轮固定的隔离年度及样本；`step-source-report-refine.mjs` 需要已有首份报告及四项空白封面，且会替换本轮私有模板，不能对业务实例或已经修复的场景重复运行。配置和日志含测试数据。

~~~powershell
$env:PLAYWRIGHT_PACKAGE = '<本机 Playwright 包目录>'
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-source-report-generate.mjs
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-source-report-status.mjs
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-source-report-download.mjs
# 首份报告及缺陷证据已保留后，在原私有实例中人工配置封面并替换修复后的模板：
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-source-report-refine.mjs
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-source-report-status.mjs
node scripts/acceptance/browser-step.mjs $acceptanceManifest scripts/acceptance/step-source-report-download.mjs
& tools-python/.venv/Scripts/python.exe scripts/acceptance/render_source_report.py export $acceptanceManifest
& <含 pypdfium2 与 Pillow 的 Python> scripts/acceptance/render_source_report.py inspect $acceptanceManifest
& tools-python/.venv/Scripts/python.exe scripts/acceptance/verify_source_report.py $acceptanceManifest
& scripts/acceptance/stop_environment.ps1 -Manifest $acceptanceManifest
~~~

生成和字段更新约需一分多钟，状态为 `ready` 后再下载；PDF 导出及 `source-report-render-fixed.json` 须先按验收记录生成，独立核验脚本才会比对真实页码。脚本以正式导出 `source-confirm-database.json` 和 `source-assessment-raw.json` 作依据；其它来源库或年度应重新建立预期。
