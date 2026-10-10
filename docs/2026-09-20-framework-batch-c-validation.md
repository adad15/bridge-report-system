# C 批次：前端加载与依赖整理验收

完成日期：2026-09-20。方案先于实施写入 [变更提案 005](superpowers/specs/changes/2026-09-19-change-005-frontend-loading-batch-c.md)。本批仅修改前端、验证脚本及文档；工作区保留此前 P1/A/B 变更，未提交或部署。

## 实施结果

- App 中 12 个业务页面按路由加载。登录、会话恢复、AppLayout、BridgeWorkspaceShell 保持稳定；桥梁子路由继续共享概览上下文，旧整理地址仍可重定向。
- ComponentRatingSummary 负责空态和评分整理；RatingTrendChart 独立加载，无有效评分不下载图表引擎。等待或失败时显示年份/评分文字；原图表主题、坐标、精度与提示保留。
- 桥梁列表按打开操作加载规范面板、创建桥梁向导。取消后迟到模块不重开；重新打开复用模块并按原行为初始化表单。
- createLazyFeature 共享成功与在途加载；失败重试清理拒绝 promise 并重建局部 lazy 边界。组件渲染异常与模块下载异常分别提示，无自动刷新或提交重放。
- 删除未使用的 @ant-design/pro-components，锁文件删除 53 个包路径条目；保留条目版本变化为 0，新增条目为 0。@vitejs/plugin-react 移到 devDependencies。
- 新增构建依赖图门禁和生产浏览器验证脚本。使用自然分包，未设置笼统 vendor chunk，未上调 500 kB 警告阈值。

## 构建与实际资源请求

按最终磁盘文件的 UTF-8 字节统计 JavaScript；gzip 使用同一 Node gzipSync 默认配置。入口比较包含全部静态依赖，不只比较入口文件名。CSS、接口响应与图片不计入下表。

入口静态闭包：**3,258,979 → 830,313 字节，减少 74.5%**。gzip 估算：**996,454 → 272,120 字节，减少 72.7%**。

浏览器为 Edge Chromium 153.0.4234.46，1440×1000，无头模式、本机 HTTP、合成 API、无网络/CPU 节流。前后各 4 场景 × 3 次独立上下文，共 24 个样本。页面就绪耗时为导航开始至目标业务内容可见的本机中位数，**不是 LCP，也不是生产 SLA**；本机缓存、调度和测量顺序均可能影响时间。

| 场景 | 改动前 JS 字节 | 改动后 JS 字节 | 减少 | 就绪中位数 ms（前 → 后） |
| --- | ---: | ---: | ---: | ---: |
| 登录 | 3,258,979 | 830,313 | 74.5% | 747 → 284 |
| 工作台 | 3,258,979 | 1,169,493 | 64.1% | 732 → 430 |
| 桥梁列表 | 3,258,979 | 1,152,658 | 64.6% | 815 → 485 |
| 构件档案（无评分） | 3,258,979 | 1,193,461 | 63.4% | 701 → 463 |

路由往返复用已加载模块，未再次请求模块。浏览器路由拦截会关闭 HTTP 缓存，因此此项验证的是同一文档的模块复用，不代表跨次访问的 HTTP/CDN 缓存效果。

入口约 0.83 MB、独立图表块约 1.48 MB，仍超过 Vite 500 kB 提示阈值。C 批次减少的是无需使用功能时的加载量；图表库自身大小、入口公共 UI 依赖及弱网下的图表首次等待仍可后续专项优化。

## 验证结果

| 检查 | 结果 |
| --- | --- |
| TypeScript 严格与未使用声明检查 | 通过 |
| 完整前端 Vitest | 83 文件、626 项通过；较 B 批次增加 14 项 |
| 生产构建和依赖图门禁 | 通过，入口无业务页面/图表/重点面板；业务页静态依赖无图表，桥梁列表无隐藏面板 |
| Ant Design lint | 本批 5 个生产 TSX 文件均为 0 问题、无跳过 |
| 锁文件复建 | 项目 prepare-dependencies.ps1 -Component frontend 成功，依赖凭据已刷新 |
| 浏览器冷启动及实际请求 | 24 样本通过；登录/工作台/桥梁/无评分档案不加载图表或隐藏面板 |
| 延迟面板、取消、再打开 | 通过，迟到结果不重开，表单重置，模块复用 |
| 有评分档案 | 独立请求图表块并实际渲染 canvas |
| 图表网络失败与连续重试 | 保留评分文字和档案外壳，无自动重载；主动确认重载后恢复 |
| 页面模块网络失败 | 错误局限于内容区；侧栏仍可切换工作台，重试不自动重载 |

新增测试覆盖身份恢复、业务路由按需导入、桥梁壳保留、旧地址重定向、报告/校对直达地址；加载共享、拒绝后重试、取消迟到结果、父组件更新时草稿保留、渲染错误隔离、手动重载确认；评分空态、排序、过滤和精度。原报告跨年度、创建向导等回归一并通过。

首次离线 npm ci 因缓存缺少 yallist@3.1.1 失败，随后从锁定地址补齐并通过项目准备脚本完成安装。未升级依赖。首次定向测试的两项按钮文字匹配失败已修正，最终完整回归全部通过。既有 React Router future flag 等测试告警未作为失败隐藏；本批文件 lint 结果不代表全仓库无历史告警。

## 复现与证据

在仓库根目录运行：

~~~powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/prepare-dependencies.ps1 -Component frontend
npm --prefix frontend run test -- --run
npm --prefix frontend run build:analyze
~~~

生产浏览器检查使用已有 Playwright 安装，不把仅用于验证的浏览器包加到运行依赖中。指定本机实际路径：

~~~powershell
$env:PLAYWRIGHT_PACKAGE = "C:/path/to/node_modules/playwright"
$env:BROWSER_EXECUTABLE = "C:/path/to/msedge.exe"
node frontend/scripts/check-loading-browser.mjs
~~~

本机保留改动前产物时可加 --baseline，默认位置为 logs/baselines/batch-c/before-dist；没有该目录的新检出运行不带此参数。脚本启动独立临时静态服务与无头浏览器，拦截全部 API，结束时关闭自身资源；未知 API 或非测试源请求使验证失败，不连接业务数据库。

- [构建分析脚本](../frontend/scripts/analyze-build.mjs)
- [浏览器验证脚本](../frontend/scripts/check-loading-browser.mjs)
- 本机证据目录：logs/baselines/batch-c。包含 before.json、build-report.json、browser-report.json、build-before.log、build-after.log、full-tests.log、prepare.log；日志/产物不纳入 Git。

## 边界与后续事项

浏览器可能缓存同一模块 URL 的失败；应用可以重新发起 lazy 尝试，但不能承诺同 URL 自动恢复。保留明确的手动重载入口并提醒未保存内容；不使用随机 query 绕过模块图。

未运行真实业务 API 性能压测、弱网/低配机及多浏览器矩阵。部署时应保留旧哈希资源的合理过渡期，避免已打开页面后续请求的旧 chunk 被删除；本批未改变部署脚本。

本批没有新增或执行业务库迁移，也未重跑 C++/Python/Office 验证；其结果继续以 A/B 历史记录为准。工具链升级、大文件业务拆分、数据库线程模型及图表引擎替换不在本批实施范围。
