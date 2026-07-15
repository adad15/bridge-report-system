# 模块 06 验收问题修复实施计划

> 日期：2026-07-15
>
> 依据：`docs/superpowers/specs/2026-07-15-module06-acceptance-fixes-design.md`

## 1. 实施原则

- 保留工作区现有账号、重开和模块 06 变更，避免覆盖用户未提交内容。
- 不提交或删除 `.claude/`、`test-inputs/`、`test-output/`、`backend-cpp/archive/`、`tools-python/archive/`。
- 每项后端边界先补失败测试，再实现最小修复。
- 正式事实写入只信任 C++ 后端验证和复算结果。
- 编辑锁的数据库竞争、重开原子性和写接口授权必须由 PostgreSQL 集成测试覆盖。

## 2. 任务一：0 扣分跨语言一致性

涉及文件：

- `tools-python/bridge_report_tools/scoring/component_score.py`
- `tools-python/tests/test_component_score.py`
- `backend-cpp/src/review/ComponentScore.cpp`
- `backend-cpp/tests/test_component_score.cpp`
- `frontend/src/review/componentScore.ts`
- `frontend/src/review/componentScore.test.ts`
- 共享评分样例文件（如现有 fixture）

步骤：

1. 增加 `[0]`、`[0,0]`、`[35,0]`、`[35,20,0]`、`[100,0]` 测试。
2. 将非法范围从 `(0,100]` 改为 `[0,100]`。
3. 修正预检和导入器中把 0 当成缺失或非法的判断及提示。
4. 运行三端评分定向测试。

## 3. 任务二：最终评分绑定

涉及文件：

- `backend-cpp/src/review/PreflightReport.cpp`
- `backend-cpp/tests/test_preflight_report.cpp`
- 必要时同步合同校验测试和前端契约测试

步骤：

1. 增加一致、人工采用复算值、人工接受 Word 值的错误最终分测试。
2. 使用统一 `round2` 比较期望最终分。
3. 返回 `component_score_confirmed_value_mismatch`，包含构件、期望值和当前值。
4. 保持已有合法评分确认路径通过。

## 4. 任务三：`warnings_only` 范围与评分联动

涉及文件：

- `backend-cpp/src/review/DraftValidation.cpp`
- `backend-cpp/include/bridge_report/review/DraftValidation.hpp`
- `backend-cpp/tests/test_draft_validation.cpp`
- `backend-cpp/src/http/ReviewRoutes.cpp`
- `frontend/src/pages/ReviewWorkspacePage.tsx`
- 评分区和页面相关测试

步骤：

1. 增加照片、非构件评分、顶层数据、伪造警告、任意最终分的越界测试。
2. 定义警告病害可编辑字段白名单，并冻结候选 ID、warnings 和来源证据。
3. 从数据库原草稿识别可编辑病害，不信任客户端 warnings。
4. 对警告病害扣分变化涉及的构件评分执行 C++ 重建或严格派生校验。
5. 需要重新人工选择评分时阻止 `warnings_only` 完成正式确认，并提示完整重开。
6. 前端评分区在 `warnings_only` 下只读，仅显示即时复算预览。

## 5. 任务四：编辑租约锁数据库与领域仓储

新增/涉及文件：

- `database/migrations/005_import_record_edit_locks.sql`
- `backend-cpp/include/bridge_report/db/EditLockRepository.hpp`
- `backend-cpp/src/db/EditLockRepository.cpp`
- `backend-cpp/tests/test_edit_lock_repository.cpp`
- `backend-cpp/CMakeLists.txt`
- 迁移检查脚本

步骤：

1. 创建锁表和强制释放审计表，迁移幂等可复跑。
2. 从认证结果提供用户 ID 和会话 ID。
3. 实现原子取得、查询摘要、心跳、正常释放、管理员强制释放。
4. 所有令牌只以 SHA-256 哈希入库。
5. 使用数据库时间判断 30 秒心跳和 2 分钟租约。
6. 增加双连接并发、过期接管、错误令牌、会话删除和审计测试。

## 6. 任务五：锁 API 与写接口保护

新增/涉及文件：

- `backend-cpp/include/bridge_report/http/EditLockRoutes.hpp`
- `backend-cpp/src/http/EditLockRoutes.cpp`
- `backend-cpp/src/http/AuthRoutes.cpp`
- `backend-cpp/src/http/ReviewRoutes.cpp`
- `backend-cpp/src/http/ImportConfirmRoutes.cpp`
- `backend-cpp/src/http/Cors.cpp`
- `backend-cpp/src/main.cpp`
- 路由测试

步骤：

1. 注册取得、心跳、释放和管理员强制释放接口。
2. 增加统一锁令牌读取、锁错误响应和写接口守卫。
3. 保存、预检、确认、取消和放弃重开全部要求活动锁。
4. 重开与取得锁在同一事务内完成；确认、取消和放弃重开与释放锁同事务完成。
5. GET review 和导入记录列表返回非敏感锁摘要。
6. CORS 允许 `X-Edit-Lock-Token`。

## 7. 任务六：前端锁生命周期

新增/涉及文件：

- `frontend/src/api/reviewApi.ts`
- `frontend/src/pages/ReviewWorkspacePage.tsx`
- `frontend/src/review/components/ReviewActionBar.tsx`
- `frontend/src/styles.css`
- 对应测试

步骤：

1. 可编辑记录加载后取得锁，保存明文令牌于页面内存。
2. 每 30 秒心跳；心跳确认失败时切换只读。
3. 所有受保护请求附加锁令牌。
4. 别人持锁时显示姓名和开始时间，保留只读浏览。
5. 正常返回时释放；关闭或刷新时使用 `keepalive` 尽力释放。
6. 未保存离开提示但不自动保存、不建立本地恢复。
7. 管理员提供带原因和二次确认的强制释放入口。

## 8. 验证与收尾

1. 幂等执行迁移 002、003、004、005。
2. 运行 Python 全量测试和真实 Word 定向回归。
3. 运行前端全量测试与生产构建。
4. 构建 C++，使用 PostgreSQL 运行全部 CTest。
5. 复核 `git diff --check` 和受保护目录状态。
6. 对照设计文档逐条记录验收结果；未通过的阻断项不得标记模块 06 完成。
