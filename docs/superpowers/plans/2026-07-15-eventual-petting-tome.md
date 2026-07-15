# 账号体系 + 已确认记录的重开校对与照片查看

## Context

校对工作台在导入记录「已确认」后整页只读（`reviewSession.ts` 按 `import_status` 一刀切），带来三个问题：

1. 「查看照片」按钮也被禁用——查看是只读动作，不应被禁。
2. 三条带 warning 的病害（标度/扣分为"/"）确认入库后无法再修正。
3. 没有任何账号概念，无法区分"普通人只能修正警告病害"与"管理员可以改一切"。

已与用户确认的两个设计决策：

- **修改机制 = 重开校对 + 修订版入库**：不原地改事实表。重开把 `import_status` 从「已确认」翻回「待校对」，编辑后走既有"保存草稿 → 入库前检查 → 确认修订版"流程生成 v+1 版本（`resolve_target_inspection_year_id` 已天然支持：旧年度行降级为「已被修订」，新建 v+1 行）。完全复用现有校验、评分复算、事务与审计链。
- **账号体系 = 轻量登录**：`users` 表预置管理员 + 普通账号（密码哈希入库），登录页 + 会话 token，不做注册/用户管理界面。

权限规则：
- 普通账号：已确认记录上，仅当存在带警告的病害时可「修正警告病害」（重开，scope=warnings_only，只能改这些病害）。
- 管理员：额外有「解锁全部修改」按钮（重开，scope=full，全部可改）。
- 照片查看：任何只读状态下都可用。

## 改动内容

### 0. 数据库迁移 `database/migrations/004_users_and_import_reopen.sql`

- `users`：id、username unique、display_name、password_hash、role check ('admin','normal')、is_active、created_at、updated_at。
- `user_sessions`：id、user_id fk (on delete cascade)、token_hash unique、expires_at、created_at。
- `import_records` 加重开审计列：`reopened_at timestamptz`、`reopened_by_username text`、`reopen_scope text check in ('warnings_only','full')`。
- 默认账号由后端启动时播种（见下），不在 SQL 里写死哈希。

### 1. 后端 C++ — 认证底座

- 新增 `src/auth/PasswordHash.{hpp,cpp}`：PBKDF2-HMAC-SHA256（OpenSSL `PKCS5_PBKDF2_HMAC`，依赖已存在），格式 `pbkdf2_sha256$iter$salt_hex$hash_hex`，`hash_password` / `verify_password` 纯函数。
- 新增 `src/db/AuthRepository.{hpp,cpp}`：`find_user_by_username`、`create_session`（随机 32 字节 token，库里只存 SHA-256 哈希，12 小时过期）、`find_session_user`、`delete_session`、`seed_default_users`（users 表为空时预置 `admin`/`user`，默认密码 `admin123`/`user123`，日志提示尽快修改）。
- 新增 `src/http/AuthRoutes.{hpp,cpp}`：`POST /api/auth/login`（返回 token + 用户信息）、`POST /api/auth/logout`、`GET /api/auth/me`；沿用现有 OPTIONS/CORS 惯用法。
- `RouteHelpers` 增加 `authenticate(req) -> std::optional<AuthUser>`（解析 `Authorization: Bearer`）与 `respond_unauthorized/forbidden`；写类端点（保存草稿、preflight、confirm、cancel、Word 导入、线索创建/绑定/重绑、新增的 reopen/restore）在处理器开头要求登录。
- [Cors.cpp](backend-cpp/src/http/Cors.cpp)：`Access-Control-Allow-Headers` 加 `Authorization`。
- [main.cpp](backend-cpp/src/main.cpp)：注册 auth 路由 + 启动时 `seed_default_users`。

### 2. 后端 C++ — 重开校对

- `ReviewRepository` 新增：
  - `reopen_import_record(id, scope, username)`：`UPDATE ... SET import_status='待校对', reopened_at=now(), ... WHERE id=$1 AND import_status='已确认'`（谓词内保证并发安全，同 `cancel_import_record` 惯用法）。
  - `restore_reopened_import_record(id)`：翻回「已确认」并清空重开列（仅当 `reopened_at is not null and import_status='待校对'`）。
  - `cancel_import_record` 加谓词 `reopened_at is null`；路由层对已重开记录返回明确错误"请使用「放弃修改」"。
  - `confirm_annual_facts` 收尾更新 import_records 时顺带清空重开列。
- 新路由（挂 [ReviewRoutes.cpp](backend-cpp/src/http/ReviewRoutes.cpp)）：
  - `POST /api/import-records/{id}/reopen` body `{scope}`：要求登录；`full` 要求 admin；仅接受 `contract_compatibility == native_1_2` 的已确认记录；`warnings_only` 还要求草稿中存在 `warnings` 非空的病害候选（新纯函数，放 `review/` 下便于测试）。
  - `POST /api/import-records/{id}/reopen-restore`：放弃修改，恢复已确认。
- 保存草稿范围校验（[DraftValidation](backend-cpp/src/review/DraftValidation.cpp) 新增纯函数 + [ReviewRoutes.cpp](backend-cpp/src/http/ReviewRoutes.cpp) 保存处理器接线）：记录处于 warnings_only 重开态时，与库中已存草稿逐条对齐 `candidate_id`，`warnings` 为空的病害候选必须 Json 深度相等，且不允许增删病害候选，违规返回 400 `reopen_scope_violation`；full 重开态的保存要求 admin 角色。
- GET review 响应（`ReviewModels::build_review_response`）加 `reopen` 字段（reopened_at / reopen_scope / reopened_by_username）。

### 3. 前端 — 认证

- 新增 `src/auth/authApi.ts`、`src/auth/AuthContext.tsx`（token 存 localStorage，启动时 `GET /api/auth/me` 恢复会话）、`src/pages/LoginPage.tsx`。
- [apiClient.ts](frontend/src/api/apiClient.ts)：模块级 token provider，`request()` 自动附 `Authorization` 头；401 时抛 `ApiError(code="auth_required")`，AuthContext 捕获后清 token 回登录页。
- [App.tsx](frontend/src/App.tsx)：`AuthProvider` 包裹；未登录渲染登录页；顶栏显示"显示名（角色）+ 退出登录"。

### 4. 前端 — 校对页行为

- **照片查看永不禁用**：[DefectsSection.tsx](frontend/src/review/components/DefectsSection.tsx) 去掉 `<fieldset disabled>` 一揽子禁用；[DefectPhotoGroup.tsx](frontend/src/review/components/DefectPhotoGroup.tsx) 与 [UnlinkedPhotosPanel.tsx](frontend/src/review/components/UnlinkedPhotosPanel.tsx) 改为逐控件 `disabled={disabled}`（字段输入、照片操作、确认本组、重新关联、缺图确认），仅"查看照片"toggle、缩略图切换、大图展示不禁用；[styles.css](frontend/src/styles.css) 的 `.controls-disabled` pointer-events 全禁样式改为基于 `:disabled` 的视觉态。
- **逐病害可编辑**：DefectsSection 接受 `isDefectEditable(defect)` 回调；重开 warnings_only 态下仅 `defect.warnings.length > 0` 的病害可编辑，full/正常待校对态全部可编辑。
- [reviewSession.ts](frontend/src/review/reviewSession.ts)：`deriveReviewSession` 增加重开态横幅（"已重开校对（仅警告病害可改 / 全部可改），完成后需确认修订版入库"）。
- [ReviewWorkspacePage.tsx](frontend/src/pages/ReviewWorkspacePage.tsx)：
  - 已确认只读态：按角色/警告情况显示「修正警告病害」（任何登录用户）与「解锁全部修改」（仅 admin），调用 reopen 后切回可编辑态。
  - 重开态：「取消导入」替换为「放弃修改」（调 reopen-restore）；warnings_only 态隐藏「批量确认普通候选」；其余保存/检查/确认按钮照旧，确认时走既有修订版弹窗。
- [reviewApi.ts](frontend/src/api/reviewApi.ts)：`ReviewResponse` 加 reopen 字段；新增 `reopenImport` / `restoreReopenedImport`。

### 5. 测试与文档

- 后端 gtest（对齐 `backend-cpp/tests/` 既有模式）：`test_password_hash`、auth 会话仓储、reopen/restore 状态谓词、warnings_only 范围校验纯函数、「存在警告病害」判定纯函数。
- 前端 vitest（对齐现有 `*.test.tsx`）：reviewSession 新态、DefectPhotoGroup 只读下照片可看/编辑禁用、DefectsSection 逐病害禁用、AuthContext/LoginPage、ReviewWorkspacePage 按钮显隐。
- 文档：新增 `docs/superpowers/specs/changes/2026-07-15-change-002-accounts-and-post-confirm-reopen.md` 简要设计记录；PROJECT_CONTEXT.md 补进度；默认账号密码与改密 SQL 写入 README 或 docs。

## 验证

1. `psql` 执行迁移 004；CMake 构建后端并跑 gtest；`npm test` 跑前端。
2. 启动 PostgreSQL + C++ 后端 + Vite（用 preview 浏览器）走端到端：
   - 未登录访问被引导到登录页；`user` 登录后打开已确认记录：照片可查看、字段灰；存在警告病害时可「修正警告病害」，仅这 3 条可编辑；改非警告病害的保存请求被后端 400 拒绝（可用 devtools 验证）。
   - 修正后：保存草稿 → 入库前检查 → 确认修订版 → 版本号 +1，档案页评分一致，重开列被清空。
   - `admin` 登录：「解锁全部修改」可改任意病害并确认修订版；「放弃修改」能恢复已确认且草稿未变。
   - 取消导入对重开记录被拒绝并提示使用「放弃修改」。
