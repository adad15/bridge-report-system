# 变更提案 002：轻量账号体系与已确认记录的重开校对

> 日期：2026-07-15
>
> 状态：已实施
>
> 影响模块：05（校对工作台）、02（数据库新增 users / user_sessions 与 import_records 重开列）
>
> 采用方案：重开校对 + 修订版入库（不原地修改事实表）；轻量登录（预置账号，无注册/用户管理界面）

## 1. 变更背景

模块 05 在导入记录「已确认」后整页只读，暴露出三个问题：

1. 「查看照片」按钮也被禁用——查看是只读动作，不应随编辑一起被禁。
2. 带警告的病害（如标度/扣分为"/"经人工确认入库的）确认后无法再修正。
3. 系统没有账号概念，无法区分"普通人只能修正警告病害"与"管理员可以修改一切"。

## 2. 账号体系（轻量登录）

- 新表 `users`（username 唯一、password_hash、role∈{admin,normal}、is_active）与
  `user_sessions`（token_hash 唯一、expires_at，12 小时会话；库中只存令牌 SHA-256）。
- 口令哈希：PBKDF2-HMAC-SHA256（OpenSSL），格式 `pbkdf2_sha256$iter$salt_hex$hash_hex`。
- 端点：`POST /api/auth/login`、`POST /api/auth/logout`、`GET /api/auth/me`。
- 默认账号由 C++ 后端启动播种（users 表为空才插入）：
  - `admin` / `admin123`（管理员）
  - `user` / `user123`（普通用户）
  - **请尽快改密**，改密 SQL（在 psql 中，新哈希可临时用后端播种再复制，或使用任意 PBKDF2 工具生成同格式哈希）：
    `update users set password_hash = '<新哈希>', updated_at = now() where username = 'admin';`
- 所有写类端点（保存草稿、入库前检查、确认、取消、Word 解析、线索创建/绑定、重开/放弃）
  要求 `Authorization: Bearer <token>`；读端点与照片内容保持开放（本地单机部署）。
- 前端：登录页 + AuthContext（localStorage 持有令牌，启动时 `GET /api/auth/me` 恢复）；
  apiClient 自动附加 Authorization，携带令牌的请求收到 401 时全局登出。

## 3. 重开校对 + 修订版入库

不引入任何"原地改事实表"的路径；确认事务仍是唯一事实写入入口。

- `POST /api/import-records/{id}/reopen`（body `{scope}`）：
  - 仅接受「已确认 + 原生 1.2」记录；旧版终态（legacy_read_only）拒绝。
  - `scope=warnings_only`：任何登录用户；要求草稿存在 `warnings` 非空的病害候选。
  - `scope=full`：仅管理员。
  - 效果：`import_status` 翻回「待校对」，记录 `reopened_at / reopened_by_username / reopen_scope`，
    并把当前草稿快照进 `reopen_backup_parsed_result_json`。
- 重开态下走既有"保存草稿 → 入库前检查 → 确认修订版"流程；确认生成年度 v+1
  （旧年度行标记「已被修订」），确认事务顺带清空重开列。
- `POST /api/import-records/{id}/reopen-restore`（放弃修改）：草稿还原为重开快照，
  状态翻回「已确认」，清空重开列；正式事实从未被触碰。
- 重开态的「取消导入」被拒绝（`import_record_reopened`），防止"事实存在但来源记录已取消"的悬空。
- 保存草稿的后端范围兜底（`validate_warnings_only_scope` 纯函数）：
  - warnings_only：非警告病害候选必须逐字段不变（数值语义比较，容忍 1.0↔1 往返差异），
    且不允许增删病害候选；违规 400 `reopen_scope_violation`。
  - "是否带警告"只看库中存量草稿，客户端伪造 warnings 数组无法解锁。
  - full 重开的草稿保存要求管理员角色。

## 4. 前端校对页行为

- 病害卡片改为逐控件禁用（不再用 fieldset 一揽子禁用）：任何只读态下
  「查看照片」toggle、缩略图切换、大图展示均可用；编辑控件按状态禁用。
- 已确认只读横幅处新增入口：「修正警告病害」（存在警告病害时，所有登录用户）、
  「解锁全部修改」（仅管理员）。
- 重开态：黄色横幅提示范围与后续流程；warnings_only 下仅带警告的病害可编辑，
  其余病害卡片锁定；「取消导入」替换为「放弃修改」；「批量确认普通评分」隐藏
  （其批量改动超出"修正警告病害"的语义）。
- GET review 响应新增 `reopen` 字段（null 或 {reopened_at, reopened_by_username, scope}）。

## 5. 迁移与兼容

- 迁移 `database/migrations/004_users_and_import_reopen.sql`，幂等可复跑。
- 旧记录三列全空（未重开），行为与之前完全一致。
- 1.0/1.1 旧版终态记录不可重开，保持"历史数据只读，不做猜测性回填"的原则。
