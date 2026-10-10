# B 批次报告与地图可靠性验证

日期：2026-09-18～2026-09-19。实现范围见 [技术方案 004](superpowers/specs/changes/2026-09-18-change-004-report-map-reliability-batch-b.md)。改动保存在工作区，未提交或部署；保留原 P1/A 批次及用户已有差异，没有迁移业务数据库。

## 交付内容

| 项目 | 结果与关键边界 |
| --- | --- |
| B1 清理 | 独立 report_job_artifacts 跟踪任务目录；领取令牌、退避重试、过期领取回收；到期立即禁用下载。文件占用和部分删除可重试，历史行删除不丢清理记录。Office/工具写入存活不明时 blocked |
| B2 模板 | 文件写入前登记目录；行锁内复制有大小限制的模板并核对 SHA-256，固定配置与校验结果后才入队；后续预检和 ReportContext 使用快照 |
| B3 图片 | 实际格式判断；小/大 WebP 转兼容格式，透明图 PNG，先转向再计算缩放；缓存名称独占，原图字节保持 |
| B4 包检查 | ZIP 目录检查失败后不读内容；XML 原始字节有界解析，拒绝 DTD/实体、错误命名空间和非法关系；完整关系 URI 判断 |
| B5 自动地图 | 桥梁行锁内比较坐标、原文件、单调修订号和人工来源；手工上传后删除也能检测，失败仅清理本次独占新文件 |
| B6 前端地图 | 容器保持挂载，失败可重试；每次 effect 独立持有并销毁地图实例；取消迟到结果，Loader 清理失败脚本和计时器 |

迁移 037 不移除旧下载字段约束。过期时任务行清空下载路径，清理台账保留实际核对路径；失败重试以台账为准。历史 failed 任务缺少可靠存活信息，迁移为 blocked。恢复与升级步骤见 [运行指南](../development.md)。

## 构建与完整回归

日志位于仓库的 logs/baselines/batch-b；该目录为本机验证产物，不纳入代码提交。

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| 前端 TypeScript + Vite 生产构建 | 通过 | frontend-build.log |
| 前端完整 Vitest | 80 文件、612 项通过 | frontend-full.log |
| Python 完整 pytest | 507 通过、30 跳过、1 警告 | python-full.log |
| Release C++ 构建 | 后端、上下文工具与测试均通过 | build-fifth.log |
| C++ / PostgreSQL 完整回归 | 1001 通过、2 跳过，共 1003 项 | backend-second.log |
| 数据库 | 37 迁移执行两遍、18 SQL smoke 通过 | backend-second.log |
| 地图定向回归 | 17 项通过 | frontend-map.log |
| Ant Design lint（地图卡片） | 0 项问题 | antd-lint.json |
| 最终 Office 所有权/FIFO 定向回归 | 21 项通过 | python-office-unit-final.log |
| 最终报告 API（含 WebP 装配与写入状态） | 11 通过、2 项 Office 用例跳过 | python-endpoints-verified.log |
| 差异格式 | git diff --check 通过 | diff-check.log |

独立 schema 为 bridge_report_test_batch_b_20260918，成功后脚本已删除。C++ 两条跳过为需显式开启的基准测试。Python 默认跳过需真实样本/Office 开关的用例。最后新增 API 用例单列，不能把不同轮次数字相加称为一次完整回归。

前端生产包约 3224.97 kB（gzip 996.45 kB），仍有大于 500 kB 的既有提示；Python 存在 Starlette/httpx 弃用警告。本批未升级依赖或实施前端分包。

## 修复过程中的失败及处理

- 首轮 C++ 回归暴露旧 expired_has_no_file 约束与保留任务行路径的冲突：改为独立台账保留清理目标，兼容旧约束。
- PostgreSQL LIMIT 参数在二进制绑定时推导为 bigint：显式指定 integer 后通过。
- Windows 文件占用测试的 Windows.h 与 Drogon 头顺序冲突：调整包含顺序后 Release 构建通过。
- 初次 Python 定向运行虽执行到全部断言通过，但系统 pytest 临时目录清理遇到权限错误：后续统一使用工作区独立 basetemp，完整回归正常退出。
- 最后 API 错误注入最初替换了路由函数名，未影响 FastAPI 已注册的回调：改为替换回调内部调用的服务函数，再重跑该文件。失败日志保留，未当作通过证据。

## 本机 Office

目录更新、输入文档不变、多节页码的 6 项测试通过（4 项未选择），使用任务临时文档与经 COM 窗口确认归属的新自动化实例。证据：python-office-real.log。

日志仍出现 WPS COM/RPC 0x800706be / 0x800706ba 诊断输出，但测试断言通过、进程退出码为 0。当地两个 ProgID 对应 WPS 的既有环境；这不替代真正 Microsoft Word 的交叉打开验收，也不代表全部 Office 故障均已消除。

## 合成报告装配采样

可复现脚本和结果位于 logs/baselines/batch-b/measure_assembly.py、assembly-samples.json。采用当前测试模板，每条病害一张 WebP；混合 80×40 与 1600×1200，包含透明图。每档连续运行三次，输出再由 python-docx 打开并检查图片数，300 份源文件摘要均保持不变。

| 病害 / 图片 | 三次装配秒数 | 中位数 | DOCX 约大小 |
| --- | --- | --- | --- |
| 10 / 10 | 0.719、0.606、0.723 | 0.719 秒 | 43 kB |
| 100 / 100 | 7.029、6.301、6.093 | 6.301 秒 | 91 kB |
| 300 / 300 | 25.497、26.681、22.963 | 25.497 秒 | 207 kB |

这是低纹理合成图片和测试模板的装配样本，不含网络取图、数据库上下文和 Office 排版，不代表真实业务照片体积、端到端耗时或优化前后提升比例。真实 34 页样本、Microsoft Word、跨机器性能仍需单独验收。

## 最终边界

- 未改业务评分、导入合同与现有框架；无依赖锁升级。
- 测试覆及真实文件占用、部分清理后重试、错误根目录、领取过期、历史删除、模板配置锁与复制失败、WebP/透明/转向、UTF-16/前缀/单引号/DTD/重复条目、地图上传/删除竞态与前端重试。
- 路径实现拒绝符号链接和 Windows reparse point；本轮没有单独创建真实 Windows junction 做验收，也没有模拟断电、磁盘硬件故障或多主机共同清理。不存在覆盖这些场景的测试声明。
- 未扫描删除无台账的旧孤儿目录；未确认写入停止的 blocked 项保留待运维核实。
- .gitignore 的 runtime/ 改为 /runtime/，避免 A 批次新增 C++ runtime 源码被误忽略；运行时数据仍被根目录规则排除。
