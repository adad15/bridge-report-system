# 规范原文库（规范管理改版）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标：** 把「规范管理」改成规范原文库：按规范浏览、切换版本、用浏览器自带阅读器看 PDF 原文；管理员可新增规范、上传新版本、替换原文、编辑和删除；原来的规则包启停改名「规则管理」留在桥梁档案页。

**架构：** 后端新增两张表（`standard_documents`、`standard_document_versions`），原文 PDF 复用 `archived_files` 与归档目录；状态、废止日期、替代关系、默认版本都由纯函数按日期推导，不存库。前端新增 `/standards` 页面，原文阅读区是一个指向不鉴权内容接口的 iframe。

**技术栈：** C++20 + drogon + jsoncpp + PostgreSQL 18（GoogleTest）；React 18 + antd 6.6 + react-router 6（Vitest + Testing Library）。

**设计：** [docs/superpowers/specs/2026-10-09-standard-documents-library-design.md](../specs/2026-10-09-standard-documents-library-design.md)，执行者须同时阅读。

## 全局约束

- 回复、注释、提交信息用中文；代码标识符用英文。
- **共享工作区。** 以下文件带有他人未提交的改动，只许增加本计划写明的行，不得回退、重排或格式化其他内容：`backend-cpp/CMakeLists.txt`、`backend-cpp/src/main.cpp`、`frontend/src/App.tsx`、`frontend/src/pages/BridgesPage.tsx`、`docs/ui-design-system.md`。`frontend/scripts/check-loading-browser.mjs` 尚未纳入版本库，只改计划写明的两行，不提交。
- **提交。** 开始执行前先问用户是否按任务提交。每次提交前 `git status` 核对，只 `git add --` 本任务新建或独占的文件；上一条所列共享文件的改动统一在任务 12 用外科式暂存提交。提交信息先用 Write 工具写进临时文件，再 `git commit -F <文件>`（Git Bash 的 heredoc 会把中文转乱），结尾加一行 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。
- **前端样式。** 只用 antd 组件及其属性，不新增 CSS 规则和 className；尺寸可写 `style`/`styles`，颜色、字号取 `theme.useToken()`。`tsconfig` 开了 `noUnusedLocals`，删掉不用的导入。两个汉字的按钮会被 antd 插空格，测试里写 `/^取\s?消$/`。
- **后端。** 迁移必须幂等，因为检查脚本会连跑两遍。每个写接口的路径都要 `register_options_handler`，否则路由预检审计会报错。归档路径只用 ASCII。Windows 上删文件前要先关掉读它的句柄。
- **构建与测试命令**（PowerShell，在仓库根目录执行）：
  - 构建前确认 `Get-Process bridge-report-backend -ErrorAction SilentlyContinue` 为空。进程还在会报 LNK1168，停用户的进程前先问。
  - 构建测试：`cmake --build backend-cpp/build/vs-debug --config Debug --target bridge_report_backend_tests`
  - 只跑本功能的纯函数测试：`backend-cpp/build/vs-debug/Debug/bridge_report_backend_tests.exe --gtest_filter=StandardDocument*`。未设数据库变量时，仓储测试会自动跳过。
  - 全量测试（含数据库，自建并自删隔离 schema）：`powershell -ExecutionPolicy Bypass -File scripts/dev/check-backend-tests.ps1`
  - 前端单测：在 `frontend` 目录下执行 `npx vitest run <文件>`。类型检查：`npx tsc -b`。

## 文件结构

| 文件 | 新建 / 修改 | 职责 |
| --- | --- | --- |
| `database/migrations/038_standard_documents.sql` | 新建 | 两张表与约束 |
| `database/tests/038_standard_documents_smoke.sql` | 新建 | 约束冒烟 |
| `backend-cpp/include/bridge_report/standards/StandardDocumentModels.hpp`、`backend-cpp/src/standards/StandardDocumentModels.cpp` | 新建 | 记录、输入结构；状态与替代关系推导；输入校验；JSON；下载文件名 |
| `backend-cpp/include/bridge_report/archive/StandardDocumentArchive.hpp`、`backend-cpp/src/archive/StandardDocumentArchive.cpp` | 新建 | PDF 识别、ASCII 路径、写盘删盘 |
| `backend-cpp/include/bridge_report/db/StandardDocumentRepository.hpp`、`backend-cpp/src/db/StandardDocumentRepository.cpp` | 新建 | 查询与事务写入，交回待删文件 |
| `backend-cpp/include/bridge_report/http/StandardDocumentRoutes.hpp`、`backend-cpp/src/http/StandardDocumentRoutes.cpp` | 新建 | 鉴权、multipart、各接口、原文下发 |
| `backend-cpp/include/bridge_report/http/RouteHelpers.hpp` | 修改 | 增加 `content_disposition` |
| `backend-cpp/src/main.cpp`、`backend-cpp/CMakeLists.txt` | 修改（共享） | 注册路由；源文件与测试 |
| `backend-cpp/tests/test_standard_document_{models,archive,repository,routes}.cpp` | 新建 | 后端测试 |
| `frontend/src/api/standardDocumentsApi.ts`、`.test.ts` | 新建 | 类型、接口、原文地址、错误文案 |
| `frontend/src/test/standardDocumentFixtures.ts` | 新建 | 前端测试共用的示例数据 |
| `frontend/src/standard-documents/versionStatus.tsx` | 新建 | 状态文案、颜色与日期短句 |
| `frontend/src/standard-documents/StandardDocumentList.tsx` | 新建 | 左栏目录 |
| `frontend/src/standard-documents/StandardDocumentHeader.tsx` | 新建 | 头部、版本下拉、非现行提示、「更多」菜单 |
| `frontend/src/standard-documents/StandardPdfReader.tsx` | 新建 | iframe 阅读区 |
| `frontend/src/standard-documents/StandardVersionTable.tsx` | 新建 | 版本记录 |
| `frontend/src/standard-documents/StandardBasicInfo.tsx` | 新建 | 基本信息 |
| `frontend/src/standard-documents/StandardVersionFields.tsx` | 新建 | 版本字段、PDF 选择项、表单值转换 |
| `frontend/src/standard-documents/StandardDocumentFormModal.tsx` | 新建 | 新增规范、编辑规范信息 |
| `frontend/src/standard-documents/StandardVersionFormModal.tsx` | 新建 | 上传新版本、编辑版本、替换原文 |
| `frontend/src/standard-documents/DeleteStandardDocumentModal.tsx` | 新建 | 删除整部规范的确认 |
| `frontend/src/pages/StandardDocumentsPage.tsx`、`.test.tsx` | 新建 | 页面、地址栏状态、弹窗编排 |
| `frontend/src/App.tsx` | 修改（共享） | `/standards` 路由 |
| `frontend/src/layouts/AppLayout.tsx`、`.test.tsx` | 修改 | 菜单移到业务中心 |
| `frontend/src/pages/BridgesPage.tsx`（共享）、`.test.tsx` | 修改 | 按钮改名、深链 `?rules=1` |
| `frontend/src/standards/StandardsAdminPanel.tsx`、`.test.tsx` | 修改 | 弹窗改名 |
| `frontend/src/pages/RatingTreePage.tsx` | 修改 | 按钮改名与深链 |
| `frontend/scripts/check-loading-browser.mjs` | 修改（不提交） | 改名 |
| `docs/ui-design-system.md` | 修改（共享） | 页面分类 |

---

### 任务 1：迁移 038 与冒烟测试

**文件：**
- 新建：`database/migrations/038_standard_documents.sql`
- 新建：`database/tests/038_standard_documents_smoke.sql`

**接口：**
- 产出：表 `standard_documents(id, name, english_name, category, created_at, updated_at)`、`standard_document_versions(id, document_id, label, code, kind, nature, issuer, editor, publish_date, effective_date, withdrawn_date, announcement, source, note, archived_file_id, uploaded_by, created_at, updated_at)`。

- [ ] **步骤 1：写冒烟测试**

`database/tests/038_standard_documents_smoke.sql`：

```sql
-- 038: 规范原文库的约束。全部在事务里做，最后回滚，不留数据。
begin;
do $$
declare
  d uuid;
  f1 uuid;
  f2 uuid;
  f3 uuid;
begin
  insert into standard_documents(name, category) values('038 冒烟规范', '冒烟') returning id into d;
  insert into archived_files(original_file_name, current_file_name, storage_relative_path, file_type, file_purpose, file_extension)
  values('a.pdf', 'a.pdf', 'standards/smoke038/a.pdf', '附件', '规范原文', '.pdf') returning id into f1;
  insert into archived_files(original_file_name, current_file_name, storage_relative_path, file_type, file_purpose, file_extension)
  values('b.pdf', 'b.pdf', 'standards/smoke038/b.pdf', '附件', '规范原文', '.pdf') returning id into f2;
  insert into archived_files(original_file_name, current_file_name, storage_relative_path, file_type, file_purpose, file_extension)
  values('c.pdf', 'c.pdf', 'standards/smoke038/c.pdf', '附件', '规范原文', '.pdf') returning id into f3;

  insert into standard_document_versions(document_id, label, code, kind, effective_date, archived_file_id)
  values(d, '2011 版', 'JTG/T H21—2011', 'official', '2011-09-01', f1);

  begin
    insert into standard_document_versions(document_id, label, code, kind, archived_file_id)
    values(d, '无实施日期', 'X', 'official', f2);
    raise exception '038: official version without effective date accepted';
  exception when check_violation then null;
  end;

  begin
    insert into standard_document_versions(document_id, label, code, kind, effective_date, archived_file_id)
    values(d, '带实施日期的稿件', 'X', 'draft_comment', '2017-01-01', f2);
    raise exception '038: draft with effective date accepted';
  exception when check_violation then null;
  end;

  begin
    insert into standard_document_versions(document_id, label, code, kind, effective_date, withdrawn_date, archived_file_id)
    values(d, '倒挂', 'X', 'official', '2020-01-01', '2019-01-01', f2);
    raise exception '038: withdrawn before effective accepted';
  exception when check_violation then null;
  end;

  begin
    insert into standard_document_versions(document_id, label, code, kind, effective_date, archived_file_id)
    values(d, '同日另一版', 'X', 'official', '2011-09-01', f2);
    raise exception '038: duplicate official effective date accepted';
  exception when unique_violation then null;
  end;

  begin
    insert into standard_document_versions(document_id, label, code, kind, archived_file_id)
    values(d, '2011 版', 'X', 'draft_comment', f2);
    raise exception '038: duplicate label accepted';
  exception when unique_violation then null;
  end;

  begin
    insert into standard_document_versions(document_id, label, code, kind, archived_file_id)
    values(d, '共用文件', 'X', 'draft_comment', f1);
    raise exception '038: two versions shared one archived file';
  exception when unique_violation then null;
  end;

  -- 稿件不受实施日期唯一约束影响，两份稿件可以并存。
  insert into standard_document_versions(document_id, label, code, kind, publish_date, archived_file_id)
  values(d, '2017 征求意见稿', 'X', 'draft_comment', '2017-06-01', f2);
  insert into standard_document_versions(document_id, label, code, kind, archived_file_id)
  values(d, '2026 报批稿', 'Y', 'draft_approval', f3);

  begin
    delete from archived_files where id = f1;
    raise exception '038: archived file deleted while a version still uses it';
  -- on delete restrict 抛的是 restrict_violation（23001），不是 foreign_key_violation。
  exception when restrict_violation then null;
  end;

  delete from standard_documents where id = d;
  if exists(select 1 from standard_document_versions where document_id = d) then
    raise exception '038: versions survived their document';
  end if;

  begin
    insert into standard_documents(name, category) values('  ', '冒烟');
    raise exception '038: blank name accepted';
  exception when check_violation then null;
  end;
end $$;
rollback;
```

- [ ] **步骤 2：在隔离 schema 里跑全部迁移加这份冒烟，确认失败**

PowerShell：

```powershell
$psql = "D:\PostgreSQL\18\bin\psql.exe"
$url = "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
& $psql $url -v ON_ERROR_STOP=1 -c "drop schema if exists plan038_check cascade; create schema plan038_check;"
$env:PGOPTIONS = "-c search_path=plan038_check"
try {
  foreach ($pass in 1..2) {
    foreach ($file in Get-ChildItem database/migrations/*.sql | Sort-Object Name) {
      & $psql $url -q -v ON_ERROR_STOP=1 -f $file.FullName | Out-Null
      if ($LASTEXITCODE -ne 0) { throw "迁移失败：$($file.Name)（第 $pass 遍）" }
    }
  }
  & $psql $url -v ON_ERROR_STOP=1 -f database/tests/038_standard_documents_smoke.sql
  if ($LASTEXITCODE -ne 0) { throw "038 冒烟失败" }
} finally {
  Remove-Item Env:PGOPTIONS
  & $psql $url -v ON_ERROR_STOP=1 -c "drop schema if exists plan038_check cascade;"
}
```

预期：冒烟失败，报 `relation "standard_documents" does not exist`。

- [ ] **步骤 3：写迁移**

`database/migrations/038_standard_documents.sql`：

```sql
-- 038: 规范原文库。
--
-- 「规范管理」页从规则包启停改成查阅规范原文（PDF）。一部规范可以有多个版本：正式版、
-- 征求意见稿、报批稿。状态（现行 / 即将实施 / 已废止）不存库，按日期推导；被后续版本
-- 替代的废止日期也是推导出来的，这里只存手工填写的那种。
-- 见 docs/superpowers/specs/2026-10-09-standard-documents-library-design.md §6。
--
-- 原文 PDF 走已有的 archived_files（类型「附件」、用途「规范原文」），这里只记引用。

create table if not exists standard_documents (
  id uuid primary key default gen_random_uuid(),
  name text not null,
  english_name text,
  category text not null,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  constraint standard_documents_name_unique unique (name),
  constraint standard_documents_name_present check (btrim(name) <> ''),
  constraint standard_documents_category_present check (btrim(category) <> '')
);

create table if not exists standard_document_versions (
  id uuid primary key default gen_random_uuid(),
  document_id uuid not null references standard_documents(id) on delete cascade,
  label text not null,
  code text not null,
  kind text not null check (kind in ('official', 'draft_comment', 'draft_approval')),
  nature text,
  issuer text,
  editor text,
  publish_date date,
  effective_date date,
  withdrawn_date date,
  announcement text,
  source text,
  note text,
  -- restrict：先删版本、再删归档行；反过来版本会指向一个不存在的文件。
  archived_file_id uuid not null references archived_files(id) on delete restrict,
  uploaded_by uuid references users(id) on delete set null,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  constraint standard_document_versions_label_unique unique (document_id, label),
  constraint standard_document_versions_file_unique unique (archived_file_id),
  constraint standard_document_versions_label_present check (btrim(label) <> ''),
  constraint standard_document_versions_code_present check (btrim(code) <> ''),
  constraint standard_document_versions_kind_dates check (
    (kind = 'official' and effective_date is not null)
    or (kind <> 'official' and effective_date is null and withdrawn_date is null)
  ),
  constraint standard_document_versions_withdrawn_after_effective check (
    withdrawn_date is null or withdrawn_date >= effective_date
  )
);

-- 同一规范的正式版按实施日期排成一条替代链，实施日期必须各不相同。
create unique index if not exists standard_document_versions_official_effective_unique
  on standard_document_versions (document_id, effective_date)
  where kind = 'official';

create index if not exists standard_document_versions_document_idx
  on standard_document_versions (document_id);

comment on table standard_documents is
  '规范原文库里的一部规范，如《公路桥梁技术状况评定标准》。与规则包（standard_packages）无关。';
comment on table standard_document_versions is
  '一部规范的一个版本及其原文 PDF。状态按日期推导，不存库。';
comment on column standard_document_versions.withdrawn_date is
  '手工填写的废止日期。被后续正式版替代的废止日期按推导得出，不写回。';
```

- [ ] **步骤 4：再跑步骤 2 的命令，确认通过**

预期：全部迁移连跑两遍无错，冒烟输出 `DO` 和 `ROLLBACK`，最后删掉 schema。

- [ ] **步骤 5：提交**

```bash
git add -- database/migrations/038_standard_documents.sql database/tests/038_standard_documents_smoke.sql
```

提交信息：`feat(db): 038 规范原文库的两张表`

---

### 任务 2：规范原文领域模型（纯函数）

**文件：**
- 新建：`backend-cpp/include/bridge_report/standards/StandardDocumentModels.hpp`
- 新建：`backend-cpp/src/standards/StandardDocumentModels.cpp`
- 新建：`backend-cpp/tests/test_standard_document_models.cpp`
- 修改（共享）：`backend-cpp/CMakeLists.txt`

**接口：**
- 产出（命名空间 `bridge_report::standards`）：
  - `enum class StandardVersionKind { official, draft_comment, draft_approval }`
  - `enum class StandardVersionStatus { current, upcoming, withdrawn, draft_comment, draft_approval }`
  - `std::optional<StandardVersionKind> parse_standard_version_kind(const std::string&)`
  - `const char* standard_version_kind_code(StandardVersionKind)`
  - `const char* standard_version_status_code(StandardVersionStatus)`
  - `struct StandardVersionRecord`、`struct StandardDocumentRecord`、`struct DerivedVersionState`（字段见代码）
  - `std::string standard_version_date(const StandardVersionRecord&)`
  - `std::vector<DerivedVersionState> derive_version_states(const std::vector<StandardVersionRecord>&, const std::string& today)`
  - `std::optional<std::string> default_standard_version_id(const std::vector<StandardVersionRecord>&, const std::vector<DerivedVersionState>&)`
  - `Json::Value standard_document_json(const StandardDocumentRecord&, const std::string& today)`
  - `std::string local_today()`、`bool is_valid_iso_date(const std::string&)`
  - `struct StandardDocumentInput`、`struct StandardVersionInput`、`struct StandardInputIssue { std::string path; std::string message; }`
  - `std::vector<StandardInputIssue> parse_standard_document_input(const Json::Value&, StandardDocumentInput&, const std::string& prefix = "")`
  - `std::vector<StandardInputIssue> parse_standard_version_input(const Json::Value&, StandardVersionInput&, const std::string& prefix = "")`
  - `Json::Value standard_input_issues_json(const std::vector<StandardInputIssue>&)`
  - `std::string standard_pdf_file_name(const std::string& code, const std::string& document_name)`

- [ ] **步骤 1：写失败的测试**

`backend-cpp/tests/test_standard_document_models.cpp`：

```cpp
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "bridge_report/standards/StandardDocumentModels.hpp"

namespace {

using namespace bridge_report::standards;

StandardVersionRecord official(const std::string& id, const std::string& effective,
                               std::optional<std::string> withdrawn = std::nullopt) {
    StandardVersionRecord version;
    version.id = id;
    version.label = id;
    version.code = "CODE-" + id;
    version.kind = StandardVersionKind::official;
    version.effective_date = effective;
    version.withdrawn_date = std::move(withdrawn);
    version.created_date = "2026-10-09";
    return version;
}

StandardVersionRecord draft(const std::string& id, StandardVersionKind kind, const std::string& publish) {
    StandardVersionRecord version;
    version.id = id;
    version.label = id;
    version.code = "CODE-" + id;
    version.kind = kind;
    version.publish_date = publish;
    version.created_date = "2026-10-09";
    return version;
}

TEST(StandardDocumentModelsTest, OfficialStatusFollowsTheEffectiveDate) {
    const std::vector<StandardVersionRecord> versions{official("v", "2011-09-01")};
    EXPECT_EQ(derive_version_states(versions, "2011-08-31")[0].status, StandardVersionStatus::upcoming);
    EXPECT_EQ(derive_version_states(versions, "2011-09-01")[0].status, StandardVersionStatus::current);
}

TEST(StandardDocumentModelsTest, LaterOfficialVersionReplacesTheEarlierOne) {
    const std::vector<StandardVersionRecord> versions{
        official("new", "2021-11-01"), official("old", "2004-10-01")};
    const auto states = derive_version_states(versions, "2026-10-09");

    EXPECT_EQ(states[0].status, StandardVersionStatus::current);
    EXPECT_EQ(states[0].replaces_version_id, std::optional<std::string>("old"));
    EXPECT_FALSE(states[0].withdrawn_date.has_value());

    EXPECT_EQ(states[1].status, StandardVersionStatus::withdrawn);
    EXPECT_EQ(states[1].withdrawn_date, std::optional<std::string>("2021-11-01"));
    EXPECT_EQ(states[1].withdrawn_date_source, std::optional<std::string>("replacement"));
    EXPECT_EQ(states[1].replaced_by_version_id, std::optional<std::string>("new"));
}

TEST(StandardDocumentModelsTest, TheWithdrawalDayItselfCountsAsWithdrawn) {
    const std::vector<StandardVersionRecord> versions{
        official("old", "2004-10-01"), official("new", "2021-11-01")};
    EXPECT_EQ(derive_version_states(versions, "2021-10-31")[0].status, StandardVersionStatus::current);
    EXPECT_EQ(derive_version_states(versions, "2021-11-01")[0].status, StandardVersionStatus::withdrawn);
}

TEST(StandardDocumentModelsTest, RemovingTheNewerVersionRestoresTheOlderOne) {
    const std::vector<StandardVersionRecord> versions{official("old", "2004-10-01")};
    const auto states = derive_version_states(versions, "2026-10-09");
    EXPECT_EQ(states[0].status, StandardVersionStatus::current);
    EXPECT_FALSE(states[0].withdrawn_date.has_value());
}

TEST(StandardDocumentModelsTest, AnUpcomingVersionLeavesTheCurrentOneInForce) {
    const std::vector<StandardVersionRecord> versions{
        official("old", "2011-09-01"), official("new", "2027-03-01")};
    const auto states = derive_version_states(versions, "2026-10-09");
    EXPECT_EQ(states[0].status, StandardVersionStatus::current);
    EXPECT_EQ(states[0].withdrawn_date, std::optional<std::string>("2027-03-01"));
    EXPECT_EQ(states[1].status, StandardVersionStatus::upcoming);
}

TEST(StandardDocumentModelsTest, AManualWithdrawalDateWinsOverTheReplacement) {
    const std::vector<StandardVersionRecord> versions{
        official("old", "2004-10-01", "2020-01-01"), official("new", "2021-11-01")};
    const auto states = derive_version_states(versions, "2026-10-09");
    EXPECT_EQ(states[0].withdrawn_date, std::optional<std::string>("2020-01-01"));
    EXPECT_EQ(states[0].withdrawn_date_source, std::optional<std::string>("manual"));
}

TEST(StandardDocumentModelsTest, DraftsKeepTheirKindAndStayOutOfTheChain) {
    const std::vector<StandardVersionRecord> versions{
        draft("approval", StandardVersionKind::draft_approval, "2026-02-11"),
        draft("comment", StandardVersionKind::draft_comment, "2017-06-01"),
        official("2011", "2011-09-01")};
    const auto states = derive_version_states(versions, "2026-10-09");
    EXPECT_EQ(states[0].status, StandardVersionStatus::draft_approval);
    EXPECT_EQ(states[1].status, StandardVersionStatus::draft_comment);
    EXPECT_EQ(states[2].status, StandardVersionStatus::current);
    EXPECT_FALSE(states[2].replaced_by_version_id.has_value());
}

TEST(StandardDocumentModelsTest, DefaultVersionPrefersCurrentThenLatest) {
    const std::vector<StandardVersionRecord> with_current{
        draft("approval", StandardVersionKind::draft_approval, "2026-02-11"), official("2011", "2011-09-01")};
    EXPECT_EQ(default_standard_version_id(with_current, derive_version_states(with_current, "2026-10-09")),
              std::optional<std::string>("2011"));

    const std::vector<StandardVersionRecord> drafts_only{
        draft("comment", StandardVersionKind::draft_comment, "2017-06-01"),
        draft("approval", StandardVersionKind::draft_approval, "2026-02-11")};
    EXPECT_EQ(default_standard_version_id(drafts_only, derive_version_states(drafts_only, "2026-10-09")),
              std::optional<std::string>("approval"));

    EXPECT_FALSE(default_standard_version_id({}, {}).has_value());
}

TEST(StandardDocumentModelsTest, DocumentJsonCarriesTheDerivedFields) {
    StandardDocumentRecord document;
    document.id = "doc";
    document.name = "公路桥涵养护规范";
    document.category = "养护与检查";
    document.versions = {official("new", "2021-11-01"), official("old", "2004-10-01")};
    document.versions[0].original_file_name = "5120.pdf";
    document.versions[0].file_size_bytes = 1234;

    const auto json = standard_document_json(document, "2026-10-09");
    EXPECT_EQ(json["default_version_id"].asString(), "new");
    EXPECT_TRUE(json["english_name"].isNull());
    EXPECT_EQ(json["versions"][0]["status"].asString(), "current");
    EXPECT_EQ(json["versions"][0]["content_path"].asString(), "/api/standard-document-versions/new/content");
    EXPECT_EQ(json["versions"][0]["file"]["size_bytes"].asUInt64(), 1234u);
    EXPECT_EQ(json["versions"][1]["status"].asString(), "withdrawn");
    EXPECT_EQ(json["versions"][1]["withdrawn_date_source"].asString(), "replacement");
    EXPECT_EQ(json["versions"][1]["replaced_by_version_id"].asString(), "new");
}

TEST(StandardDocumentModelsTest, ValidatesIsoDates) {
    EXPECT_TRUE(is_valid_iso_date("2024-02-29"));
    EXPECT_FALSE(is_valid_iso_date("2023-02-29"));
    EXPECT_FALSE(is_valid_iso_date("2011-13-01"));
    EXPECT_FALSE(is_valid_iso_date("2011/09/01"));
}

TEST(StandardDocumentModelsTest, ParsesAValidOfficialVersion) {
    Json::Value body;
    body["label"] = " 2011 版 ";
    body["code"] = "JTG/T H21—2011";
    body["kind"] = "official";
    body["publish_date"] = "2011-07-19";
    body["effective_date"] = "2011-09-01";
    body["note"] = "";
    StandardVersionInput input;
    EXPECT_TRUE(parse_standard_version_input(body, input).empty());
    EXPECT_EQ(input.label, "2011 版");
    EXPECT_EQ(input.effective_date, std::optional<std::string>("2011-09-01"));
    EXPECT_FALSE(input.note.has_value());
}

TEST(StandardDocumentModelsTest, ReportsEveryProblemWithItsPath) {
    Json::Value body;
    body["label"] = "";
    body["code"] = "JTG/T H21—2011";
    body["kind"] = "official";
    body["effective_date"] = "2011-09-01";
    body["withdrawn_date"] = "2011-08-01";
    StandardVersionInput input;
    const auto issues = parse_standard_version_input(body, input, "version.");
    ASSERT_EQ(issues.size(), 2u);
    EXPECT_EQ(issues[0].path, "version.label");
    EXPECT_EQ(issues[1].path, "version.withdrawn_date");
}

TEST(StandardDocumentModelsTest, OfficialNeedsAnEffectiveDateAndDraftsMustNotHaveOne) {
    Json::Value official_body;
    official_body["label"] = "2011 版";
    official_body["code"] = "JTG/T H21—2011";
    official_body["kind"] = "official";
    StandardVersionInput input;
    auto issues = parse_standard_version_input(official_body, input);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].path, "effective_date");

    Json::Value draft_body = official_body;
    draft_body["kind"] = "draft_approval";
    draft_body["effective_date"] = "2026-02-11";
    issues = parse_standard_version_input(draft_body, input);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].path, "effective_date");
}

TEST(StandardDocumentModelsTest, RejectsAnUnknownKindAndAnOverlongLabel) {
    Json::Value body;
    body["label"] = std::string(41, 'x');
    body["code"] = "C";
    body["kind"] = "final";
    StandardVersionInput input;
    const auto issues = parse_standard_version_input(body, input);
    ASSERT_EQ(issues.size(), 2u);
    EXPECT_EQ(issues[0].path, "label");
    EXPECT_EQ(issues[1].path, "kind");
}

TEST(StandardDocumentModelsTest, CountsCharactersNotBytes) {
    Json::Value body;
    std::string label;
    for (int index = 0; index < 40; ++index) label += "版";
    body["label"] = label;
    body["code"] = "C";
    body["kind"] = "draft_comment";
    StandardVersionInput input;
    EXPECT_TRUE(parse_standard_version_input(body, input).empty());
}

TEST(StandardDocumentModelsTest, ParsesDocumentInput) {
    Json::Value body;
    body["name"] = "公路桥梁技术状况评定标准";
    body["category"] = "技术状况评定";
    StandardDocumentInput input;
    EXPECT_TRUE(parse_standard_document_input(body, input).empty());
    EXPECT_FALSE(input.english_name.has_value());

    Json::Value missing;
    missing["name"] = "  ";
    const auto issues = parse_standard_document_input(missing, input, "document.");
    ASSERT_EQ(issues.size(), 2u);
    EXPECT_EQ(issues[0].path, "document.name");
    EXPECT_EQ(issues[1].path, "document.category");
}

TEST(StandardDocumentModelsTest, BuildsASafeDownloadFileName) {
    EXPECT_EQ(standard_pdf_file_name("JTG/T H21—2011", "公路桥梁技术状况评定标准"),
              "JTG T H21—2011 公路桥梁技术状况评定标准.pdf");
    EXPECT_EQ(standard_pdf_file_name("A:B*C", "名?称"), "A-B-C 名-称.pdf");
}

}  // namespace
```

把源文件和测试加进 `backend-cpp/CMakeLists.txt`（共享文件，只加这两行）：
- 在 `    src/standards/StandardRegistry.cpp` 那一行之后加 `    src/standards/StandardDocumentModels.cpp`
- 在 `    tests/test_standard_registry.cpp` 那一行之后加 `    tests/test_standard_document_models.cpp`

- [ ] **步骤 2：构建，确认失败**

运行：`cmake --build backend-cpp/build/vs-debug --config Debug --target bridge_report_backend_tests`
预期：编译失败，找不到 `bridge_report/standards/StandardDocumentModels.hpp`。

- [ ] **步骤 3：写头文件**

`backend-cpp/include/bridge_report/standards/StandardDocumentModels.hpp`：

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json/json.h>

namespace bridge_report::standards {

/*
 * 规范原文库的领域模型（设计 docs/superpowers/specs/2026-10-09-standard-documents-library-design.md §6）。
 *
 * 和同目录下的规则包（StandardModels 等）是两回事：规则包是本系统的结构化计算规则，
 * 这里是规范原文 PDF 及其版本。日期一律用 `YYYY-MM-DD` 字符串，字典序就是时间序。
 */

enum class StandardVersionKind { official, draft_comment, draft_approval };

/// 推导出来的状态，不存库（§6.4）。
enum class StandardVersionStatus { current, upcoming, withdrawn, draft_comment, draft_approval };

std::optional<StandardVersionKind> parse_standard_version_kind(const std::string& code);
const char* standard_version_kind_code(StandardVersionKind kind);
const char* standard_version_status_code(StandardVersionStatus status);

struct StandardVersionRecord {
    std::string id;
    std::string document_id;
    std::string label;
    std::string code;
    StandardVersionKind kind{StandardVersionKind::official};
    std::optional<std::string> nature;
    std::optional<std::string> issuer;
    std::optional<std::string> editor;
    std::optional<std::string> publish_date;
    std::optional<std::string> effective_date;
    /// 手工填写的废止日期；被后续版本替代的那种不存在这里（§6.5）。
    std::optional<std::string> withdrawn_date;
    std::optional<std::string> announcement;
    std::optional<std::string> source;
    std::optional<std::string> note;
    std::string archived_file_id;
    std::string original_file_name;
    std::string storage_relative_path;
    std::string file_hash;
    std::uintmax_t file_size_bytes{0};
    /// 上传人的显示名。
    std::optional<std::string> uploaded_by;
    std::string uploaded_at;
    std::string updated_at;
    /// 记录创建那天，版本日期的最后兜底（§6.6）。
    std::string created_date;
};

struct StandardDocumentRecord {
    std::string id;
    std::string name;
    std::optional<std::string> english_name;
    std::string category;
    /// 按版本日期从新到旧。
    std::vector<StandardVersionRecord> versions;
};

/// 一个版本推导出来的状态与替代关系，与输入的版本一一对应、顺序相同。
struct DerivedVersionState {
    StandardVersionStatus status{StandardVersionStatus::current};
    /// 生效的废止日期：手工值优先，否则取紧随其后那个正式版的实施日期。
    std::optional<std::string> withdrawn_date;
    /// "manual" 或 "replacement"；没有废止日期时为空。
    std::optional<std::string> withdrawn_date_source;
    std::optional<std::string> replaced_by_version_id;
    std::optional<std::string> replaces_version_id;
};

/// 版本日期 = 实施日期，没有就用发布日期，再没有就用创建日期；所有「从新到旧」都按它排。
std::string standard_version_date(const StandardVersionRecord& version);

std::vector<DerivedVersionState> derive_version_states(
    const std::vector<StandardVersionRecord>& versions, const std::string& today);

/// 现行版本；没有就取版本日期最晚的；没有版本时为空。
std::optional<std::string> default_standard_version_id(
    const std::vector<StandardVersionRecord>& versions,
    const std::vector<DerivedVersionState>& states);

Json::Value standard_document_json(const StandardDocumentRecord& document, const std::string& today);

/// 后端机器的本地日期，作状态推导的「今天」。
std::string local_today();

bool is_valid_iso_date(const std::string& value);

struct StandardDocumentInput {
    std::string name;
    std::optional<std::string> english_name;
    std::string category;
};

struct StandardVersionInput {
    std::string label;
    std::string code;
    StandardVersionKind kind{StandardVersionKind::official};
    std::optional<std::string> nature;
    std::optional<std::string> issuer;
    std::optional<std::string> editor;
    std::optional<std::string> publish_date;
    std::optional<std::string> effective_date;
    std::optional<std::string> withdrawn_date;
    std::optional<std::string> announcement;
    std::optional<std::string> source;
    std::optional<std::string> note;
};

struct StandardInputIssue {
    std::string path;
    std::string message;
};

/// 解析并校验规范信息；问题列表为空才算通过。每条问题的 path 前面拼上 prefix。
std::vector<StandardInputIssue> parse_standard_document_input(
    const Json::Value& body, StandardDocumentInput& input, const std::string& prefix = "");

/// 解析并校验版本信息，规则见设计 §7.4。
std::vector<StandardInputIssue> parse_standard_version_input(
    const Json::Value& body, StandardVersionInput& input, const std::string& prefix = "");

Json::Value standard_input_issues_json(const std::vector<StandardInputIssue>& issues);

/// 下载文件名「{标准编号} {规范名称}.pdf」：`/` 换成空格，其他 Windows 禁用字符换成 `-`。
std::string standard_pdf_file_name(const std::string& code, const std::string& document_name);

}  // namespace bridge_report::standards
```

- [ ] **步骤 4：写实现**

`backend-cpp/src/standards/StandardDocumentModels.cpp`：

```cpp
#include "bridge_report/standards/StandardDocumentModels.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <regex>

namespace bridge_report::standards {
namespace {

constexpr std::size_t kNameMax = 100;
constexpr std::size_t kCategoryMax = 30;
constexpr std::size_t kLabelMax = 40;
constexpr std::size_t kCodeMax = 60;
constexpr std::size_t kNoteMax = 500;
constexpr std::size_t kTextMax = 200;

/// 按字符计长度：UTF-8 里只数非续字节。
std::size_t character_count(const std::string& value) {
    return static_cast<std::size_t>(std::count_if(value.begin(), value.end(), [](char character) {
        return (static_cast<unsigned char>(character) & 0xC0) != 0x80;
    }));
}

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

void add_issue(std::vector<StandardInputIssue>& issues, const std::string& prefix,
               const std::string& field, const std::string& message) {
    issues.push_back({prefix + field, message});
}

std::string object_path(const std::string& prefix) {
    return prefix.empty() ? "body" : prefix.substr(0, prefix.size() - 1);
}

std::string length_message(const std::string& label, std::size_t max) {
    return label + "不能超过 " + std::to_string(max) + " 个字。";
}

/// 必填文本：缺失、不是字符串、去空白后为空、超长都记问题。
std::string required_text(const Json::Value& body, const char* field, std::size_t max,
                          const std::string& label, const std::string& prefix,
                          std::vector<StandardInputIssue>& issues) {
    const auto& value = body[field];
    if (!value.isString() || trim(value.asString()).empty()) {
        add_issue(issues, prefix, field, label + "不能为空。");
        return "";
    }
    const auto text = trim(value.asString());
    if (character_count(text) > max) add_issue(issues, prefix, field, length_message(label, max));
    return text;
}

/// 可选文本：缺失、null、空串都当没填；不是字符串或超长记问题。
std::optional<std::string> optional_text(const Json::Value& body, const char* field, std::size_t max,
                                         const std::string& label, const std::string& prefix,
                                         std::vector<StandardInputIssue>& issues) {
    const auto& value = body[field];
    if (value.isNull()) return std::nullopt;
    if (!value.isString()) {
        add_issue(issues, prefix, field, label + "必须是文本。");
        return std::nullopt;
    }
    const auto text = trim(value.asString());
    if (text.empty()) return std::nullopt;
    if (character_count(text) > max) add_issue(issues, prefix, field, length_message(label, max));
    return text;
}

std::optional<std::string> optional_date(const Json::Value& body, const char* field,
                                         const std::string& label, const std::string& prefix,
                                         std::vector<StandardInputIssue>& issues) {
    const auto& value = body[field];
    if (value.isNull()) return std::nullopt;
    const auto text = value.isString() ? trim(value.asString()) : std::string("invalid");
    if (text.empty()) return std::nullopt;
    if (!is_valid_iso_date(text)) {
        add_issue(issues, prefix, field, label + "必须是 YYYY-MM-DD 格式的日期。");
        return std::nullopt;
    }
    return text;
}

Json::Value nullable(const std::optional<std::string>& value) {
    return value.has_value() ? Json::Value(*value) : Json::Value(Json::nullValue);
}

}  // namespace

std::optional<StandardVersionKind> parse_standard_version_kind(const std::string& code) {
    if (code == "official") return StandardVersionKind::official;
    if (code == "draft_comment") return StandardVersionKind::draft_comment;
    if (code == "draft_approval") return StandardVersionKind::draft_approval;
    return std::nullopt;
}

const char* standard_version_kind_code(StandardVersionKind kind) {
    switch (kind) {
        case StandardVersionKind::official: return "official";
        case StandardVersionKind::draft_comment: return "draft_comment";
        case StandardVersionKind::draft_approval: return "draft_approval";
    }
    return "official";
}

const char* standard_version_status_code(StandardVersionStatus status) {
    switch (status) {
        case StandardVersionStatus::current: return "current";
        case StandardVersionStatus::upcoming: return "upcoming";
        case StandardVersionStatus::withdrawn: return "withdrawn";
        case StandardVersionStatus::draft_comment: return "draft_comment";
        case StandardVersionStatus::draft_approval: return "draft_approval";
    }
    return "current";
}

std::string standard_version_date(const StandardVersionRecord& version) {
    if (version.effective_date.has_value()) return *version.effective_date;
    if (version.publish_date.has_value()) return *version.publish_date;
    return version.created_date;
}

std::vector<DerivedVersionState> derive_version_states(
    const std::vector<StandardVersionRecord>& versions, const std::string& today) {
    std::vector<DerivedVersionState> states(versions.size());

    // 正式版按实施日期排成一条链：每个版本的后继就是替代它的那个（§6.5）。
    std::vector<std::size_t> chain;
    for (std::size_t index = 0; index < versions.size(); ++index) {
        if (versions[index].kind == StandardVersionKind::official) chain.push_back(index);
    }
    std::sort(chain.begin(), chain.end(), [&](std::size_t left, std::size_t right) {
        return versions[left].effective_date.value_or("") < versions[right].effective_date.value_or("");
    });

    for (std::size_t position = 0; position < chain.size(); ++position) {
        const auto& version = versions[chain[position]];
        auto& state = states[chain[position]];
        const bool has_next = position + 1 < chain.size();
        if (has_next) state.replaced_by_version_id = versions[chain[position + 1]].id;
        if (position > 0) state.replaces_version_id = versions[chain[position - 1]].id;

        if (version.withdrawn_date.has_value()) {
            state.withdrawn_date = version.withdrawn_date;
            state.withdrawn_date_source = "manual";
        } else if (has_next) {
            state.withdrawn_date = versions[chain[position + 1]].effective_date;
            state.withdrawn_date_source = "replacement";
        }

        if (state.withdrawn_date.has_value() && *state.withdrawn_date <= today) {
            state.status = StandardVersionStatus::withdrawn;
        } else if (version.effective_date.value_or("") <= today) {
            state.status = StandardVersionStatus::current;
        } else {
            state.status = StandardVersionStatus::upcoming;
        }
    }

    for (std::size_t index = 0; index < versions.size(); ++index) {
        if (versions[index].kind == StandardVersionKind::draft_comment) {
            states[index].status = StandardVersionStatus::draft_comment;
        } else if (versions[index].kind == StandardVersionKind::draft_approval) {
            states[index].status = StandardVersionStatus::draft_approval;
        }
    }
    return states;
}

std::optional<std::string> default_standard_version_id(
    const std::vector<StandardVersionRecord>& versions,
    const std::vector<DerivedVersionState>& states) {
    for (std::size_t index = 0; index < versions.size(); ++index) {
        if (states[index].status == StandardVersionStatus::current) return versions[index].id;
    }
    std::optional<std::size_t> latest;
    for (std::size_t index = 0; index < versions.size(); ++index) {
        if (!latest.has_value() ||
            standard_version_date(versions[index]) > standard_version_date(versions[*latest])) {
            latest = index;
        }
    }
    if (!latest.has_value()) return std::nullopt;
    return versions[*latest].id;
}

Json::Value standard_document_json(const StandardDocumentRecord& document, const std::string& today) {
    const auto states = derive_version_states(document.versions, today);
    Json::Value body;
    body["id"] = document.id;
    body["name"] = document.name;
    body["english_name"] = nullable(document.english_name);
    body["category"] = document.category;
    body["default_version_id"] = nullable(default_standard_version_id(document.versions, states));
    body["versions"] = Json::Value(Json::arrayValue);
    for (std::size_t index = 0; index < document.versions.size(); ++index) {
        const auto& version = document.versions[index];
        const auto& state = states[index];
        Json::Value item;
        item["id"] = version.id;
        item["label"] = version.label;
        item["code"] = version.code;
        item["kind"] = standard_version_kind_code(version.kind);
        item["status"] = standard_version_status_code(state.status);
        item["nature"] = nullable(version.nature);
        item["issuer"] = nullable(version.issuer);
        item["editor"] = nullable(version.editor);
        item["publish_date"] = nullable(version.publish_date);
        item["effective_date"] = nullable(version.effective_date);
        item["withdrawn_date"] = nullable(state.withdrawn_date);
        item["withdrawn_date_source"] = nullable(state.withdrawn_date_source);
        item["announcement"] = nullable(version.announcement);
        item["source"] = nullable(version.source);
        item["note"] = nullable(version.note);
        item["replaced_by_version_id"] = nullable(state.replaced_by_version_id);
        item["replaces_version_id"] = nullable(state.replaces_version_id);
        Json::Value file;
        file["original_name"] = version.original_file_name;
        file["size_bytes"] = static_cast<Json::UInt64>(version.file_size_bytes);
        item["file"] = file;
        item["content_path"] = "/api/standard-document-versions/" + version.id + "/content";
        item["uploaded_by"] = nullable(version.uploaded_by);
        item["uploaded_at"] = version.uploaded_at;
        item["updated_at"] = version.updated_at;
        body["versions"].append(item);
    }
    return body;
}

std::string local_today() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[11]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &local);
    return buffer;
}

bool is_valid_iso_date(const std::string& value) {
    static const std::regex pattern("^[0-9]{4}-[0-9]{2}-[0-9]{2}$");
    if (!std::regex_match(value, pattern)) return false;
    const std::chrono::year year{std::stoi(value.substr(0, 4))};
    const std::chrono::month month{static_cast<unsigned>(std::stoi(value.substr(5, 2)))};
    const std::chrono::day day{static_cast<unsigned>(std::stoi(value.substr(8, 2)))};
    return std::chrono::year_month_day{year, month, day}.ok();
}

std::vector<StandardInputIssue> parse_standard_document_input(
    const Json::Value& body, StandardDocumentInput& input, const std::string& prefix) {
    std::vector<StandardInputIssue> issues;
    if (!body.isObject()) {
        issues.push_back({object_path(prefix), "规范信息必须是 JSON 对象。"});
        return issues;
    }
    input.name = required_text(body, "name", kNameMax, "规范名称", prefix, issues);
    input.english_name = optional_text(body, "english_name", kTextMax, "英文名称", prefix, issues);
    input.category = required_text(body, "category", kCategoryMax, "分类", prefix, issues);
    return issues;
}

std::vector<StandardInputIssue> parse_standard_version_input(
    const Json::Value& body, StandardVersionInput& input, const std::string& prefix) {
    std::vector<StandardInputIssue> issues;
    if (!body.isObject()) {
        issues.push_back({object_path(prefix), "版本信息必须是 JSON 对象。"});
        return issues;
    }
    input.label = required_text(body, "label", kLabelMax, "版本名称", prefix, issues);
    input.code = required_text(body, "code", kCodeMax, "标准编号", prefix, issues);
    const auto kind = body["kind"].isString()
        ? parse_standard_version_kind(body["kind"].asString())
        : std::nullopt;
    if (kind.has_value()) {
        input.kind = *kind;
    } else {
        add_issue(issues, prefix, "kind", "稿件类型必须是正式版、征求意见稿或报批稿。");
    }
    input.nature = optional_text(body, "nature", kTextMax, "标准性质", prefix, issues);
    input.issuer = optional_text(body, "issuer", kTextMax, "发布部门", prefix, issues);
    input.editor = optional_text(body, "editor", kTextMax, "主编单位", prefix, issues);
    input.publish_date = optional_date(body, "publish_date", "发布日期", prefix, issues);
    input.effective_date = optional_date(body, "effective_date", "实施日期", prefix, issues);
    input.withdrawn_date = optional_date(body, "withdrawn_date", "废止日期", prefix, issues);
    input.announcement = optional_text(body, "announcement", kTextMax, "公告文号", prefix, issues);
    input.source = optional_text(body, "source", kTextMax, "原文来源", prefix, issues);
    input.note = optional_text(body, "note", kNoteMax, "备注", prefix, issues);

    const auto has_issue = [&](const char* field) {
        return std::any_of(issues.begin(), issues.end(), [&](const StandardInputIssue& issue) {
            return issue.path == prefix + field;
        });
    };
    if (kind == StandardVersionKind::official) {
        if (!input.effective_date.has_value() && !has_issue("effective_date")) {
            add_issue(issues, prefix, "effective_date", "正式版必须填写实施日期。");
        }
        if (input.withdrawn_date.has_value() && input.effective_date.has_value() &&
            *input.withdrawn_date < *input.effective_date) {
            add_issue(issues, prefix, "withdrawn_date", "废止日期不能早于实施日期。");
        }
    } else if (kind.has_value()) {
        if (input.effective_date.has_value()) {
            add_issue(issues, prefix, "effective_date", "征求意见稿、报批稿不填实施日期。");
        }
        if (input.withdrawn_date.has_value()) {
            add_issue(issues, prefix, "withdrawn_date", "征求意见稿、报批稿不填废止日期。");
        }
    }
    return issues;
}

Json::Value standard_input_issues_json(const std::vector<StandardInputIssue>& issues) {
    Json::Value array(Json::arrayValue);
    for (const auto& issue : issues) {
        Json::Value item;
        item["path"] = issue.path;
        item["message"] = issue.message;
        array.append(item);
    }
    return array;
}

std::string standard_pdf_file_name(const std::string& code, const std::string& document_name) {
    std::string result;
    // 逐字节处理是安全的：UTF-8 多字节序列里的每个字节都 ≥ 0x80，不会撞上下面这些 ASCII。
    for (const char character : code + " " + document_name) {
        if (character == '/') {
            result += ' ';
        } else if (character == '\\' || character == ':' || character == '*' || character == '?' ||
                   character == '"' || character == '<' || character == '>' || character == '|') {
            result += '-';
        } else {
            result += character;
        }
    }
    return result + ".pdf";
}

}  // namespace bridge_report::standards
```

- [ ] **步骤 5：构建并跑测试，确认通过**

运行：
`cmake --build backend-cpp/build/vs-debug --config Debug --target bridge_report_backend_tests`
`backend-cpp/build/vs-debug/Debug/bridge_report_backend_tests.exe --gtest_filter=StandardDocumentModelsTest.*`
预期：16 个用例全部 PASS。

- [ ] **步骤 6：提交（只加本任务新建文件；CMakeLists 留到任务 12）**

```bash
git add -- backend-cpp/include/bridge_report/standards/StandardDocumentModels.hpp backend-cpp/src/standards/StandardDocumentModels.cpp backend-cpp/tests/test_standard_document_models.cpp
```

提交信息：`feat(standards): 规范原文的状态推导与输入校验`

---

### 任务 3：原文归档（PDF 识别、写盘、删盘）

**文件：**
- 新建：`backend-cpp/include/bridge_report/archive/StandardDocumentArchive.hpp`
- 新建：`backend-cpp/src/archive/StandardDocumentArchive.cpp`
- 新建：`backend-cpp/tests/test_standard_document_archive.cpp`
- 修改（共享）：`backend-cpp/CMakeLists.txt`

**接口：**
- 使用：`archive::sanitize_path_part`、`archive::resolve_path_under_root`、`archive::is_safe_archive_relative_path`（`ArchivePaths.hpp`），`archive::sha256_hex`（`ImageContent.hpp`）。
- 产出（命名空间 `bridge_report::archive`）：
  - 异常 `StandardDocumentArchiveError`，以及它的两个子类 `StandardDocumentNotPdfError`、`StandardDocumentEmptyError`
  - `struct ArchivedStandardPdf { std::string original_file_name; std::string storage_relative_path; std::uintmax_t file_size_bytes; std::string sha256; }`
  - `bool looks_like_pdf(std::string_view content, std::string_view original_file_name)`
  - `std::filesystem::path build_standard_document_relative_path(std::string_view document_id, std::string_view file_name)`
  - `ArchivedStandardPdf archive_standard_pdf(std::string_view content, std::string_view original_file_name, const std::filesystem::path& archive_root, std::string_view document_id)`
  - `void remove_archived_standard_pdf(const std::filesystem::path& archive_root, const std::filesystem::path& storage_relative_path, const std::string& sha256) noexcept`

- [ ] **步骤 1：写失败的测试**

`backend-cpp/tests/test_standard_document_archive.cpp`：

```cpp
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/archive/StandardDocumentArchive.hpp"

namespace {

namespace fs = std::filesystem;
using namespace bridge_report::archive;

const std::string kPdf = "%PDF-1.4\n1 0 obj << >> endobj\n%%EOF\n";

class StandardDocumentArchiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
            ("standard-document-archive-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        fs::remove_all(root_);
        fs::create_directories(root_);
    }

    void TearDown() override { fs::remove_all(root_); }

    fs::path root_;
};

TEST_F(StandardDocumentArchiveTest, StoresThePdfUnderItsDocumentDirectory) {
    const auto archived = archive_standard_pdf(kPdf, "公路桥梁技术状况评定标准.pdf", root_, "doc-1");

    EXPECT_EQ(archived.original_file_name, "公路桥梁技术状况评定标准.pdf");
    EXPECT_EQ(archived.storage_relative_path.rfind("standards/doc-1/", 0), 0u);
    EXPECT_EQ(archived.storage_relative_path.substr(archived.storage_relative_path.size() - 4), ".pdf");
    EXPECT_EQ(archived.file_size_bytes, kPdf.size());
    EXPECT_EQ(archived.sha256.size(), 64u);

    std::ifstream input(root_ / archived.storage_relative_path, std::ios::binary);
    const std::string stored((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    EXPECT_EQ(stored, kPdf);
}

TEST_F(StandardDocumentArchiveTest, EveryUploadGetsItsOwnFile) {
    const auto first = archive_standard_pdf(kPdf, "a.pdf", root_, "doc-1");
    const auto second = archive_standard_pdf(kPdf, "a.pdf", root_, "doc-1");
    EXPECT_NE(first.storage_relative_path, second.storage_relative_path);
}

TEST_F(StandardDocumentArchiveTest, RejectsContentThatIsNotAPdf) {
    EXPECT_THROW(archive_standard_pdf("hello", "a.pdf", root_, "doc-1"), StandardDocumentNotPdfError);
    EXPECT_THROW(archive_standard_pdf(kPdf, "a.txt", root_, "doc-1"), StandardDocumentNotPdfError);
    EXPECT_THROW(archive_standard_pdf("", "a.pdf", root_, "doc-1"), StandardDocumentEmptyError);
    EXPECT_TRUE(fs::is_empty(root_));
}

TEST_F(StandardDocumentArchiveTest, AcceptsAnUppercaseExtension) {
    EXPECT_TRUE(looks_like_pdf(kPdf, "H21.PDF"));
}

TEST_F(StandardDocumentArchiveTest, RemovesOnlyWhenTheHashMatches) {
    const auto archived = archive_standard_pdf(kPdf, "a.pdf", root_, "doc-1");
    const auto absolute = root_ / archived.storage_relative_path;

    remove_archived_standard_pdf(root_, archived.storage_relative_path, std::string(64, '0'));
    EXPECT_TRUE(fs::exists(absolute));

    remove_archived_standard_pdf(root_, archived.storage_relative_path, archived.sha256);
    EXPECT_FALSE(fs::exists(absolute));
}

}  // namespace
```

`backend-cpp/CMakeLists.txt`（共享，只加两行）：
- 在 `    src/archive/BridgeMediaArchive.cpp` 之后加 `    src/archive/StandardDocumentArchive.cpp`
- 在 `    tests/test_standard_document_models.cpp` 之后加 `    tests/test_standard_document_archive.cpp`

- [ ] **步骤 2：构建，确认失败**

运行：`cmake --build backend-cpp/build/vs-debug --config Debug --target bridge_report_backend_tests`
预期：编译失败，找不到 `bridge_report/archive/StandardDocumentArchive.hpp`。

- [ ] **步骤 3：写头文件**

`backend-cpp/include/bridge_report/archive/StandardDocumentArchive.hpp`：

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bridge_report::archive {

class StandardDocumentArchiveError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// 扩展名不是 .pdf，或文件头不是 `%PDF-`。调用方要把它和写盘失败映射成不同的 HTTP 状态。
class StandardDocumentNotPdfError : public StandardDocumentArchiveError {
public:
    using StandardDocumentArchiveError::StandardDocumentArchiveError;
};

/// 空文件。
class StandardDocumentEmptyError : public StandardDocumentArchiveError {
public:
    using StandardDocumentArchiveError::StandardDocumentArchiveError;
};

struct ArchivedStandardPdf {
    std::string original_file_name;
    std::string storage_relative_path;
    std::uintmax_t file_size_bytes{0};
    std::string sha256;
};

/// 扩展名（不分大小写）是 .pdf，而且内容以 `%PDF-` 开头。
bool looks_like_pdf(std::string_view content, std::string_view original_file_name);

/// `standards/<规范 id>/<文件名>`，全是 ASCII：窄字符 API 遇到中文路径会建出乱码目录。
std::filesystem::path build_standard_document_relative_path(
    std::string_view document_id, std::string_view file_name);

/**
 * @brief 把一份规范原文写进归档。
 *
 * 校验全在写盘之前完成；先写 `.uploading` 再改名，任何失败都不留下半个文件。
 * 每次都用新的 UUID 文件名：替换原文时新旧文件互不覆盖，提交不确定时两份都还在。
 *
 * @throws StandardDocumentEmptyError 内容为空。
 * @throws StandardDocumentNotPdfError 不是 PDF。
 * @throws StandardDocumentArchiveError 写盘失败。
 */
ArchivedStandardPdf archive_standard_pdf(
    std::string_view content,
    std::string_view original_file_name,
    const std::filesystem::path& archive_root,
    std::string_view document_id);

/// 删除已归档的原文；SHA-256 对不上就原样留着，宁可留垃圾也不误删。
void remove_archived_standard_pdf(
    const std::filesystem::path& archive_root,
    const std::filesystem::path& storage_relative_path,
    const std::string& sha256) noexcept;

}  // namespace bridge_report::archive
```

- [ ] **步骤 4：写实现**

`backend-cpp/src/archive/StandardDocumentArchive.cpp`：

```cpp
#include "bridge_report/archive/StandardDocumentArchive.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

#include <drogon/utils/Utilities.h>

#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/archive/ImageContent.hpp"

namespace bridge_report::archive {

bool looks_like_pdf(std::string_view content, std::string_view original_file_name) {
    if (original_file_name.size() < 4) return false;
    std::string extension(original_file_name.substr(original_file_name.size() - 4));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension == ".pdf" && content.substr(0, 5) == "%PDF-";
}

std::filesystem::path build_standard_document_relative_path(
    std::string_view document_id, std::string_view file_name) {
    return std::filesystem::path("standards") / sanitize_path_part(document_id) / sanitize_path_part(file_name);
}

ArchivedStandardPdf archive_standard_pdf(
    std::string_view content,
    std::string_view original_file_name,
    const std::filesystem::path& archive_root,
    std::string_view document_id) {
    if (content.empty()) throw StandardDocumentEmptyError("standard document upload is empty");
    // 只信内容和扩展名。浏览器报的 MIME 是客户端说了算的。
    if (!looks_like_pdf(content, original_file_name)) {
        throw StandardDocumentNotPdfError("standard document upload is not a PDF");
    }
    const auto hash = sha256_hex(content);
    if (hash.empty()) throw StandardDocumentArchiveError("unable to hash the standard document");

    const auto relative = build_standard_document_relative_path(document_id, drogon::utils::getUuid() + ".pdf");
    const auto absolute = resolve_path_under_root(archive_root, relative);

    std::error_code code;
    std::filesystem::create_directories(absolute.parent_path(), code);
    if (code) throw StandardDocumentArchiveError("unable to create the standard document directory");

    auto staging = absolute;
    staging += ".uploading";
    {
        std::ofstream output(staging, std::ios::binary | std::ios::trunc);
        if (!output) throw StandardDocumentArchiveError("unable to open the standard document for writing");
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.close();
        if (!output) {
            std::filesystem::remove(staging, code);
            throw StandardDocumentArchiveError("unable to write the standard document");
        }
    }
    std::filesystem::rename(staging, absolute, code);
    if (code) {
        std::error_code ignored;
        std::filesystem::remove(staging, ignored);
        throw StandardDocumentArchiveError("unable to publish the standard document");
    }

    ArchivedStandardPdf archived;
    archived.original_file_name = std::string(original_file_name);
    archived.storage_relative_path = relative.generic_string();
    archived.file_size_bytes = content.size();
    archived.sha256 = hash;
    return archived;
}

void remove_archived_standard_pdf(
    const std::filesystem::path& archive_root,
    const std::filesystem::path& storage_relative_path,
    const std::string& sha256) noexcept {
    try {
        if (!is_safe_archive_relative_path(storage_relative_path)) return;
        const auto absolute = resolve_path_under_root(archive_root, storage_relative_path);
        if (!std::filesystem::is_regular_file(absolute)) return;

        std::string content;
        {
            // 读完就关：Windows 上文件还开着句柄就删不掉，而下面的删除失败是静默的。
            std::ifstream input(absolute, std::ios::binary);
            if (!input) return;
            content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }
        // 内容对不上说明这不是我们以为的那个文件，留着比误删强。
        if (!sha256.empty() && sha256_hex(content) != sha256) return;

        std::error_code code;
        std::filesystem::remove(absolute, code);
    } catch (...) {
        // 清理失败不该影响业务结果：数据库那一侧已经改完了。
    }
}

}  // namespace bridge_report::archive
```

- [ ] **步骤 5：构建并跑测试，确认通过**

运行：
`cmake --build backend-cpp/build/vs-debug --config Debug --target bridge_report_backend_tests`
`backend-cpp/build/vs-debug/Debug/bridge_report_backend_tests.exe --gtest_filter=StandardDocumentArchiveTest.*`
预期：5 个用例全部 PASS。

- [ ] **步骤 6：提交**

```bash
git add -- backend-cpp/include/bridge_report/archive/StandardDocumentArchive.hpp backend-cpp/src/archive/StandardDocumentArchive.cpp backend-cpp/tests/test_standard_document_archive.cpp
```

提交信息：`feat(archive): 规范原文 PDF 的归档与清理`

---

### 任务 4：仓储层

**文件：**
- 新建：`backend-cpp/include/bridge_report/db/StandardDocumentRepository.hpp`
- 新建：`backend-cpp/src/db/StandardDocumentRepository.cpp`
- 新建：`backend-cpp/tests/test_standard_document_repository.cpp`
- 修改（共享）：`backend-cpp/CMakeLists.txt`

**接口：**
- 使用：任务 2 的 `standards::StandardDocumentRecord`、`StandardVersionRecord`、`StandardDocumentInput`、`StandardVersionInput`、`parse_standard_version_kind`、`standard_version_kind_code`；`db::CommitLatch`（`CommitLatch.hpp`）。
- 产出（命名空间 `bridge_report::db`）：
  - `class StandardDocumentCommitUncertain : public std::runtime_error`
  - `enum class StandardDocumentWriteStatus { Ok, DocumentNotFound, VersionNotFound, NameTaken, LabelTaken, EffectiveDateTaken }`
  - `struct StandardPdfFileInput { std::string original_file_name; std::string storage_relative_path; std::uintmax_t file_size_bytes; std::string sha256; }`
  - `struct StandardFileToRemove { std::string storage_relative_path; std::string sha256; }`
  - `struct StandardDocumentWriteOutcome { StandardDocumentWriteStatus status; std::optional<standards::StandardDocumentRecord> document; std::vector<StandardFileToRemove> files_to_remove; }`
  - `struct StandardVersionFile { standards::StandardVersionRecord version; std::string document_name; }`
  - `class StandardDocumentRepository`，方法：
    - `allocate_document_id()`、`list()`、`find(document_id)`、`find_version_file(version_id)`
    - `create(document_id, document, version, file, user_id)`、`update(document_id, document)`、`remove(document_id)`
    - `add_version(document_id, version, file, user_id)`、`update_version(version_id, version)`
    - `replace_version_file(version_id, file, user_id)`、`remove_version(version_id)`
    - 签名见下方头文件。`user_id` 为空串时表示无上传人。

- [ ] **步骤 1：写失败的测试**

`backend-cpp/tests/test_standard_document_repository.cpp`：

```cpp
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>

#include <drogon/utils/Utilities.h>
#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/StandardDocumentRepository.hpp"

namespace {

using bridge_report::db::StandardDocumentRepository;
using bridge_report::db::StandardDocumentWriteStatus;
using bridge_report::db::StandardPdfFileInput;
using bridge_report::standards::StandardDocumentInput;
using bridge_report::standards::StandardVersionInput;
using bridge_report::standards::StandardVersionKind;

/// 整个进程共用一个连接：max_connections 是 100，每个用例各开一个会把连接池耗尽。
drogon::orm::DbClientPtr shared_test_client() {
    static drogon::orm::DbClientPtr client = [] {
        const bridge_report::config::PostgresConfig config{};
        return bridge_report::db::create_db_client(config, 1);
    }();
    return client;
}

// 规范原文库仓储的数据库集成夹具。
//
// 关心三件事：唯一性冲突给出业务结论而不是数据库异常；替换和删除把要删的文件交回调用方；
// 删掉最后一个版本时规范本身留着。
class StandardDocumentRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        client_ = shared_test_client();
        run_ = drogon::utils::getUuid();
        prefix_ = "规范原文测试-" + run_ + "-";
    }

    void TearDown() override {
        if (client_ == nullptr) return;
        // 先删规范（级联删版本），归档行才删得掉。
        client_->execSqlSync("delete from standard_documents where name like $1", prefix_ + "%");
        client_->execSqlSync("delete from archived_files where storage_relative_path like $1",
                             "standard-test/" + run_ + "/%");
    }

    StandardDocumentRepository repository() const { return StandardDocumentRepository(client_); }

    StandardPdfFileInput file(const std::string& name) const {
        return {name, "standard-test/" + run_ + "/" + name, 1024, std::string(64, 'b')};
    }

    static StandardVersionInput official(const std::string& label, const std::string& effective) {
        StandardVersionInput input;
        input.label = label;
        input.code = "CODE " + label;
        input.kind = StandardVersionKind::official;
        input.effective_date = effective;
        return input;
    }

    static StandardVersionInput draft(const std::string& label) {
        StandardVersionInput input;
        input.label = label;
        input.code = "CODE " + label;
        input.kind = StandardVersionKind::draft_comment;
        return input;
    }

    StandardDocumentInput document(const std::string& suffix) const {
        return {prefix_ + suffix, std::nullopt, "测试分类"};
    }

    std::string create_document(const std::string& suffix, const StandardVersionInput& first,
                                const std::string& file_name) {
        const auto id = repository().allocate_document_id();
        const auto outcome = repository().create(id, document(suffix), first, file(file_name), "");
        EXPECT_EQ(outcome.status, StandardDocumentWriteStatus::Ok);
        return id;
    }

    drogon::orm::DbClientPtr client_;
    std::string run_;
    std::string prefix_;
};

TEST_F(StandardDocumentRepositoryTest, CreatesADocumentWithItsFirstVersion) {
    const auto id = create_document("H21", official("2011 版", "2011-09-01"), "h21.pdf");
    const auto found = repository().find(id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, prefix_ + "H21");
    ASSERT_EQ(found->versions.size(), 1u);
    EXPECT_EQ(found->versions[0].label, "2011 版");
    EXPECT_EQ(found->versions[0].effective_date, std::optional<std::string>("2011-09-01"));
    EXPECT_EQ(found->versions[0].original_file_name, "h21.pdf");
    EXPECT_FALSE(found->versions[0].nature.has_value());
}

TEST_F(StandardDocumentRepositoryTest, RejectsADuplicateDocumentName) {
    create_document("重名", official("2011 版", "2011-09-01"), "a.pdf");
    const auto id = repository().allocate_document_id();
    const auto outcome = repository().create(id, document("重名"), official("2011 版", "2011-09-01"), file("b.pdf"), "");
    EXPECT_EQ(outcome.status, StandardDocumentWriteStatus::NameTaken);
    EXPECT_FALSE(repository().find(id).has_value());
}

TEST_F(StandardDocumentRepositoryTest, RejectsDuplicateLabelsAndOfficialEffectiveDates) {
    const auto id = create_document("冲突", official("2011 版", "2011-09-01"), "a.pdf");
    EXPECT_EQ(repository().add_version(id, official("2011 版", "2021-11-01"), file("b.pdf"), "").status,
              StandardDocumentWriteStatus::LabelTaken);
    EXPECT_EQ(repository().add_version(id, official("另一版", "2011-09-01"), file("c.pdf"), "").status,
              StandardDocumentWriteStatus::EffectiveDateTaken);
    EXPECT_EQ(repository().add_version(id, draft("2017 征求意见稿"), file("d.pdf"), "").status,
              StandardDocumentWriteStatus::Ok);
}

TEST_F(StandardDocumentRepositoryTest, AddingAVersionToAMissingDocumentReportsNotFound) {
    const auto missing = repository().allocate_document_id();
    EXPECT_EQ(repository().add_version(missing, official("2011 版", "2011-09-01"), file("a.pdf"), "").status,
              StandardDocumentWriteStatus::DocumentNotFound);
}

TEST_F(StandardDocumentRepositoryTest, ListsVersionsNewestFirstUnderTheirDocument) {
    const auto id = create_document("排序", official("2004 版", "2004-10-01"), "old.pdf");
    ASSERT_EQ(repository().add_version(id, official("2021 版", "2021-11-01"), file("new.pdf"), "").status,
              StandardDocumentWriteStatus::Ok);
    const auto documents = repository().list();
    const auto found = std::find_if(documents.begin(), documents.end(),
                                    [&](const auto& item) { return item.id == id; });
    ASSERT_NE(found, documents.end());
    ASSERT_EQ(found->versions.size(), 2u);
    EXPECT_EQ(found->versions[0].label, "2021 版");
    EXPECT_EQ(found->versions[1].label, "2004 版");
}

TEST_F(StandardDocumentRepositoryTest, UpdatesVersionFieldsWithoutTouchingTheFile) {
    const auto id = create_document("编辑", official("2011 版", "2011-09-01"), "a.pdf");
    const auto before = repository().find(id)->versions[0];
    auto changed = official("2011 版（修订）", "2011-09-01");
    changed.announcement = "交通运输部公告2011年第41号";

    const auto outcome = repository().update_version(before.id, changed);
    ASSERT_EQ(outcome.status, StandardDocumentWriteStatus::Ok);
    ASSERT_TRUE(outcome.document.has_value());
    EXPECT_EQ(outcome.document->versions[0].label, "2011 版（修订）");
    EXPECT_EQ(outcome.document->versions[0].announcement, std::optional<std::string>("交通运输部公告2011年第41号"));
    EXPECT_EQ(outcome.document->versions[0].archived_file_id, before.archived_file_id);
    EXPECT_TRUE(outcome.files_to_remove.empty());
}

TEST_F(StandardDocumentRepositoryTest, ReplacingTheFileHandsBackTheOldOne) {
    const auto id = create_document("替换", official("2011 版", "2011-09-01"), "old.pdf");
    const auto version_id = repository().find(id)->versions[0].id;

    const auto outcome = repository().replace_version_file(version_id, file("new.pdf"), "");
    ASSERT_EQ(outcome.status, StandardDocumentWriteStatus::Ok);
    EXPECT_EQ(outcome.document->versions[0].original_file_name, "new.pdf");
    ASSERT_EQ(outcome.files_to_remove.size(), 1u);
    EXPECT_EQ(outcome.files_to_remove[0].storage_relative_path, "standard-test/" + run_ + "/old.pdf");
    EXPECT_TRUE(client_->execSqlSync("select 1 from archived_files where storage_relative_path = $1",
                                     "standard-test/" + run_ + "/old.pdf").empty());
}

TEST_F(StandardDocumentRepositoryTest, RemovingAVersionKeepsTheDocument) {
    const auto id = create_document("删版本", official("2011 版", "2011-09-01"), "a.pdf");
    const auto version_id = repository().find(id)->versions[0].id;

    const auto outcome = repository().remove_version(version_id);
    ASSERT_EQ(outcome.status, StandardDocumentWriteStatus::Ok);
    ASSERT_TRUE(outcome.document.has_value());
    EXPECT_TRUE(outcome.document->versions.empty());
    EXPECT_EQ(outcome.files_to_remove.size(), 1u);
    EXPECT_EQ(repository().remove_version(version_id).status, StandardDocumentWriteStatus::VersionNotFound);
}

TEST_F(StandardDocumentRepositoryTest, RemovingADocumentHandsBackEveryFile) {
    const auto id = create_document("删规范", official("2004 版", "2004-10-01"), "a.pdf");
    ASSERT_EQ(repository().add_version(id, draft("2017 征求意见稿"), file("b.pdf"), "").status,
              StandardDocumentWriteStatus::Ok);

    const auto outcome = repository().remove(id);
    ASSERT_EQ(outcome.status, StandardDocumentWriteStatus::Ok);
    EXPECT_EQ(outcome.files_to_remove.size(), 2u);
    EXPECT_FALSE(repository().find(id).has_value());
    EXPECT_EQ(repository().remove(id).status, StandardDocumentWriteStatus::DocumentNotFound);
}

TEST_F(StandardDocumentRepositoryTest, FindsTheFileOfAVersionWithItsDocumentName) {
    const auto id = create_document("内容", official("2011 版", "2011-09-01"), "a.pdf");
    const auto version_id = repository().find(id)->versions[0].id;
    const auto found = repository().find_version_file(version_id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->document_name, prefix_ + "内容");
    EXPECT_EQ(found->version.storage_relative_path, "standard-test/" + run_ + "/a.pdf");
}

}  // namespace
```

`backend-cpp/CMakeLists.txt`（共享，只加两行）：
- 在 `    src/db/StandardRepository.cpp` 之后加 `    src/db/StandardDocumentRepository.cpp`
- 在 `    tests/test_standard_document_archive.cpp` 之后加 `    tests/test_standard_document_repository.cpp`

- [ ] **步骤 2：构建，确认失败**

运行：`cmake --build backend-cpp/build/vs-debug --config Debug --target bridge_report_backend_tests`
预期：编译失败，找不到 `bridge_report/db/StandardDocumentRepository.hpp`。

- [ ] **步骤 3：写头文件**

`backend-cpp/include/bridge_report/db/StandardDocumentRepository.hpp`：

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/standards/StandardDocumentModels.hpp"

namespace bridge_report::db {

/// 提交结果不确定：服务器可能已经提交，只是确认没送回来。此时任何文件都不能删。
class StandardDocumentCommitUncertain : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class StandardDocumentWriteStatus {
    Ok,
    DocumentNotFound,
    VersionNotFound,
    NameTaken,
    LabelTaken,
    EffectiveDateTaken,
};

/// 已写进归档目录、还没入库的新原文。
struct StandardPdfFileInput {
    std::string original_file_name;
    std::string storage_relative_path;
    std::uintmax_t file_size_bytes{0};
    std::string sha256;
};

/// 事务提交之后要从磁盘删掉的文件。
struct StandardFileToRemove {
    std::string storage_relative_path;
    std::string sha256;
};

struct StandardDocumentWriteOutcome {
    StandardDocumentWriteStatus status{StandardDocumentWriteStatus::Ok};
    /// 写完之后的规范；删除整部规范时为空。
    std::optional<standards::StandardDocumentRecord> document;
    std::vector<StandardFileToRemove> files_to_remove;
};

/// 原文内容接口要的东西：版本记录，加上拼下载文件名用的规范名称。
struct StandardVersionFile {
    standards::StandardVersionRecord version;
    std::string document_name;
};

/**
 * @brief 规范原文库的读写（设计 §6、§7）。
 *
 * 文件本身在 archived_files 里。和桥梁图件一样：磁盘上的旧文件由调用方在事务提交之后删，
 * 这里只把「要删哪些」交回去；提交确认丢失时抛 StandardDocumentCommitUncertain。
 * 同一部规范的版本写入都先锁住规范那一行，重名和实施日期的检查才可靠。
 */
class StandardDocumentRepository {
public:
    explicit StandardDocumentRepository(drogon::orm::DbClientPtr client);

    /// 新规范的 id。路由要先拿到它，才能把原文写进 `standards/<id>/`。
    std::string allocate_document_id() const;

    std::vector<standards::StandardDocumentRecord> list() const;
    std::optional<standards::StandardDocumentRecord> find(const std::string& document_id) const;
    std::optional<StandardVersionFile> find_version_file(const std::string& version_id) const;

    StandardDocumentWriteOutcome create(
        const std::string& document_id,
        const standards::StandardDocumentInput& document,
        const standards::StandardVersionInput& version,
        const StandardPdfFileInput& file,
        const std::string& user_id) const;
    StandardDocumentWriteOutcome update(
        const std::string& document_id, const standards::StandardDocumentInput& document) const;
    StandardDocumentWriteOutcome remove(const std::string& document_id) const;

    StandardDocumentWriteOutcome add_version(
        const std::string& document_id,
        const standards::StandardVersionInput& version,
        const StandardPdfFileInput& file,
        const std::string& user_id) const;
    StandardDocumentWriteOutcome update_version(
        const std::string& version_id, const standards::StandardVersionInput& version) const;
    StandardDocumentWriteOutcome replace_version_file(
        const std::string& version_id, const StandardPdfFileInput& file, const std::string& user_id) const;
    StandardDocumentWriteOutcome remove_version(const std::string& version_id) const;

private:
    drogon::orm::DbClientPtr client_;
};

}  // namespace bridge_report::db
```

- [ ] **步骤 4：写实现**

`backend-cpp/src/db/StandardDocumentRepository.cpp`：

```cpp
#include "bridge_report/db/StandardDocumentRepository.hpp"

#include <map>
#include <memory>
#include <utility>

#include "bridge_report/db/CommitLatch.hpp"

namespace bridge_report::db {
namespace {

using standards::StandardDocumentRecord;
using standards::StandardVersionRecord;

constexpr const char* kVersionColumns =
    "v.id::text as id, v.document_id::text as document_id, v.label, v.code, v.kind, "
    "v.nature, v.issuer, v.editor, "
    "to_char(v.publish_date, 'YYYY-MM-DD') as publish_date, "
    "to_char(v.effective_date, 'YYYY-MM-DD') as effective_date, "
    "to_char(v.withdrawn_date, 'YYYY-MM-DD') as withdrawn_date, "
    "v.announcement, v.source, v.note, v.archived_file_id::text as archived_file_id, "
    "f.original_file_name, f.storage_relative_path, coalesce(f.file_hash, '') as file_hash, "
    "coalesce(f.file_size_bytes, 0) as file_size_bytes, u.display_name as uploaded_by, "
    "to_char(f.uploaded_at, 'YYYY-MM-DD HH24:MI:SS') as uploaded_at, "
    "to_char(v.updated_at, 'YYYY-MM-DD HH24:MI:SS') as updated_at, "
    "to_char(v.created_at, 'YYYY-MM-DD') as created_date";

constexpr const char* kVersionFrom =
    " from standard_document_versions v"
    " join archived_files f on f.id = v.archived_file_id"
    " left join users u on u.id = v.uploaded_by";

constexpr const char* kVersionOrder =
    " order by coalesce(v.effective_date, v.publish_date, v.created_at::date) desc, v.label";

std::optional<std::string> optional_text(const drogon::orm::Row& row, const char* column) {
    if (row[column].isNull()) return std::nullopt;
    return row[column].as<std::string>();
}

StandardVersionRecord read_version(const drogon::orm::Row& row) {
    StandardVersionRecord version;
    version.id = row["id"].as<std::string>();
    version.document_id = row["document_id"].as<std::string>();
    version.label = row["label"].as<std::string>();
    version.code = row["code"].as<std::string>();
    version.kind = standards::parse_standard_version_kind(row["kind"].as<std::string>())
        .value_or(standards::StandardVersionKind::official);
    version.nature = optional_text(row, "nature");
    version.issuer = optional_text(row, "issuer");
    version.editor = optional_text(row, "editor");
    version.publish_date = optional_text(row, "publish_date");
    version.effective_date = optional_text(row, "effective_date");
    version.withdrawn_date = optional_text(row, "withdrawn_date");
    version.announcement = optional_text(row, "announcement");
    version.source = optional_text(row, "source");
    version.note = optional_text(row, "note");
    version.archived_file_id = row["archived_file_id"].as<std::string>();
    version.original_file_name = row["original_file_name"].as<std::string>();
    version.storage_relative_path = row["storage_relative_path"].as<std::string>();
    version.file_hash = row["file_hash"].as<std::string>();
    version.file_size_bytes = static_cast<std::uintmax_t>(row["file_size_bytes"].as<int64_t>());
    version.uploaded_by = optional_text(row, "uploaded_by");
    version.uploaded_at = row["uploaded_at"].as<std::string>();
    version.updated_at = row["updated_at"].as<std::string>();
    version.created_date = row["created_date"].as<std::string>();
    return version;
}

StandardDocumentRecord read_document(const drogon::orm::Row& row) {
    StandardDocumentRecord document;
    document.id = row["id"].as<std::string>();
    document.name = row["name"].as<std::string>();
    document.english_name = optional_text(row, "english_name");
    document.category = row["category"].as<std::string>();
    return document;
}

/// 可空字段沿用现有写法：传空串，SQL 里 nullif 回 null。
std::string or_empty(const std::optional<std::string>& value) { return value.value_or(""); }

/// 在一个事务里跑 body。body 返回非 Ok 表示业务上不成立，回滚后原样返回；
/// 提交确认丢失时抛 StandardDocumentCommitUncertain。
template <typename Body>
StandardDocumentWriteStatus run_transaction(
    const drogon::orm::DbClientPtr& client, const std::string& context, Body&& body) {
    std::shared_ptr<drogon::orm::Transaction> tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = client->newTransaction(latch->callback());
        const auto status = body(*tx);
        if (status != StandardDocumentWriteStatus::Ok) {
            tx->rollback();
            return status;
        }
        tx.reset();
        if (!latch->wait()) throw StandardDocumentCommitUncertain(context);
        return StandardDocumentWriteStatus::Ok;
    } catch (...) {
        if (tx != nullptr) tx->rollback();
        throw;
    }
}

bool lock_document(drogon::orm::Transaction& tx, const std::string& document_id) {
    return !tx.execSqlSync("select 1 from standard_documents where id = $1::uuid for update", document_id).empty();
}

/// 版本所属的规范；版本不存在时为空。
std::optional<std::string> owner_of(drogon::orm::Transaction& tx, const std::string& version_id) {
    const auto rows = tx.execSqlSync(
        "select document_id::text as document_id from standard_document_versions where id = $1::uuid", version_id);
    if (rows.empty()) return std::nullopt;
    return rows[0]["document_id"].as<std::string>();
}

bool name_taken(drogon::orm::Transaction& tx, const std::string& name, const std::string& except_id) {
    return !tx.execSqlSync("select 1 from standard_documents where name = $1 and id::text <> $2",
                           name, except_id).empty();
}

StandardDocumentWriteStatus version_conflict(
    drogon::orm::Transaction& tx, const std::string& document_id,
    const standards::StandardVersionInput& version, const std::string& except_version_id) {
    if (!tx.execSqlSync(
            "select 1 from standard_document_versions "
            "where document_id = $1::uuid and label = $2 and id::text <> $3",
            document_id, version.label, except_version_id).empty()) {
        return StandardDocumentWriteStatus::LabelTaken;
    }
    if (version.kind == standards::StandardVersionKind::official && version.effective_date.has_value() &&
        !tx.execSqlSync(
            "select 1 from standard_document_versions where document_id = $1::uuid and kind = 'official' "
            "and effective_date = $2::date and id::text <> $3",
            document_id, *version.effective_date, except_version_id).empty()) {
        return StandardDocumentWriteStatus::EffectiveDateTaken;
    }
    return StandardDocumentWriteStatus::Ok;
}

std::string insert_archived_file(drogon::orm::Transaction& tx, const StandardPdfFileInput& file) {
    return tx.execSqlSync(
        "insert into archived_files(original_file_name, current_file_name, storage_relative_path, "
        "file_type, file_purpose, file_extension, file_size_bytes, file_hash, source_description) "
        "values($1, $1, $2, '附件', '规范原文', '.pdf', $3, nullif($4, ''), '规范原文上传') "
        "returning id::text as id",
        file.original_file_name, file.storage_relative_path,
        static_cast<int64_t>(file.file_size_bytes), file.sha256)[0]["id"].as<std::string>();
}

void insert_version(drogon::orm::Transaction& tx, const std::string& document_id,
                    const standards::StandardVersionInput& version, const std::string& archived_file_id,
                    const std::string& user_id) {
    tx.execSqlSync(
        "insert into standard_document_versions(document_id, label, code, kind, nature, issuer, editor, "
        "publish_date, effective_date, withdrawn_date, announcement, source, note, archived_file_id, uploaded_by) "
        "values($1::uuid, $2, $3, $4, nullif($5, ''), nullif($6, ''), nullif($7, ''), "
        "nullif($8, '')::date, nullif($9, '')::date, nullif($10, '')::date, "
        "nullif($11, ''), nullif($12, ''), nullif($13, ''), $14::uuid, nullif($15, '')::uuid)",
        document_id, version.label, version.code,
        std::string(standards::standard_version_kind_code(version.kind)),
        or_empty(version.nature), or_empty(version.issuer), or_empty(version.editor),
        or_empty(version.publish_date), or_empty(version.effective_date), or_empty(version.withdrawn_date),
        or_empty(version.announcement), or_empty(version.source), or_empty(version.note),
        archived_file_id, user_id);
}

/// 版本的归档文件（加锁）。版本不存在时为空。
std::optional<std::pair<std::string, StandardFileToRemove>> version_file_for_update(
    drogon::orm::Transaction& tx, const std::string& version_id) {
    const auto rows = tx.execSqlSync(
        "select v.archived_file_id::text as archived_file_id, f.storage_relative_path, "
        "coalesce(f.file_hash, '') as file_hash from standard_document_versions v "
        "join archived_files f on f.id = v.archived_file_id where v.id = $1::uuid for update of v",
        version_id);
    if (rows.empty()) return std::nullopt;
    return std::make_pair(rows[0]["archived_file_id"].as<std::string>(),
                          StandardFileToRemove{rows[0]["storage_relative_path"].as<std::string>(),
                                               rows[0]["file_hash"].as<std::string>()});
}

}  // namespace

StandardDocumentRepository::StandardDocumentRepository(drogon::orm::DbClientPtr client)
    : client_(std::move(client)) {}

std::string StandardDocumentRepository::allocate_document_id() const {
    return client_->execSqlSync("select gen_random_uuid()::text as id")[0]["id"].as<std::string>();
}

std::vector<StandardDocumentRecord> StandardDocumentRepository::list() const {
    const auto rows = client_->execSqlSync(
        "select id::text as id, name, english_name, category from standard_documents order by category, name");
    std::vector<StandardDocumentRecord> documents;
    std::map<std::string, std::size_t> index;
    for (const auto& row : rows) {
        index[row["id"].as<std::string>()] = documents.size();
        documents.push_back(read_document(row));
    }
    // 一次取全部版本：几十部规范，不值得为每部各跑一趟。
    const auto versions = client_->execSqlSync(std::string("select ") + kVersionColumns + kVersionFrom + kVersionOrder);
    for (const auto& row : versions) {
        auto version = read_version(row);
        const auto found = index.find(version.document_id);
        if (found != index.end()) documents[found->second].versions.push_back(std::move(version));
    }
    return documents;
}

std::optional<StandardDocumentRecord> StandardDocumentRepository::find(const std::string& document_id) const {
    const auto rows = client_->execSqlSync(
        "select id::text as id, name, english_name, category from standard_documents where id = $1::uuid",
        document_id);
    if (rows.empty()) return std::nullopt;
    auto document = read_document(rows[0]);
    const auto versions = client_->execSqlSync(
        std::string("select ") + kVersionColumns + kVersionFrom + " where v.document_id = $1::uuid" + kVersionOrder,
        document_id);
    for (const auto& row : versions) document.versions.push_back(read_version(row));
    return document;
}

std::optional<StandardVersionFile> StandardDocumentRepository::find_version_file(const std::string& version_id) const {
    const auto rows = client_->execSqlSync(
        std::string("select ") + kVersionColumns + ", d.name as document_name" + kVersionFrom +
            " join standard_documents d on d.id = v.document_id where v.id = $1::uuid",
        version_id);
    if (rows.empty()) return std::nullopt;
    return StandardVersionFile{read_version(rows[0]), rows[0]["document_name"].as<std::string>()};
}

StandardDocumentWriteOutcome StandardDocumentRepository::create(
    const std::string& document_id, const standards::StandardDocumentInput& document,
    const standards::StandardVersionInput& version, const StandardPdfFileInput& file,
    const std::string& user_id) const {
    const auto status = run_transaction(
        client_, "create document=" + document_id + " file=" + file.storage_relative_path,
        [&](drogon::orm::Transaction& tx) {
            if (name_taken(tx, document.name, "")) return StandardDocumentWriteStatus::NameTaken;
            tx.execSqlSync(
                "insert into standard_documents(id, name, english_name, category) "
                "values($1::uuid, $2, nullif($3, ''), $4)",
                document_id, document.name, or_empty(document.english_name), document.category);
            insert_version(tx, document_id, version, insert_archived_file(tx, file), user_id);
            return StandardDocumentWriteStatus::Ok;
        });
    if (status != StandardDocumentWriteStatus::Ok) return {status, std::nullopt, {}};
    return {status, find(document_id), {}};
}

StandardDocumentWriteOutcome StandardDocumentRepository::update(
    const std::string& document_id, const standards::StandardDocumentInput& document) const {
    const auto status = run_transaction(client_, "update document=" + document_id, [&](drogon::orm::Transaction& tx) {
        if (!lock_document(tx, document_id)) return StandardDocumentWriteStatus::DocumentNotFound;
        if (name_taken(tx, document.name, document_id)) return StandardDocumentWriteStatus::NameTaken;
        tx.execSqlSync(
            "update standard_documents set name = $2, english_name = nullif($3, ''), category = $4, "
            "updated_at = now() where id = $1::uuid",
            document_id, document.name, or_empty(document.english_name), document.category);
        return StandardDocumentWriteStatus::Ok;
    });
    if (status != StandardDocumentWriteStatus::Ok) return {status, std::nullopt, {}};
    return {status, find(document_id), {}};
}

StandardDocumentWriteOutcome StandardDocumentRepository::remove(const std::string& document_id) const {
    std::vector<StandardFileToRemove> files;
    const auto status = run_transaction(client_, "remove document=" + document_id, [&](drogon::orm::Transaction& tx) {
        if (!lock_document(tx, document_id)) return StandardDocumentWriteStatus::DocumentNotFound;
        const auto rows = tx.execSqlSync(
            "select v.archived_file_id::text as archived_file_id, f.storage_relative_path, "
            "coalesce(f.file_hash, '') as file_hash from standard_document_versions v "
            "join archived_files f on f.id = v.archived_file_id where v.document_id = $1::uuid",
            document_id);
        // 先删规范（级联删版本），再删归档行：版本对归档行是 restrict，反过来会被挡住。
        tx.execSqlSync("delete from standard_documents where id = $1::uuid", document_id);
        for (const auto& row : rows) {
            tx.execSqlSync("delete from archived_files where id = $1::uuid", row["archived_file_id"].as<std::string>());
            files.push_back({row["storage_relative_path"].as<std::string>(), row["file_hash"].as<std::string>()});
        }
        return StandardDocumentWriteStatus::Ok;
    });
    if (status != StandardDocumentWriteStatus::Ok) return {status, std::nullopt, {}};
    return {status, std::nullopt, files};
}

StandardDocumentWriteOutcome StandardDocumentRepository::add_version(
    const std::string& document_id, const standards::StandardVersionInput& version,
    const StandardPdfFileInput& file, const std::string& user_id) const {
    const auto status = run_transaction(
        client_, "add version document=" + document_id + " file=" + file.storage_relative_path,
        [&](drogon::orm::Transaction& tx) {
            if (!lock_document(tx, document_id)) return StandardDocumentWriteStatus::DocumentNotFound;
            const auto conflict = version_conflict(tx, document_id, version, "");
            if (conflict != StandardDocumentWriteStatus::Ok) return conflict;
            insert_version(tx, document_id, version, insert_archived_file(tx, file), user_id);
            return StandardDocumentWriteStatus::Ok;
        });
    if (status != StandardDocumentWriteStatus::Ok) return {status, std::nullopt, {}};
    return {status, find(document_id), {}};
}

StandardDocumentWriteOutcome StandardDocumentRepository::update_version(
    const std::string& version_id, const standards::StandardVersionInput& version) const {
    std::string document_id;
    const auto status = run_transaction(client_, "update version=" + version_id, [&](drogon::orm::Transaction& tx) {
        const auto owner = owner_of(tx, version_id);
        if (!owner.has_value()) return StandardDocumentWriteStatus::VersionNotFound;
        document_id = *owner;
        lock_document(tx, document_id);
        if (!version_file_for_update(tx, version_id).has_value()) return StandardDocumentWriteStatus::VersionNotFound;
        const auto conflict = version_conflict(tx, document_id, version, version_id);
        if (conflict != StandardDocumentWriteStatus::Ok) return conflict;
        tx.execSqlSync(
            "update standard_document_versions set label = $2, code = $3, kind = $4, nature = nullif($5, ''), "
            "issuer = nullif($6, ''), editor = nullif($7, ''), publish_date = nullif($8, '')::date, "
            "effective_date = nullif($9, '')::date, withdrawn_date = nullif($10, '')::date, "
            "announcement = nullif($11, ''), source = nullif($12, ''), note = nullif($13, ''), "
            "updated_at = now() where id = $1::uuid",
            version_id, version.label, version.code,
            std::string(standards::standard_version_kind_code(version.kind)),
            or_empty(version.nature), or_empty(version.issuer), or_empty(version.editor),
            or_empty(version.publish_date), or_empty(version.effective_date), or_empty(version.withdrawn_date),
            or_empty(version.announcement), or_empty(version.source), or_empty(version.note));
        return StandardDocumentWriteStatus::Ok;
    });
    if (status != StandardDocumentWriteStatus::Ok) return {status, std::nullopt, {}};
    return {status, find(document_id), {}};
}

StandardDocumentWriteOutcome StandardDocumentRepository::replace_version_file(
    const std::string& version_id, const StandardPdfFileInput& file, const std::string& user_id) const {
    std::string document_id;
    std::vector<StandardFileToRemove> files;
    const auto status = run_transaction(
        client_, "replace version=" + version_id + " new_file=" + file.storage_relative_path,
        [&](drogon::orm::Transaction& tx) {
            const auto owner = owner_of(tx, version_id);
            if (!owner.has_value()) return StandardDocumentWriteStatus::VersionNotFound;
            document_id = *owner;
            lock_document(tx, document_id);
            const auto previous = version_file_for_update(tx, version_id);
            if (!previous.has_value()) return StandardDocumentWriteStatus::VersionNotFound;
            const auto new_file_id = insert_archived_file(tx, file);
            tx.execSqlSync(
                "update standard_document_versions set archived_file_id = $2::uuid, "
                "uploaded_by = nullif($3, '')::uuid, updated_at = now() where id = $1::uuid",
                version_id, new_file_id, user_id);
            tx.execSqlSync("delete from archived_files where id = $1::uuid", previous->first);
            files.push_back(previous->second);
            return StandardDocumentWriteStatus::Ok;
        });
    if (status != StandardDocumentWriteStatus::Ok) return {status, std::nullopt, {}};
    return {status, find(document_id), files};
}

StandardDocumentWriteOutcome StandardDocumentRepository::remove_version(const std::string& version_id) const {
    std::string document_id;
    std::vector<StandardFileToRemove> files;
    const auto status = run_transaction(client_, "remove version=" + version_id, [&](drogon::orm::Transaction& tx) {
        const auto owner = owner_of(tx, version_id);
        if (!owner.has_value()) return StandardDocumentWriteStatus::VersionNotFound;
        document_id = *owner;
        lock_document(tx, document_id);
        const auto previous = version_file_for_update(tx, version_id);
        if (!previous.has_value()) return StandardDocumentWriteStatus::VersionNotFound;
        tx.execSqlSync("delete from standard_document_versions where id = $1::uuid", version_id);
        tx.execSqlSync("delete from archived_files where id = $1::uuid", previous->first);
        files.push_back(previous->second);
        return StandardDocumentWriteStatus::Ok;
    });
    if (status != StandardDocumentWriteStatus::Ok) return {status, std::nullopt, {}};
    return {status, find(document_id), files};
}

}  // namespace bridge_report::db
```

- [ ] **步骤 5：构建并跑全量测试（含数据库），确认通过**

运行：
`cmake --build backend-cpp/build/vs-debug --config Debug --target bridge_report_backend_tests`
`powershell -ExecutionPolicy Bypass -File scripts/dev/check-backend-tests.ps1`
预期：脚本最后打印 `Isolated backend check passed ...`；`StandardDocumentRepositoryTest.*` 的 10 个用例全部 PASS，没有被跳过。

- [ ] **步骤 6：提交**

```bash
git add -- backend-cpp/include/bridge_report/db/StandardDocumentRepository.hpp backend-cpp/src/db/StandardDocumentRepository.cpp backend-cpp/tests/test_standard_document_repository.cpp
```

提交信息：`feat(db): 规范原文库仓储`

---

### 任务 5：接口与路由注册

**文件：**
- 新建：`backend-cpp/include/bridge_report/http/StandardDocumentRoutes.hpp`
- 新建：`backend-cpp/src/http/StandardDocumentRoutes.cpp`
- 新建：`backend-cpp/tests/test_standard_document_routes.cpp`
- 修改：`backend-cpp/include/bridge_report/http/RouteHelpers.hpp`
- 修改（共享）：`backend-cpp/src/main.cpp`、`backend-cpp/CMakeLists.txt`

**接口：**
- 使用：任务 2–4 的全部产出；`authenticate_request`（`AuthRoutes.hpp`）；`respond_json`、`make_error_body`、`respond_unauthorized`、`respond_forbidden`、`respond_db_unavailable`、`register_options_handler`、`is_valid_uuid`、`apply_local_dev_cors_headers`（`RouteHelpers.hpp` 与 `Cors.hpp`）。
- 产出（命名空间 `bridge_report::http`）：
  - `std::string content_disposition(std::string_view kind, const std::string& filename, const std::string& ascii_fallback = "download")`
  - `std::vector<standards::StandardInputIssue> parse_create_standard_metadata(const std::string& text, standards::StandardDocumentInput&, standards::StandardVersionInput&)`
  - `std::vector<standards::StandardInputIssue> parse_standard_version_metadata(const std::string& text, standards::StandardVersionInput&)`
  - `struct StandardWriteError { std::string code; std::string message; drogon::HttpStatusCode status; }`
  - `std::optional<StandardWriteError> standard_write_error(db::StandardDocumentWriteStatus)`
  - `void register_standard_document_routes(const drogon::orm::DbClientPtr&, const config::AppConfig&)`
  - HTTP 接口见设计 §7.1–§7.3。

- [ ] **步骤 1：写失败的测试**

`backend-cpp/tests/test_standard_document_routes.cpp`：

```cpp
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/http/RouteHelpers.hpp"
#include "bridge_report/http/StandardDocumentRoutes.hpp"

namespace {

using namespace bridge_report;

TEST(StandardDocumentRoutesTest, CreateMetadataReportsIssuesWithSectionPrefixes) {
    standards::StandardDocumentInput document;
    standards::StandardVersionInput version;
    const auto issues = http::parse_create_standard_metadata(
        R"({"document": {"name": "", "category": "技术状况评定"},)"
        R"( "version": {"label": "2011 版", "code": "JTG/T H21—2011", "kind": "official"}})",
        document, version);
    ASSERT_EQ(issues.size(), 2u);
    EXPECT_EQ(issues[0].path, "document.name");
    EXPECT_EQ(issues[1].path, "version.effective_date");
}

TEST(StandardDocumentRoutesTest, RejectsMetadataThatIsNotJson) {
    standards::StandardVersionInput version;
    const auto issues = http::parse_standard_version_metadata("not json", version);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].path, "metadata");
}

TEST(StandardDocumentRoutesTest, AcceptsADraftVersion) {
    standards::StandardVersionInput version;
    EXPECT_TRUE(http::parse_standard_version_metadata(
        R"({"label": "2026 报批稿", "code": "JTG/T 52XX—2026", "kind": "draft_approval", "publish_date": "2026-02-11"})",
        version).empty());
    EXPECT_EQ(version.kind, standards::StandardVersionKind::draft_approval);
}

TEST(StandardDocumentRoutesTest, MapsWriteConflictsToHttpErrors) {
    EXPECT_FALSE(http::standard_write_error(db::StandardDocumentWriteStatus::Ok).has_value());
    const auto taken = http::standard_write_error(db::StandardDocumentWriteStatus::NameTaken);
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(taken->code, "standard_document_name_taken");
    EXPECT_EQ(taken->status, drogon::k409Conflict);
    EXPECT_EQ(http::standard_write_error(db::StandardDocumentWriteStatus::VersionNotFound)->status,
              drogon::k404NotFound);
    EXPECT_EQ(http::standard_write_error(db::StandardDocumentWriteStatus::EffectiveDateTaken)->code,
              "standard_version_effective_date_taken");
}

TEST(StandardDocumentRoutesTest, InlineDispositionEncodesTheChineseName) {
    const auto header = http::content_disposition("inline", "JTG T H21—2011 公路.pdf", "standard.pdf");
    EXPECT_EQ(header.rfind("inline; filename=\"standard.pdf\"; filename*=UTF-8''JTG%20T%20H21%E2%80%942011%20", 0), 0u);
    EXPECT_EQ(http::attachment_disposition("a.pdf").rfind("attachment; filename=\"download\"", 0), 0u);
}

}  // namespace
```

`backend-cpp/CMakeLists.txt`（共享，只加两行）：
- 在 `    src/http/StandardRoutes.cpp` 之后加 `    src/http/StandardDocumentRoutes.cpp`
- 在 `    tests/test_standard_document_repository.cpp` 之后加 `    tests/test_standard_document_routes.cpp`

- [ ] **步骤 2：构建，确认失败**

运行：`cmake --build backend-cpp/build/vs-debug --config Debug --target bridge_report_backend_tests`
预期：编译失败，找不到 `bridge_report/http/StandardDocumentRoutes.hpp`。

- [ ] **步骤 3：把 `attachment_disposition` 扩展出 inline**

在 `backend-cpp/include/bridge_report/http/RouteHelpers.hpp` 里，把整个 `attachment_disposition` 函数（含上方注释）替换为：

```cpp
/**
 * @brief 文件响应的 Content-Disposition，中文文件名走 RFC 5987 的 filename*。
 *
 * 只写 `filename=` 会被大多数浏览器按 latin-1 解释，`百股大桥定期检测报告.docx`
 * 存下来就是一串乱码；两个都写，老客户端用前者、新客户端用后者。前一个退化成
 * ASCII 占位，真正的名字由 filename* 给出。
 *
 * kind 为 "attachment"（下载）或 "inline"（在浏览器里直接打开，例如 iframe 里的 PDF）。
 *
 * 注意跨域时还要把 Content-Disposition 放进 Access-Control-Expose-Headers，
 * 否则前端用 fetch 取 blob 根本读不到这个头（见 Cors.cpp）。
 */
inline std::string content_disposition(
    std::string_view kind, const std::string& filename, const std::string& ascii_fallback = "download") {
    std::ostringstream encoded;
    encoded << std::hex << std::uppercase;
    for (const unsigned char character : filename) {
        const bool unreserved =
            (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_' || character == '.' || character == '~';
        if (unreserved) {
            encoded << static_cast<char>(character);
        } else {
            encoded << '%' << (character < 0x10 ? "0" : "") << static_cast<int>(character);
        }
    }
    return std::string(kind) + "; filename=\"" + ascii_fallback + "\"; filename*=UTF-8''" + encoded.str();
}

inline std::string attachment_disposition(
    const std::string& filename, const std::string& ascii_fallback = "download") {
    return content_disposition("attachment", filename, ascii_fallback);
}
```

（`string_view` 已经在这个头文件里 `#include <string_view>`，不用另加。）

- [ ] **步骤 4：写路由头文件**

`backend-cpp/include/bridge_report/http/StandardDocumentRoutes.hpp`：

```cpp
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/HttpTypes.h>
#include <drogon/orm/DbClient.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/StandardDocumentRepository.hpp"
#include "bridge_report/standards/StandardDocumentModels.hpp"

namespace bridge_report::http {

/// 新增规范的 metadata：`{"document": {...}, "version": {...}}`，问题路径带 document./version. 前缀。
std::vector<standards::StandardInputIssue> parse_create_standard_metadata(
    const std::string& text,
    standards::StandardDocumentInput& document,
    standards::StandardVersionInput& version);

/// 上传新版本的 metadata：版本字段本身。
std::vector<standards::StandardInputIssue> parse_standard_version_metadata(
    const std::string& text, standards::StandardVersionInput& version);

struct StandardWriteError {
    std::string code;
    std::string message;
    drogon::HttpStatusCode status{drogon::k400BadRequest};
};

/// 仓储层的业务结论映射到 HTTP 错误；Ok 返回空。
std::optional<StandardWriteError> standard_write_error(db::StandardDocumentWriteStatus status);

/**
 * @brief 规范原文库的接口（设计 §7）。
 *
 * 列表要登录，写接口只给管理员。原文内容那条不鉴权：iframe 带不上 Authorization 头，
 * 和桥梁图件、病害照片的内容路由一样；规范原文本身是公开文件，版本 id 是随机 UUID。
 */
void register_standard_document_routes(
    const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config);

}  // namespace bridge_report::http
```

- [ ] **步骤 5：写路由实现**

`backend-cpp/src/http/StandardDocumentRoutes.cpp`：

```cpp
#include "bridge_report/http/StandardDocumentRoutes.hpp"

#include <filesystem>
#include <sstream>

#include <drogon/drogon.h>

#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/archive/StandardDocumentArchive.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {
namespace {

using db::StandardDocumentRepository;
using db::StandardDocumentWriteOutcome;
using db::StandardDocumentWriteStatus;

bool parse_json_text(const std::string& text, Json::Value& body) {
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(text);
    return Json::parseFromStream(builder, stream, &body, &errors) && body.isObject();
}

void respond_invalid(const HttpCallback& callback, const std::vector<standards::StandardInputIssue>& issues) {
    auto body = make_error_body("standard_document_invalid", "规范信息有误，请按提示修改。");
    body["issues"] = standards::standard_input_issues_json(issues);
    respond_json(callback, body, drogon::k400BadRequest);
}

void respond_error(const HttpCallback& callback, const StandardWriteError& error) {
    respond_json(callback, make_error_body(error.code, error.message), error.status);
}

void respond_document_not_found(const HttpCallback& callback) {
    respond_error(callback, *standard_write_error(StandardDocumentWriteStatus::DocumentNotFound));
}

void respond_version_not_found(const HttpCallback& callback) {
    respond_error(callback, *standard_write_error(StandardDocumentWriteStatus::VersionNotFound));
}

void respond_commit_uncertain(const HttpCallback& callback, const std::string& detail) {
    LOG_ERROR << "Standard document commit requires reconciliation; files retained: " << detail;
    respond_json(callback,
        make_error_body("standard_document_commit_uncertain",
                        "保存结果暂时无法确认，相关文件已保留。请刷新核实后再操作。"),
        drogon::k503ServiceUnavailable);
}

/// 没登录 401，登录了但不是管理员 403。
std::optional<db::AuthUser> require_admin(
    const drogon::orm::DbClientPtr& db_client, const drogon::HttpRequestPtr& request,
    const HttpCallback& callback) {
    const auto user = authenticate_request(db_client, request);
    if (!user.has_value()) {
        respond_unauthorized(callback);
        return std::nullopt;
    }
    if (!user->is_admin()) {
        respond_forbidden(callback);
        return std::nullopt;
    }
    return user;
}

/// multipart 里唯一一个名为 file 的文件；没有就回 standard_file_required。
const drogon::HttpFile* single_file(const drogon::MultiPartParser& parser, const HttpCallback& callback) {
    const auto& files = parser.getFiles();
    if (files.size() != 1 || files[0].getItemName() != "file") {
        respond_json(callback, make_error_body("standard_file_required", "请上传一个 PDF 文件。"),
                     drogon::k400BadRequest);
        return nullptr;
    }
    return &files[0];
}

/// 写盘；失败时已经回应，返回空。
std::optional<archive::ArchivedStandardPdf> archive_upload(
    const drogon::HttpFile& file, const config::AppConfig& config, const std::string& document_id,
    const HttpCallback& callback) {
    try {
        return archive::archive_standard_pdf(file.fileContent(), file.getFileName(), config.archive_root, document_id);
    } catch (const archive::StandardDocumentEmptyError&) {
        respond_json(callback, make_error_body("standard_file_required", "上传的文件是空的。"), drogon::k400BadRequest);
    } catch (const archive::StandardDocumentNotPdfError&) {
        respond_json(callback, make_error_body("standard_file_not_pdf", "只能上传 PDF 文件。"), drogon::k400BadRequest);
    } catch (const std::exception&) {
        respond_json(callback, make_error_body("standard_file_store_failed", "原文写入归档失败，请稍后重试。"),
                     drogon::k500InternalServerError);
    }
    return std::nullopt;
}

db::StandardPdfFileInput file_input(const archive::ArchivedStandardPdf& archived) {
    return {archived.original_file_name, archived.storage_relative_path, archived.file_size_bytes, archived.sha256};
}

void remove_new_file(const config::AppConfig& config, const archive::ArchivedStandardPdf& archived) {
    archive::remove_archived_standard_pdf(config.archive_root, archived.storage_relative_path, archived.sha256);
}

/// 写操作的统一收尾：失败时删掉这次新写的文件；成功时提交后删旧文件，回最新的规范。
void finish_write(const config::AppConfig& config, const HttpCallback& callback,
                  const StandardDocumentWriteOutcome& outcome,
                  const std::optional<archive::ArchivedStandardPdf>& new_file,
                  drogon::HttpStatusCode success_status) {
    if (const auto error = standard_write_error(outcome.status); error.has_value()) {
        if (new_file.has_value()) remove_new_file(config, *new_file);
        respond_error(callback, *error);
        return;
    }
    for (const auto& file : outcome.files_to_remove) {
        archive::remove_archived_standard_pdf(config.archive_root, file.storage_relative_path, file.sha256);
    }
    Json::Value body;
    body["document"] = outcome.document.has_value()
        ? standards::standard_document_json(*outcome.document, standards::local_today())
        : Json::Value(Json::nullValue);
    respond_json(callback, body, success_status);
}

void handle_list(const drogon::orm::DbClientPtr& db_client, const drogon::HttpRequestPtr& request,
                 const HttpCallback& callback) {
    if (!authenticate_request(db_client, request).has_value()) {
        respond_unauthorized(callback);
        return;
    }
    const auto today = standards::local_today();
    Json::Value body;
    body["documents"] = Json::Value(Json::arrayValue);
    for (const auto& document : StandardDocumentRepository(db_client).list()) {
        body["documents"].append(standards::standard_document_json(document, today));
    }
    respond_json(callback, body);
}

void handle_create(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                   const drogon::HttpRequestPtr& request, const HttpCallback& callback) {
    const auto user = require_admin(db_client, request, callback);
    if (!user.has_value()) return;

    drogon::MultiPartParser parser;
    if (parser.parse(request) != 0) {
        respond_invalid(callback, {{"metadata", "上传内容不是合法的 multipart 表单。"}});
        return;
    }
    standards::StandardDocumentInput document;
    standards::StandardVersionInput version;
    const auto issues = parse_create_standard_metadata(parser.getParameter<std::string>("metadata"), document, version);
    if (!issues.empty()) {
        respond_invalid(callback, issues);
        return;
    }
    const auto* file = single_file(parser, callback);
    if (file == nullptr) return;

    const StandardDocumentRepository repository(db_client);
    const auto document_id = repository.allocate_document_id();
    const auto archived = archive_upload(*file, config, document_id, callback);
    if (!archived.has_value()) return;

    StandardDocumentWriteOutcome outcome;
    try {
        outcome = repository.create(document_id, document, version, file_input(*archived), user->id);
    } catch (const db::StandardDocumentCommitUncertain& error) {
        respond_commit_uncertain(callback, error.what());
        return;
    } catch (...) {
        remove_new_file(config, *archived);
        throw;
    }
    finish_write(config, callback, outcome, archived, drogon::k201Created);
}

void handle_update(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                   const drogon::HttpRequestPtr& request, const HttpCallback& callback,
                   const std::string& document_id) {
    if (!is_valid_uuid(document_id)) {
        respond_document_not_found(callback);
        return;
    }
    if (!require_admin(db_client, request, callback).has_value()) return;
    const auto json = request->getJsonObject();
    standards::StandardDocumentInput document;
    const auto issues = json != nullptr
        ? standards::parse_standard_document_input(*json, document)
        : std::vector<standards::StandardInputIssue>{{"body", "请求体必须是 JSON 对象。"}};
    if (!issues.empty()) {
        respond_invalid(callback, issues);
        return;
    }
    StandardDocumentWriteOutcome outcome;
    try {
        outcome = StandardDocumentRepository(db_client).update(document_id, document);
    } catch (const db::StandardDocumentCommitUncertain& error) {
        respond_commit_uncertain(callback, error.what());
        return;
    }
    finish_write(config, callback, outcome, std::nullopt, drogon::k200OK);
}

void handle_remove(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                   const drogon::HttpRequestPtr& request, const HttpCallback& callback,
                   const std::string& document_id) {
    if (!is_valid_uuid(document_id)) {
        respond_document_not_found(callback);
        return;
    }
    if (!require_admin(db_client, request, callback).has_value()) return;
    StandardDocumentWriteOutcome outcome;
    try {
        outcome = StandardDocumentRepository(db_client).remove(document_id);
    } catch (const db::StandardDocumentCommitUncertain& error) {
        respond_commit_uncertain(callback, error.what());
        return;
    }
    if (const auto error = standard_write_error(outcome.status); error.has_value()) {
        respond_error(callback, *error);
        return;
    }
    for (const auto& file : outcome.files_to_remove) {
        archive::remove_archived_standard_pdf(config.archive_root, file.storage_relative_path, file.sha256);
    }
    // 不用 204：前端 request() 一律按 JSON 解析响应体。
    Json::Value body;
    body["deleted"] = true;
    respond_json(callback, body);
}

void handle_add_version(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                        const drogon::HttpRequestPtr& request, const HttpCallback& callback,
                        const std::string& document_id) {
    if (!is_valid_uuid(document_id)) {
        respond_document_not_found(callback);
        return;
    }
    const auto user = require_admin(db_client, request, callback);
    if (!user.has_value()) return;

    drogon::MultiPartParser parser;
    if (parser.parse(request) != 0) {
        respond_invalid(callback, {{"metadata", "上传内容不是合法的 multipart 表单。"}});
        return;
    }
    standards::StandardVersionInput version;
    const auto issues = parse_standard_version_metadata(parser.getParameter<std::string>("metadata"), version);
    if (!issues.empty()) {
        respond_invalid(callback, issues);
        return;
    }
    const auto* file = single_file(parser, callback);
    if (file == nullptr) return;
    const auto archived = archive_upload(*file, config, document_id, callback);
    if (!archived.has_value()) return;

    StandardDocumentWriteOutcome outcome;
    try {
        outcome = StandardDocumentRepository(db_client).add_version(document_id, version, file_input(*archived), user->id);
    } catch (const db::StandardDocumentCommitUncertain& error) {
        respond_commit_uncertain(callback, error.what());
        return;
    } catch (...) {
        remove_new_file(config, *archived);
        throw;
    }
    finish_write(config, callback, outcome, archived, drogon::k201Created);
}

void handle_update_version(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                           const drogon::HttpRequestPtr& request, const HttpCallback& callback,
                           const std::string& version_id) {
    if (!is_valid_uuid(version_id)) {
        respond_version_not_found(callback);
        return;
    }
    if (!require_admin(db_client, request, callback).has_value()) return;
    const auto json = request->getJsonObject();
    standards::StandardVersionInput version;
    const auto issues = json != nullptr
        ? standards::parse_standard_version_input(*json, version)
        : std::vector<standards::StandardInputIssue>{{"body", "请求体必须是 JSON 对象。"}};
    if (!issues.empty()) {
        respond_invalid(callback, issues);
        return;
    }
    StandardDocumentWriteOutcome outcome;
    try {
        outcome = StandardDocumentRepository(db_client).update_version(version_id, version);
    } catch (const db::StandardDocumentCommitUncertain& error) {
        respond_commit_uncertain(callback, error.what());
        return;
    }
    finish_write(config, callback, outcome, std::nullopt, drogon::k200OK);
}

void handle_replace_file(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                         const drogon::HttpRequestPtr& request, const HttpCallback& callback,
                         const std::string& version_id) {
    if (!is_valid_uuid(version_id)) {
        respond_version_not_found(callback);
        return;
    }
    const auto user = require_admin(db_client, request, callback);
    if (!user.has_value()) return;

    drogon::MultiPartParser parser;
    if (parser.parse(request) != 0) {
        respond_invalid(callback, {{"file", "上传内容不是合法的 multipart 表单。"}});
        return;
    }
    const auto* file = single_file(parser, callback);
    if (file == nullptr) return;

    const StandardDocumentRepository repository(db_client);
    const auto current = repository.find_version_file(version_id);
    if (!current.has_value()) {
        respond_version_not_found(callback);
        return;
    }
    const auto archived = archive_upload(*file, config, current->version.document_id, callback);
    if (!archived.has_value()) return;

    StandardDocumentWriteOutcome outcome;
    try {
        outcome = repository.replace_version_file(version_id, file_input(*archived), user->id);
    } catch (const db::StandardDocumentCommitUncertain& error) {
        respond_commit_uncertain(callback, error.what());
        return;
    } catch (...) {
        remove_new_file(config, *archived);
        throw;
    }
    finish_write(config, callback, outcome, archived, drogon::k200OK);
}

void handle_remove_version(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                           const drogon::HttpRequestPtr& request, const HttpCallback& callback,
                           const std::string& version_id) {
    if (!is_valid_uuid(version_id)) {
        respond_version_not_found(callback);
        return;
    }
    if (!require_admin(db_client, request, callback).has_value()) return;
    StandardDocumentWriteOutcome outcome;
    try {
        outcome = StandardDocumentRepository(db_client).remove_version(version_id);
    } catch (const db::StandardDocumentCommitUncertain& error) {
        respond_commit_uncertain(callback, error.what());
        return;
    }
    finish_write(config, callback, outcome, std::nullopt, drogon::k200OK);
}

void handle_content(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                    const drogon::HttpRequestPtr& request, const HttpCallback& callback,
                    const std::string& version_id) {
    if (!is_valid_uuid(version_id)) {
        respond_version_not_found(callback);
        return;
    }
    const auto found = StandardDocumentRepository(db_client).find_version_file(version_id);
    if (!found.has_value()) {
        respond_version_not_found(callback);
        return;
    }
    const auto respond_missing = [&callback] {
        respond_json(callback, make_error_body("standard_file_missing", "原文文件在归档里找不到，请管理员重新上传。"),
                     drogon::k409Conflict);
    };
    const std::filesystem::path relative(found->version.storage_relative_path);
    if (!archive::is_safe_archive_relative_path(relative)) {
        respond_missing();
        return;
    }
    const auto absolute = archive::resolve_path_under_root(config.archive_root, relative);
    if (!std::filesystem::is_regular_file(absolute)) {
        respond_missing();
        return;
    }
    // 传入 request：drogon 据此处理 Range，大文件可以边下边看。
    auto response = drogon::HttpResponse::newFileResponse(
        absolute.string(), "", drogon::CT_CUSTOM, "application/pdf", request);
    const bool download = request->getParameter("download") == "1";
    response->addHeader("Content-Disposition",
        content_disposition(download ? "attachment" : "inline",
                            standards::standard_pdf_file_name(found->version.code, found->document_name),
                            "standard.pdf"));
    apply_local_dev_cors_headers(response);
    callback(response);
}

}  // namespace

std::vector<standards::StandardInputIssue> parse_create_standard_metadata(
    const std::string& text, standards::StandardDocumentInput& document, standards::StandardVersionInput& version) {
    Json::Value body;
    if (!parse_json_text(text, body)) return {{"metadata", "metadata 不是合法的 JSON 对象。"}};
    auto issues = standards::parse_standard_document_input(body["document"], document, "document.");
    const auto version_issues = standards::parse_standard_version_input(body["version"], version, "version.");
    issues.insert(issues.end(), version_issues.begin(), version_issues.end());
    return issues;
}

std::vector<standards::StandardInputIssue> parse_standard_version_metadata(
    const std::string& text, standards::StandardVersionInput& version) {
    Json::Value body;
    if (!parse_json_text(text, body)) return {{"metadata", "metadata 不是合法的 JSON 对象。"}};
    return standards::parse_standard_version_input(body, version);
}

std::optional<StandardWriteError> standard_write_error(db::StandardDocumentWriteStatus status) {
    switch (status) {
        case StandardDocumentWriteStatus::Ok:
            return std::nullopt;
        case StandardDocumentWriteStatus::DocumentNotFound:
            return StandardWriteError{"standard_document_not_found", "规范不存在或已被删除。", drogon::k404NotFound};
        case StandardDocumentWriteStatus::VersionNotFound:
            return StandardWriteError{"standard_version_not_found", "版本不存在或已被删除。", drogon::k404NotFound};
        case StandardDocumentWriteStatus::NameTaken:
            return StandardWriteError{"standard_document_name_taken", "已有同名规范。", drogon::k409Conflict};
        case StandardDocumentWriteStatus::LabelTaken:
            return StandardWriteError{"standard_version_label_taken", "这部规范里已有同名版本。", drogon::k409Conflict};
        case StandardDocumentWriteStatus::EffectiveDateTaken:
            return StandardWriteError{"standard_version_effective_date_taken",
                                      "这部规范里已有同一实施日期的正式版本。", drogon::k409Conflict};
    }
    return std::nullopt;
}

void register_standard_document_routes(
    const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config) {
    const std::string collection = "/api/standard-documents";
    const std::string document_path = "/api/standard-documents/{document_id}";
    const std::string versions_path = "/api/standard-documents/{document_id}/versions";
    const std::string version_path = "/api/standard-document-versions/{version_id}";
    const std::string file_path = "/api/standard-document-versions/{version_id}/file";
    const std::string content_path = "/api/standard-document-versions/{version_id}/content";
    for (const auto& path : {collection, document_path, versions_path, version_path, file_path, content_path}) {
        register_options_handler(path);
    }

    drogon::app().registerHandler(
        collection,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                handle_list(db_client, request, callback);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        collection,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                handle_create(db_client, config, request, callback);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        document_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& document_id) {
            try {
                handle_update(db_client, config, request, callback, document_id);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Patch});

    drogon::app().registerHandler(
        document_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& document_id) {
            try {
                handle_remove(db_client, config, request, callback, document_id);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Delete});

    drogon::app().registerHandler(
        versions_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& document_id) {
            try {
                handle_add_version(db_client, config, request, callback, document_id);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        version_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& version_id) {
            try {
                handle_update_version(db_client, config, request, callback, version_id);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Patch});

    drogon::app().registerHandler(
        version_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& version_id) {
            try {
                handle_remove_version(db_client, config, request, callback, version_id);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Delete});

    drogon::app().registerHandler(
        file_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& version_id) {
            try {
                handle_replace_file(db_client, config, request, callback, version_id);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Put});

    drogon::app().registerHandler(
        content_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& version_id) {
            try {
                handle_content(db_client, config, request, callback, version_id);
            } catch (const std::filesystem::filesystem_error&) {
                respond_json(callback, make_error_body("standard_file_missing", "原文文件无法读取。"),
                             drogon::k409Conflict);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});
}

}  // namespace bridge_report::http
```

- [ ] **步骤 6：在 `main.cpp` 注册（共享文件，只加两行）**

- 在 `#include "bridge_report/http/StandardRoutes.hpp"` 之后加：`#include "bridge_report/http/StandardDocumentRoutes.hpp"`
- 在 `    bridge_report::http::register_standard_routes(db_client, standards.registry);` 之后加：`    bridge_report::http::register_standard_document_routes(db_client, config);`

- [ ] **步骤 7：构建并跑测试，确认通过**

运行：
`cmake --build backend-cpp/build/vs-debug --config Debug --target bridge_report_backend_tests bridge-report-backend`
`backend-cpp/build/vs-debug/Debug/bridge_report_backend_tests.exe --gtest_filter=StandardDocument*`
预期：`StandardDocumentRoutesTest.*` 5 个用例 PASS，前面任务的用例仍全部 PASS；`bridge-report-backend` 能链接成功。全量回归放到任务 11 一起跑。

- [ ] **步骤 8：提交（`main.cpp`、`CMakeLists.txt` 留到任务 12）**

```bash
git add -- backend-cpp/include/bridge_report/http/StandardDocumentRoutes.hpp backend-cpp/src/http/StandardDocumentRoutes.cpp backend-cpp/tests/test_standard_document_routes.cpp backend-cpp/include/bridge_report/http/RouteHelpers.hpp
```

提交信息：`feat(http): 规范原文库接口与原文下发`

---

### 任务 6：前端接口模块

**文件：**
- 新建：`frontend/src/api/standardDocumentsApi.ts`
- 新建：`frontend/src/api/standardDocumentsApi.test.ts`

**接口：**
- 使用：`request`、`ApiError`（`./apiClient`）。
- 产出：
  - 类型：`StandardVersionKind`、`StandardVersionStatus`、`StandardVersion`、`StandardDocument`、`StandardDocumentInput`、`StandardVersionInput`
  - 常量：`STANDARD_PDF_MAX_BYTES`
  - 函数（都接收 `baseUrl: string`）：
    - `fetchStandardDocuments(baseUrl)`
    - `createStandardDocument(baseUrl, document, version, file)`、`updateStandardDocument(baseUrl, documentId, document)`、`deleteStandardDocument(baseUrl, documentId)`
    - `addStandardVersion(baseUrl, documentId, version, file)`、`updateStandardVersion(baseUrl, versionId, version)`
    - `replaceStandardVersionFile(baseUrl, versionId, file)`、`deleteStandardVersion(baseUrl, versionId)`
    - `standardVersionContentUrl(baseUrl, version, { download?, page? })`、`standardDocumentsError(error)`

- [ ] **步骤 1：写失败的测试**

`frontend/src/api/standardDocumentsApi.test.ts`：

```ts
import { afterEach, describe, expect, it, vi } from "vitest";

import { ApiError } from "./apiClient";
import {
  addStandardVersion,
  createStandardDocument,
  deleteStandardDocument,
  deleteStandardVersion,
  fetchStandardDocuments,
  replaceStandardVersionFile,
  standardDocumentsError,
  standardVersionContentUrl,
  updateStandardDocument,
  updateStandardVersion,
  type StandardVersion,
  type StandardVersionInput,
} from "./standardDocumentsApi";

const versionInput: StandardVersionInput = {
  label: "2011 版",
  code: "JTG/T H21—2011",
  kind: "official",
  nature: null,
  issuer: "中华人民共和国交通运输部",
  editor: null,
  publish_date: "2011-07-19",
  effective_date: "2011-09-01",
  withdrawn_date: null,
  announcement: null,
  source: null,
  note: null,
};

function respondWith(body: unknown) {
  const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 200, json: async () => body });
  vi.stubGlobal("fetch", fetchMock);
  return fetchMock;
}

describe("standardDocumentsApi", () => {
  afterEach(() => {
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
  });

  it("lists every standard with its versions in one request", async () => {
    const fetchMock = respondWith({ documents: [{ id: "h21" }] });
    await expect(fetchStandardDocuments("http://backend")).resolves.toEqual([{ id: "h21" }]);
    expect(fetchMock).toHaveBeenCalledWith("http://backend/api/standard-documents");
  });

  it("creates a standard with its first version as multipart metadata plus the PDF", async () => {
    const fetchMock = respondWith({ document: { id: "h21" } });
    const file = new File(["%PDF-1.4"], "h21.pdf", { type: "application/pdf" });
    const document = { name: "公路桥梁技术状况评定标准", english_name: null, category: "技术状况评定" };

    await expect(createStandardDocument("http://backend", document, versionInput, file)).resolves.toEqual({ id: "h21" });

    const [url, init] = fetchMock.mock.calls[0];
    expect(url).toBe("http://backend/api/standard-documents");
    expect(init.method).toBe("POST");
    const form = init.body as FormData;
    expect(JSON.parse(form.get("metadata") as string)).toEqual({ document, version: versionInput });
    expect((form.get("file") as File).name).toBe("h21.pdf");
  });

  it("edits, replaces and deletes with the matching methods and encoded ids", async () => {
    const fetchMock = respondWith({ document: { id: "h21" }, deleted: true });
    const file = new File(["%PDF-1.4"], "new.pdf", { type: "application/pdf" });

    await updateStandardDocument("http://backend", "a/b", { name: "n", english_name: null, category: "c" });
    await deleteStandardDocument("http://backend", "a/b");
    await addStandardVersion("http://backend", "a/b", versionInput, file);
    await updateStandardVersion("http://backend", "v/1", versionInput);
    await replaceStandardVersionFile("http://backend", "v/1", file);
    await deleteStandardVersion("http://backend", "v/1");

    expect(fetchMock.mock.calls.map(([url, init]) => [url, init.method])).toEqual([
      ["http://backend/api/standard-documents/a%2Fb", "PATCH"],
      ["http://backend/api/standard-documents/a%2Fb", "DELETE"],
      ["http://backend/api/standard-documents/a%2Fb/versions", "POST"],
      ["http://backend/api/standard-document-versions/v%2F1", "PATCH"],
      ["http://backend/api/standard-document-versions/v%2F1/file", "PUT"],
      ["http://backend/api/standard-document-versions/v%2F1", "DELETE"],
    ]);
    expect(fetchMock.mock.calls[0][1].headers).toEqual({ "Content-Type": "application/json" });
  });

  it("builds content URLs that change when the file is replaced", () => {
    const version = {
      content_path: "/api/standard-document-versions/v1/content",
      updated_at: "2026-10-09 10:00:00",
    } as StandardVersion;
    expect(standardVersionContentUrl("http://backend/", version)).toBe(
      "http://backend/api/standard-document-versions/v1/content?v=2026-10-09+10%3A00%3A00",
    );
    expect(standardVersionContentUrl("http://backend", version, { download: true, page: 16 })).toBe(
      "http://backend/api/standard-document-versions/v1/content?v=2026-10-09+10%3A00%3A00&download=1#page=16",
    );
  });

  it("explains failures in plain language", () => {
    expect(standardDocumentsError(new ApiError("standard_version_label_taken", "x"))).toBe("这部规范里已有同名版本。");
    expect(standardDocumentsError(new ApiError("unrecognized_error_response", "请求失败（HTTP 413）"))).toBe(
      "文件超过了上传上限。",
    );
    expect(standardDocumentsError(new ApiError("db_unavailable", "数据库暂不可用，请稍后重试。"))).toBe(
      "数据库暂不可用，请稍后重试。",
    );
    expect(standardDocumentsError(new Error("boom"))).toBe("操作失败，请稍后重试。");
  });
});
```

- [ ] **步骤 2：跑测试，确认失败**

运行（frontend 目录）：`npx vitest run src/api/standardDocumentsApi.test.ts`
预期：FAIL，`Failed to resolve import "./standardDocumentsApi"`。

- [ ] **步骤 3：写实现**

`frontend/src/api/standardDocumentsApi.ts`：

```ts
// 规范原文库（设计 docs/superpowers/specs/2026-10-09-standard-documents-library-design.md）。
//
// 和 standardsApi.ts 不是一回事：那边是本系统的结构化规则包（「规则管理」），
// 这边是规范原文 PDF 及其版本（「规范管理」）。

import { ApiError, request } from "./apiClient";

export type StandardVersionKind = "official" | "draft_comment" | "draft_approval";

/** 后端按日期推导的状态，前端不再重复推一遍。 */
export type StandardVersionStatus = "current" | "upcoming" | "withdrawn" | "draft_comment" | "draft_approval";

export interface StandardVersion {
  id: string;
  label: string;
  code: string;
  kind: StandardVersionKind;
  status: StandardVersionStatus;
  nature: string | null;
  issuer: string | null;
  editor: string | null;
  publish_date: string | null;
  effective_date: string | null;
  /** 生效的废止日期：手工值，或被后续正式版替代时推算出来的值。 */
  withdrawn_date: string | null;
  withdrawn_date_source: "manual" | "replacement" | null;
  announcement: string | null;
  source: string | null;
  note: string | null;
  replaced_by_version_id: string | null;
  replaces_version_id: string | null;
  file: { original_name: string; size_bytes: number };
  content_path: string;
  uploaded_by: string | null;
  uploaded_at: string;
  updated_at: string;
}

export interface StandardDocument {
  id: string;
  name: string;
  english_name: string | null;
  category: string;
  default_version_id: string | null;
  /** 按版本日期从新到旧。 */
  versions: StandardVersion[];
}

export interface StandardDocumentInput {
  name: string;
  english_name: string | null;
  category: string;
}

export interface StandardVersionInput {
  label: string;
  code: string;
  kind: StandardVersionKind;
  nature: string | null;
  issuer: string | null;
  editor: string | null;
  publish_date: string | null;
  effective_date: string | null;
  withdrawn_date: string | null;
  announcement: string | null;
  source: string | null;
  note: string | null;
}

/** 与后端全局请求体上限一致（`word_upload_max_bytes`，默认 256 MB），前端先拦一道。 */
export const STANDARD_PDF_MAX_BYTES = 256 * 1024 * 1024;

const JSON_HEADERS = { "Content-Type": "application/json" };

function documentUrl(baseUrl: string, documentId: string): string {
  return `${baseUrl}/api/standard-documents/${encodeURIComponent(documentId)}`;
}

function versionUrl(baseUrl: string, versionId: string): string {
  return `${baseUrl}/api/standard-document-versions/${encodeURIComponent(versionId)}`;
}

export async function fetchStandardDocuments(baseUrl: string): Promise<StandardDocument[]> {
  const body = await request<{ documents: StandardDocument[] }>(`${baseUrl}/api/standard-documents`);
  return body.documents;
}

export async function createStandardDocument(
  baseUrl: string,
  document: StandardDocumentInput,
  version: StandardVersionInput,
  file: File,
): Promise<StandardDocument> {
  const form = new FormData();
  form.append("metadata", JSON.stringify({ document, version }));
  form.append("file", file);
  const body = await request<{ document: StandardDocument }>(`${baseUrl}/api/standard-documents`, {
    method: "POST",
    body: form,
  });
  return body.document;
}

export async function updateStandardDocument(
  baseUrl: string,
  documentId: string,
  document: StandardDocumentInput,
): Promise<StandardDocument> {
  const body = await request<{ document: StandardDocument }>(documentUrl(baseUrl, documentId), {
    method: "PATCH",
    headers: JSON_HEADERS,
    body: JSON.stringify(document),
  });
  return body.document;
}

export async function deleteStandardDocument(baseUrl: string, documentId: string): Promise<void> {
  await request(documentUrl(baseUrl, documentId), { method: "DELETE" });
}

export async function addStandardVersion(
  baseUrl: string,
  documentId: string,
  version: StandardVersionInput,
  file: File,
): Promise<StandardDocument> {
  const form = new FormData();
  form.append("metadata", JSON.stringify(version));
  form.append("file", file);
  const body = await request<{ document: StandardDocument }>(`${documentUrl(baseUrl, documentId)}/versions`, {
    method: "POST",
    body: form,
  });
  return body.document;
}

export async function updateStandardVersion(
  baseUrl: string,
  versionId: string,
  version: StandardVersionInput,
): Promise<StandardDocument> {
  const body = await request<{ document: StandardDocument }>(versionUrl(baseUrl, versionId), {
    method: "PATCH",
    headers: JSON_HEADERS,
    body: JSON.stringify(version),
  });
  return body.document;
}

export async function replaceStandardVersionFile(
  baseUrl: string,
  versionId: string,
  file: File,
): Promise<StandardDocument> {
  const form = new FormData();
  form.append("file", file);
  const body = await request<{ document: StandardDocument }>(`${versionUrl(baseUrl, versionId)}/file`, {
    method: "PUT",
    body: form,
  });
  return body.document;
}

export async function deleteStandardVersion(baseUrl: string, versionId: string): Promise<StandardDocument> {
  const body = await request<{ document: StandardDocument }>(versionUrl(baseUrl, versionId), { method: "DELETE" });
  return body.document;
}

/**
 * 原文地址。内容接口不鉴权：iframe 带不上 Authorization 头。
 *
 * 带上 updated_at：替换原文后地址跟着变，iframe 和浏览器缓存都会取新文件。
 * page 拼成 `#page=N`，浏览器自带的 PDF 阅读器据此跳页。
 */
export function standardVersionContentUrl(
  baseUrl: string,
  version: Pick<StandardVersion, "content_path" | "updated_at">,
  options: { download?: boolean; page?: number } = {},
): string {
  const base = baseUrl.replace(/\/+$/, "");
  const query = new URLSearchParams({ v: version.updated_at });
  if (options.download) query.set("download", "1");
  const fragment = options.page ? `#page=${options.page}` : "";
  return `${base}${version.content_path}?${query.toString()}${fragment}`;
}

export function standardDocumentsError(error: unknown): string {
  if (!(error instanceof ApiError)) return "操作失败，请稍后重试。";
  // drogon 对超大请求直接回 413，没有 JSON 体，apiClient 只能给出这个中性错误码。
  if (error.code === "unrecognized_error_response" && error.message.includes("413")) return "文件超过了上传上限。";
  const messages: Record<string, string> = {
    forbidden: "只有管理员可以维护规范。",
    standard_file_not_pdf: "只能上传 PDF 文件。",
    standard_file_required: "请选择要上传的 PDF 文件。",
    standard_document_name_taken: "已有同名规范。",
    standard_version_label_taken: "这部规范里已有同名版本。",
    standard_version_effective_date_taken: "这部规范里已有同一实施日期的正式版本。",
    standard_document_not_found: "规范不存在或已被删除，请刷新。",
    standard_version_not_found: "版本不存在或已被删除，请刷新。",
    standard_document_commit_uncertain: "保存结果暂时无法确认，请刷新页面核实。",
  };
  // 其余错误码（含 standard_document_invalid 的逐项说明）用后端给的文案。
  return messages[error.code] ?? error.message;
}
```

- [ ] **步骤 4：跑测试，确认通过**

运行（frontend 目录）：`npx vitest run src/api/standardDocumentsApi.test.ts`
预期：5 个用例 PASS。

- [ ] **步骤 5：提交**

```bash
git add -- frontend/src/api/standardDocumentsApi.ts frontend/src/api/standardDocumentsApi.test.ts
```

提交信息：`feat(frontend): 规范原文库接口模块`

---

### 任务 7：只读展示组件

**文件：**
- 新建：`frontend/src/test/standardDocumentFixtures.ts`
- 新建：`frontend/src/standard-documents/versionStatus.tsx`
- 新建：`frontend/src/standard-documents/StandardDocumentList.tsx`、`StandardDocumentList.test.tsx`
- 新建：`frontend/src/standard-documents/StandardDocumentHeader.tsx`、`StandardDocumentHeader.test.tsx`
- 新建：`frontend/src/standard-documents/StandardPdfReader.tsx`
- 新建：`frontend/src/standard-documents/StandardVersionTable.tsx`、`StandardVersionTable.test.tsx`
- 新建：`frontend/src/standard-documents/StandardBasicInfo.tsx`

**接口：**
- 使用：任务 6 的类型与 `standardVersionContentUrl`；`backendBaseUrl`（`../config`）。
- 产出：
  - `versionStatus.tsx`：`VERSION_STATUS_META`、`VERSION_KIND_LABELS`、`VersionStatusTag({ status })`、`isDraft(version)`、`shortIssuer(issuer)`、`versionDateText(version)`
  - `StandardDocumentList({ documents, selectedId, isAdmin, onSelect(documentId), onCreate() })`
  - `StandardDocumentHeader({ document, version, isAdmin, onSelectVersion(versionId), onEditDocument(), onDeleteDocument() })`
  - `StandardPdfReader({ document, version, page?, height })`
  - `StandardVersionTable({ document, viewingId, isAdmin, onRead(versionId), onUpload(), onEdit(version), onReplace(version), onDelete(version): Promise<void> })`
  - `StandardBasicInfo({ document, version })`
  - 测试数据（`src/test/standardDocumentFixtures.ts`）：`standardVersion(overrides)`、`h21Document`、`maintenanceDocument`、`allStandardDocuments`

- [ ] **步骤 1：写测试数据**

`frontend/src/test/standardDocumentFixtures.ts`：

```ts
import type { StandardDocument, StandardVersion } from "../api/standardDocumentsApi";

/** 规范原文库测试共用的示例数据：H21 三个版本、养护规范新旧两版。 */
export function standardVersion(overrides: Partial<StandardVersion> & { id: string }): StandardVersion {
  return {
    label: "版本",
    code: "CODE",
    kind: "official",
    status: "current",
    nature: "推荐性行业标准",
    issuer: "中华人民共和国交通运输部",
    editor: null,
    publish_date: null,
    effective_date: null,
    withdrawn_date: null,
    withdrawn_date_source: null,
    announcement: null,
    source: "交通运输部政府信息公开",
    note: null,
    replaced_by_version_id: null,
    replaces_version_id: null,
    file: { original_name: "standard.pdf", size_bytes: 3_456_789 },
    content_path: `/api/standard-document-versions/${overrides.id}/content`,
    uploaded_by: "系统管理员",
    uploaded_at: "2026-10-09 10:00:00",
    updated_at: "2026-10-09 10:00:00",
    ...overrides,
  };
}

export const h21Document: StandardDocument = {
  id: "h21",
  name: "公路桥梁技术状况评定标准",
  english_name: "Standards for Technical Condition Evaluation of Highway Bridges",
  category: "技术状况评定",
  default_version_id: "h21-2011",
  versions: [
    standardVersion({
      id: "h21-2026", label: "2026 报批稿", code: "JTG/T 52XX—2026", kind: "draft_approval",
      status: "draft_approval", publish_date: "2026-02-11", source: "第三方渠道（待核实）",
    }),
    standardVersion({
      id: "h21-2017", label: "2017 征求意见稿", code: "JTG/T H21—2017", kind: "draft_comment", status: "draft_comment",
    }),
    standardVersion({
      id: "h21-2011", label: "2011 版", code: "JTG/T H21—2011", publish_date: "2011-07-19",
      effective_date: "2011-09-01", announcement: "交通运输部公告2011年第41号", editor: "交通运输部公路科学研究院",
    }),
  ],
};

export const maintenanceDocument: StandardDocument = {
  id: "maintenance",
  name: "公路桥涵养护规范",
  english_name: null,
  category: "养护与检查",
  default_version_id: "jtg-5120",
  versions: [
    standardVersion({
      id: "jtg-5120", label: "2021 版", code: "JTG 5120—2021", nature: "行业标准", publish_date: "2021-08-09",
      effective_date: "2021-11-01", replaces_version_id: "h11",
    }),
    standardVersion({
      id: "h11", label: "2004 版", code: "JTG H11—2004", nature: "行业标准", status: "withdrawn",
      publish_date: "2004-06-28", effective_date: "2004-10-01", withdrawn_date: "2021-11-01",
      withdrawn_date_source: "replacement", replaced_by_version_id: "jtg-5120", source: "本地上传",
    }),
  ],
};

export const allStandardDocuments = [maintenanceDocument, h21Document];
```

- [ ] **步骤 2：写组件测试**

`frontend/src/standard-documents/StandardDocumentHeader.test.tsx`：

```tsx
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { h21Document, maintenanceDocument, standardVersion } from "../test/standardDocumentFixtures";
import { StandardDocumentHeader } from "./StandardDocumentHeader";

function renderHeader(props: Partial<Parameters<typeof StandardDocumentHeader>[0]> = {}) {
  const handlers = { onSelectVersion: vi.fn(), onEditDocument: vi.fn(), onDeleteDocument: vi.fn() };
  render(
    <StandardDocumentHeader
      document={maintenanceDocument}
      version={maintenanceDocument.versions[1]}
      isAdmin={false}
      {...handlers}
      {...props}
    />,
  );
  return handlers;
}

describe("StandardDocumentHeader", () => {
  it("explains a withdrawn version and offers the current one", async () => {
    const { onSelectVersion } = renderHeader();

    expect(screen.getByText("JTG H11—2004 已于 2021-11-01 废止，由 JTG 5120—2021 替代；仅供查阅历史报告的评定依据。"))
      .toBeInTheDocument();
    expect(screen.getByText("JTG H11—2004 · 交通运输部发布 · 2004-10-01 实施 · 2021-11-01 废止")).toBeInTheDocument();
    expect(screen.getByRole("link", { name: /下载原文/ })).toHaveAttribute("href", expect.stringContaining("download=1"));

    await userEvent.click(screen.getByRole("button", { name: "查看现行版本" }));
    expect(onSelectVersion).toHaveBeenCalledWith("jtg-5120");
  });

  it("warns that a draft cannot be used for assessment", () => {
    renderHeader({ document: h21Document, version: h21Document.versions[0] });
    expect(screen.getByText("2026 报批稿尚未正式发布，内容可能与最终发布版本不同，不能作为评定依据。")).toBeInTheDocument();
    expect(screen.getByText("JTG/T 52XX—2026 · 尚未发布 · 稿件日期 2026-02-11")).toBeInTheDocument();
  });

  it("says when an upcoming version takes over", () => {
    const upcoming = standardVersion({ id: "next", label: "2027 版", code: "JTG 5120—2027", status: "upcoming", effective_date: "2027-03-01" });
    const document = { ...maintenanceDocument, versions: [upcoming, ...maintenanceDocument.versions] };
    renderHeader({ document, version: upcoming });
    expect(screen.getByText("JTG 5120—2027 将于 2027-03-01 实施。在此之前现行版本仍是 JTG 5120—2021。")).toBeInTheDocument();
  });

  it("shows no notice and no maintenance menu for a reader on the current version", () => {
    renderHeader({ version: maintenanceDocument.versions[0] });
    expect(screen.queryByRole("alert")).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "更多" })).not.toBeInTheDocument();
  });

  it("gives administrators edit and delete in the more menu", async () => {
    const { onDeleteDocument } = renderHeader({ isAdmin: true, version: maintenanceDocument.versions[0] });
    await userEvent.click(screen.getByRole("button", { name: "更多" }));
    await userEvent.click(await screen.findByText("删除规范"));
    expect(onDeleteDocument).toHaveBeenCalledTimes(1);
  });
});
```

`frontend/src/standard-documents/StandardDocumentList.test.tsx`：

```tsx
import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { allStandardDocuments, h21Document } from "../test/standardDocumentFixtures";
import { StandardDocumentList } from "./StandardDocumentList";

describe("StandardDocumentList", () => {
  it("groups standards by category and filters by any version's code", async () => {
    const onSelect = vi.fn();
    render(<StandardDocumentList documents={allStandardDocuments} selectedId="h21" isAdmin={false} onSelect={onSelect} onCreate={vi.fn()} />);

    const group = screen.getByRole("group", { name: "技术状况评定" });
    expect(within(group).getByText("JTG/T H21—2011 · 3 个版本")).toBeInTheDocument();

    await userEvent.type(screen.getByRole("textbox", { name: "搜索规范名称或编号" }), "H11");
    expect(screen.queryByRole("group", { name: "技术状况评定" })).not.toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "公路桥涵养护规范" }));
    expect(onSelect).toHaveBeenCalledWith("maintenance");
  });

  it("invites administrators to add the first standard", async () => {
    const onCreate = vi.fn();
    render(<StandardDocumentList documents={[]} selectedId={null} isAdmin onSelect={vi.fn()} onCreate={onCreate} />);
    await userEvent.click(screen.getByRole("button", { name: "新增规范" }));
    expect(onCreate).toHaveBeenCalledTimes(1);
  });

  it("marks a standard without versions", () => {
    const empty = { ...h21Document, default_version_id: null, versions: [] };
    render(<StandardDocumentList documents={[empty]} selectedId={null} isAdmin={false} onSelect={vi.fn()} onCreate={vi.fn()} />);
    expect(screen.getByText("暂无版本")).toBeInTheDocument();
  });
});
```

`frontend/src/standard-documents/StandardVersionTable.test.tsx`：

```tsx
import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { h21Document, maintenanceDocument } from "../test/standardDocumentFixtures";
import { StandardVersionTable } from "./StandardVersionTable";

function renderTable(isAdmin: boolean, document = maintenanceDocument) {
  const handlers = {
    onRead: vi.fn(), onUpload: vi.fn(), onEdit: vi.fn(), onReplace: vi.fn(),
    onDelete: vi.fn().mockResolvedValue(undefined),
  };
  render(<StandardVersionTable document={document} viewingId={document.versions[0].id} isAdmin={isAdmin} {...handlers} />);
  return handlers;
}

describe("StandardVersionTable", () => {
  it("states replacements, dates and sources for every version", () => {
    renderTable(false);
    expect(screen.getByText("2021-11-01 起由 JTG 5120—2021 替代")).toBeInTheDocument();
    expect(screen.getByText("替代 JTG H11—2004")).toBeInTheDocument();
    expect(screen.getByText("2004-06-28 发布")).toBeInTheDocument();
    expect(screen.getByText("原文来源：本地上传")).toBeInTheDocument();
    expect(screen.getByText("正在查看")).toBeInTheDocument();
    expect(screen.queryByText("替换原文")).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /上传新版本/ })).not.toBeInTheDocument();
  });

  it("shows the draft date for drafts", () => {
    renderTable(false, h21Document);
    expect(screen.getByText("稿件日期 2026-02-11")).toBeInTheDocument();
    expect(screen.getByText("未发布")).toBeInTheDocument();
  });

  it("asks before deleting a version", async () => {
    const { onDelete, onRead } = renderTable(true);
    const row = screen.getByText("2004 版").closest("tr") as HTMLElement;

    await userEvent.click(within(row).getByText("阅读"));
    expect(onRead).toHaveBeenCalledWith("h11");

    await userEvent.click(within(row).getByText("删除"));
    await userEvent.click(await screen.findByRole("button", { name: /^删\s?除$/ }));
    expect(onDelete).toHaveBeenCalledWith(maintenanceDocument.versions[1]);
  });
});
```

- [ ] **步骤 3：跑测试，确认失败**

运行（frontend 目录）：`npx vitest run src/standard-documents`
预期：FAIL，找不到 `./StandardDocumentHeader` 等模块。

- [ ] **步骤 4：写组件**

`frontend/src/standard-documents/versionStatus.tsx`：

```tsx
import { Tag } from "antd";

import type { StandardVersion, StandardVersionKind, StandardVersionStatus } from "../api/standardDocumentsApi";

/** 状态标签的文案与颜色（设计 §5.3）。 */
export const VERSION_STATUS_META: Record<StandardVersionStatus, { label: string; color: string }> = {
  current: { label: "现行", color: "success" },
  upcoming: { label: "即将实施", color: "processing" },
  withdrawn: { label: "已废止", color: "default" },
  draft_comment: { label: "征求意见稿", color: "warning" },
  draft_approval: { label: "报批稿", color: "warning" },
};

export const VERSION_KIND_LABELS: Record<StandardVersionKind, string> = {
  official: "正式版",
  draft_comment: "征求意见稿",
  draft_approval: "报批稿",
};

export function VersionStatusTag({ status }: { status: StandardVersionStatus }) {
  const meta = VERSION_STATUS_META[status];
  return <Tag color={meta.color} style={{ marginInlineEnd: 0 }}>{meta.label}</Tag>;
}

export function isDraft(version: Pick<StandardVersion, "kind">): boolean {
  return version.kind !== "official";
}

/** 头部那一行放不下「中华人民共和国」，去掉前缀。 */
export function shortIssuer(issuer: string | null): string | null {
  return issuer ? issuer.replace(/^中华人民共和国/, "") : null;
}

/** 「2011-09-01 实施」；稿件写「稿件日期 2026-02-11」；都没有返回空。 */
export function versionDateText(version: StandardVersion): string | null {
  if (isDraft(version)) return version.publish_date ? `稿件日期 ${version.publish_date}` : null;
  return version.effective_date ? `${version.effective_date} 实施` : null;
}
```

`frontend/src/standard-documents/StandardDocumentList.tsx`：

```tsx
import { SearchOutlined } from "@ant-design/icons";
import { Button, Card, Empty, Flex, Input, Typography, theme } from "antd";
import { useMemo, useState } from "react";

import type { StandardDocument } from "../api/standardDocumentsApi";
import { VersionStatusTag } from "./versionStatus";

interface Props {
  /** 已按分类、名称排好；null 表示还在加载。 */
  documents: StandardDocument[] | null;
  selectedId: string | null;
  isAdmin: boolean;
  onSelect: (documentId: string) => void;
  onCreate: () => void;
}

/** 左栏规范目录：按分类分组，可按名称或任一版本的编号搜索（设计 §5.2）。 */
export function StandardDocumentList({ documents, selectedId, isAdmin, onSelect, onCreate }: Props) {
  const { token } = theme.useToken();
  const [query, setQuery] = useState("");
  const groups = useMemo(() => {
    const keyword = query.trim().toLocaleLowerCase();
    const visible = (documents ?? []).filter((document) =>
      !keyword
      || document.name.toLocaleLowerCase().includes(keyword)
      || document.versions.some((version) => version.code.toLocaleLowerCase().includes(keyword)));
    const byCategory = new Map<string, StandardDocument[]>();
    for (const document of visible) {
      byCategory.set(document.category, [...(byCategory.get(document.category) ?? []), document]);
    }
    return [...byCategory.entries()];
  }, [documents, query]);

  return (
    <Card
      title="规范目录"
      extra={<Typography.Text type="secondary">{documents?.length ?? 0} 部</Typography.Text>}
      style={{ height: "100%" }}
      styles={{
        root: { display: "flex", flexDirection: "column" },
        body: { flex: 1, minHeight: 0, display: "flex", flexDirection: "column", gap: 12 },
      }}
    >
      <Input
        allowClear
        aria-label="搜索规范名称或编号"
        prefix={<SearchOutlined aria-hidden="true" />}
        placeholder="搜索规范名称或编号"
        value={query}
        onChange={(event) => setQuery(event.target.value)}
      />
      <Flex vertical gap={16} style={{ flex: 1, minHeight: 0, overflow: "auto" }}>
        {documents !== null && documents.length === 0 ? (
          <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description={isAdmin ? "还没有规范" : "暂无规范"}>
            {isAdmin ? <Button type="primary" onClick={onCreate}>新增规范</Button> : null}
          </Empty>
        ) : null}
        {groups.map(([category, items]) => (
          <Flex key={category} vertical gap={2} role="group" aria-label={category}>
            <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM, padding: "0 12px 4px" }}>
              {category}
            </Typography.Text>
            {items.map((document) => {
              const selected = document.id === selectedId;
              const shown = document.versions.find((version) => version.id === document.default_version_id);
              return (
                <Button
                  key={document.id}
                  type="text"
                  block
                  aria-label={document.name}
                  aria-pressed={selected}
                  onClick={() => onSelect(document.id)}
                  style={{
                    height: "auto",
                    padding: "10px 12px",
                    justifyContent: "flex-start",
                    textAlign: "left",
                    background: selected ? token.colorPrimaryBg : undefined,
                  }}
                >
                  <Flex vertical gap={4} style={{ width: "100%", minWidth: 0 }}>
                    <Flex align="center" justify="space-between" gap={8}>
                      <Typography.Text strong ellipsis style={{ color: selected ? token.colorPrimary : undefined }}>
                        {document.name}
                      </Typography.Text>
                      {shown ? <VersionStatusTag status={shown.status} /> : <Typography.Text type="secondary">暂无版本</Typography.Text>}
                    </Flex>
                    {shown ? (
                      <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
                        {shown.code}
                        {document.versions.length > 1 ? ` · ${document.versions.length} 个版本` : ""}
                      </Typography.Text>
                    ) : null}
                  </Flex>
                </Button>
              );
            })}
          </Flex>
        ))}
      </Flex>
    </Card>
  );
}
```

`frontend/src/standard-documents/StandardDocumentHeader.tsx`：

```tsx
import { DownloadOutlined, MoreOutlined } from "@ant-design/icons";
import { Alert, Button, Dropdown, Flex, Select, Typography, theme } from "antd";

import { type StandardDocument, type StandardVersion, standardVersionContentUrl } from "../api/standardDocumentsApi";
import { backendBaseUrl } from "../config";
import { VersionStatusTag, isDraft, shortIssuer, versionDateText } from "./versionStatus";

interface Props {
  document: StandardDocument;
  /** 没有版本时为空。 */
  version: StandardVersion | null;
  isAdmin: boolean;
  onSelectVersion: (versionId: string) => void;
  onEditDocument: () => void;
  onDeleteDocument: () => void;
}

/** 非现行版本的提示（设计 §5.3）；现行版本返回空。 */
function noticeFor(document: StandardDocument, version: StandardVersion): string | null {
  const current = document.versions.find((item) => item.status === "current");
  if (isDraft(version)) return `${version.label}尚未正式发布，内容可能与最终发布版本不同，不能作为评定依据。`;
  if (version.status === "withdrawn") {
    const replacement = document.versions.find((item) => item.id === version.replaced_by_version_id);
    return replacement
      ? `${version.code} 已于 ${version.withdrawn_date} 废止，由 ${replacement.code} 替代；仅供查阅历史报告的评定依据。`
      : `${version.code} 已于 ${version.withdrawn_date} 废止；仅供查阅历史报告的评定依据。`;
  }
  if (version.status === "upcoming") {
    return current
      ? `${version.code} 将于 ${version.effective_date} 实施。在此之前现行版本仍是 ${current.code}。`
      : `${version.code} 将于 ${version.effective_date} 实施。`;
  }
  return null;
}

export function StandardDocumentHeader({ document, version, isAdmin, onSelectVersion, onEditDocument, onDeleteDocument }: Props) {
  const { token } = theme.useToken();
  const current = document.versions.find((item) => item.status === "current");
  const official = document.versions.filter((item) => !isDraft(item));
  const drafts = document.versions.filter(isDraft);
  const issuer = version ? shortIssuer(version.issuer) : null;
  const facts = version
    ? [
        version.code,
        isDraft(version) ? "尚未发布" : issuer ? `${issuer}发布` : null,
        versionDateText(version),
        !isDraft(version) && version.withdrawn_date ? `${version.withdrawn_date} 废止` : null,
        version.announcement,
      ].filter((item): item is string => Boolean(item))
    : [];
  const notice = version ? noticeFor(document, version) : null;
  const toOption = (item: StandardVersion) => ({ value: item.id, label: item.label });

  return (
    <Flex vertical gap={14}>
      <Flex align="start" justify="space-between" gap={16} wrap>
        <Flex vertical gap={4} style={{ minWidth: 0 }}>
          <Typography.Text type="secondary">
            {[document.category, version?.nature].filter(Boolean).join(" · ")}
          </Typography.Text>
          <Flex align="center" gap={10} wrap>
            <Typography.Title level={3} style={{ margin: 0 }}>{document.name}</Typography.Title>
            {version ? <VersionStatusTag status={version.status} /> : null}
          </Flex>
          {facts.length > 0 ? <Typography.Text type="secondary">{facts.join(" · ")}</Typography.Text> : null}
        </Flex>
        <Flex align="center" gap={8} wrap>
          {version ? (
            <>
              <Typography.Text type="secondary">版本</Typography.Text>
              <Select
                aria-label="版本"
                value={version.id}
                onChange={onSelectVersion}
                style={{ width: 260 }}
                popupMatchSelectWidth={340}
                listHeight={360}
                labelRender={({ value }) => {
                  const item = document.versions.find((candidate) => candidate.id === value);
                  return item ? (
                    <Flex align="center" gap={8}>
                      <span>{item.label}</span>
                      <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>{item.code}</Typography.Text>
                    </Flex>
                  ) : null;
                }}
                optionRender={(option) => {
                  const item = document.versions.find((candidate) => candidate.id === option.value);
                  if (!item) return option.label;
                  return (
                    <Flex align="center" justify="space-between" gap={12}>
                      <Flex vertical>
                        <span>{item.label}</span>
                        <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
                          {[item.code, versionDateText(item)].filter(Boolean).join(" · ")}
                        </Typography.Text>
                      </Flex>
                      <VersionStatusTag status={item.status} />
                    </Flex>
                  );
                }}
                options={[
                  ...(official.length ? [{ label: "正式版本", title: "正式版本", options: official.map(toOption) }] : []),
                  ...(drafts.length ? [{ label: "修订稿（未发布）", title: "修订稿（未发布）", options: drafts.map(toOption) }] : []),
                ]}
              />
              <Button icon={<DownloadOutlined />} href={standardVersionContentUrl(backendBaseUrl, version, { download: true })}>
                下载原文
              </Button>
            </>
          ) : null}
          {isAdmin ? (
            <Dropdown
              trigger={["click"]}
              menu={{
                items: [
                  { key: "edit", label: "编辑规范信息" },
                  { key: "delete", label: "删除规范", danger: true },
                ],
                onClick: ({ key }) => (key === "edit" ? onEditDocument() : onDeleteDocument()),
              }}
            >
              <Button icon={<MoreOutlined />} aria-label="更多">更多</Button>
            </Dropdown>
          ) : null}
        </Flex>
      </Flex>
      {notice ? (
        <Alert
          type="warning"
          showIcon
          title={notice}
          action={current && version && current.id !== version.id ? (
            <Button size="small" onClick={() => onSelectVersion(current.id)}>查看现行版本</Button>
          ) : undefined}
        />
      ) : null}
    </Flex>
  );
}
```

`frontend/src/standard-documents/StandardPdfReader.tsx`：

```tsx
import { theme } from "antd";

import { type StandardDocument, type StandardVersion, standardVersionContentUrl } from "../api/standardDocumentsApi";
import { backendBaseUrl } from "../config";

interface Props {
  document: StandardDocument;
  version: StandardVersion;
  page?: number;
  /** 两栏时撑满卡片（"100%"），叠放时给固定高度。 */
  height: string;
}

/**
 * 原文阅读区：直接交给浏览器自带的 PDF 阅读器（设计 §5.4）。
 *
 * 目录（书签）、翻页、缩放、查找都在阅读器自己的工具栏和侧栏里，本页不画。
 * 以版本 id 作 key：切版本就重建 iframe，不让阅读器沿用上一份文件的翻页位置。
 */
export function StandardPdfReader({ document, version, page, height }: Props) {
  const { token } = theme.useToken();
  return (
    <iframe
      key={version.id}
      title={`规范原文：${version.code} ${document.name}`}
      src={standardVersionContentUrl(backendBaseUrl, version, { page })}
      style={{
        display: "block",
        width: "100%",
        height,
        border: `1px solid ${token.colorBorderSecondary}`,
        borderRadius: token.borderRadius,
        background: token.colorBgLayout,
      }}
    />
  );
}
```

`frontend/src/standard-documents/StandardVersionTable.tsx`：

```tsx
import { UploadOutlined } from "@ant-design/icons";
import { Button, Flex, Popconfirm, Table, Typography, theme } from "antd";

import { type StandardDocument, type StandardVersion, standardVersionContentUrl } from "../api/standardDocumentsApi";
import { backendBaseUrl } from "../config";
import { VersionStatusTag, isDraft } from "./versionStatus";

interface Props {
  document: StandardDocument;
  viewingId: string | null;
  isAdmin: boolean;
  onRead: (versionId: string) => void;
  onUpload: () => void;
  onEdit: (version: StandardVersion) => void;
  onReplace: (version: StandardVersion) => void;
  /** 返回 Promise：确认框在删除完成前保持加载状态。 */
  onDelete: (version: StandardVersion) => Promise<void>;
}

/** 说明栏：替代关系、公告文号、备注，取第一个有值的（设计 §5.5）。 */
function noteText(document: StandardDocument, version: StandardVersion): string {
  const codeOf = (id: string | null) => document.versions.find((item) => item.id === id)?.code;
  const replacedBy = codeOf(version.replaced_by_version_id);
  if (version.status === "withdrawn" && replacedBy) return `${version.withdrawn_date} 起由 ${replacedBy} 替代`;
  const replaces = codeOf(version.replaces_version_id);
  if (replaces) return `替代 ${replaces}`;
  return version.announcement ?? version.note ?? "—";
}

export function StandardVersionTable({ document, viewingId, isAdmin, onRead, onUpload, onEdit, onReplace, onDelete }: Props) {
  const { token } = theme.useToken();
  return (
    <Flex vertical gap={12}>
      <Flex align="center" justify="space-between" gap={12} wrap>
        <Typography.Text type="secondary">
          共 {document.versions.length} 个版本。征求意见稿、报批稿仅供对照查阅，不作为评定依据。
        </Typography.Text>
        {isAdmin ? <Button icon={<UploadOutlined />} onClick={onUpload}>上传新版本</Button> : null}
      </Flex>
      <Table<StandardVersion>
        rowKey="id"
        pagination={false}
        dataSource={document.versions}
        columns={[
          {
            title: "版本",
            key: "label",
            width: 150,
            render: (_, version) => (
              <Flex vertical>
                <Typography.Text strong>{version.label}</Typography.Text>
                {version.id === viewingId ? (
                  <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>正在查看</Typography.Text>
                ) : null}
              </Flex>
            ),
          },
          { title: "标准编号", dataIndex: "code", width: 180 },
          { title: "状态", key: "status", width: 120, render: (_, version) => <VersionStatusTag status={version.status} /> },
          {
            title: "发布 / 实施",
            key: "dates",
            width: 190,
            render: (_, version) => {
              if (isDraft(version)) {
                return <Typography.Text type="secondary">{version.publish_date ? `稿件日期 ${version.publish_date}` : "未发布"}</Typography.Text>;
              }
              return (
                <Flex vertical>
                  <span>{version.publish_date ?? "—"} 发布</span>
                  <Typography.Text type="secondary">{version.effective_date ?? "—"} 实施</Typography.Text>
                </Flex>
              );
            },
          },
          {
            title: "说明",
            key: "note",
            render: (_, version) => (
              <Flex vertical>
                <span>{noteText(document, version)}</span>
                <Typography.Text type="secondary" style={{ fontSize: token.fontSizeSM }}>
                  原文来源：{version.source ?? "—"}
                </Typography.Text>
              </Flex>
            ),
          },
          {
            title: "操作",
            key: "actions",
            width: isAdmin ? 260 : 120,
            render: (_, version) => (
              <Flex gap={12} style={{ whiteSpace: "nowrap" }}>
                <Typography.Link onClick={() => onRead(version.id)}>阅读</Typography.Link>
                <Typography.Link href={standardVersionContentUrl(backendBaseUrl, version, { download: true })}>下载</Typography.Link>
                {isAdmin ? (
                  <>
                    <Typography.Link onClick={() => onEdit(version)}>编辑</Typography.Link>
                    <Typography.Link onClick={() => onReplace(version)}>替换原文</Typography.Link>
                    <Popconfirm
                      title={`删除「${version.label}」？`}
                      description="原文文件会一并删除，无法恢复。"
                      okText="删除"
                      okButtonProps={{ danger: true }}
                      cancelText="取消"
                      onConfirm={() => onDelete(version)}
                    >
                      <Typography.Link type="danger">删除</Typography.Link>
                    </Popconfirm>
                  </>
                ) : null}
              </Flex>
            ),
          },
        ]}
      />
    </Flex>
  );
}
```

`frontend/src/standard-documents/StandardBasicInfo.tsx`：

```tsx
import { Descriptions } from "antd";

import type { StandardDocument, StandardVersion } from "../api/standardDocumentsApi";
import { VersionStatusTag, isDraft } from "./versionStatus";

function fileSize(bytes: number): string {
  if (bytes >= 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  return `${Math.max(1, Math.round(bytes / 1024))} KB`;
}

/** 基本信息页签（设计 §5.6）。空值显示「—」。 */
export function StandardBasicInfo({ document, version }: { document: StandardDocument; version: StandardVersion }) {
  const text = (value: string | null) => value ?? "—";
  return (
    <Descriptions
      bordered
      column={{ xs: 1, lg: 2 }}
      items={[
        { key: "name", label: "规范名称", children: document.name },
        { key: "english", label: "英文名称", children: text(document.english_name) },
        { key: "code", label: "标准编号", children: version.code },
        { key: "nature", label: "标准性质", children: text(version.nature) },
        { key: "issuer", label: "发布部门", children: text(version.issuer) },
        { key: "editor", label: "主编单位", children: text(version.editor) },
        { key: "publish", label: isDraft(version) ? "稿件日期" : "发布日期", children: text(version.publish_date) },
        { key: "effective", label: "实施日期", children: text(version.effective_date) },
        { key: "withdrawn", label: "废止日期", children: text(version.withdrawn_date) },
        { key: "announcement", label: "公告文号", children: text(version.announcement) },
        { key: "status", label: "状态", children: <VersionStatusTag status={version.status} /> },
        { key: "file", label: "原文", children: `PDF · ${fileSize(version.file.size_bytes)}` },
        { key: "source", label: "原文来源", children: text(version.source) },
        { key: "uploaded", label: "上传", children: `${version.uploaded_by ?? "—"} · ${version.uploaded_at}` },
        { key: "note", label: "备注", children: text(version.note) },
      ]}
    />
  );
}
```

- [ ] **步骤 5：跑测试和类型检查，确认通过**

运行（frontend 目录）：
`npx vitest run src/standard-documents`
`npx tsc -b`
预期：3 个测试文件、11 个用例 PASS；类型检查无错误。

- [ ] **步骤 6：提交**

```bash
git add -- frontend/src/test/standardDocumentFixtures.ts frontend/src/standard-documents/versionStatus.tsx frontend/src/standard-documents/StandardDocumentList.tsx frontend/src/standard-documents/StandardDocumentList.test.tsx frontend/src/standard-documents/StandardDocumentHeader.tsx frontend/src/standard-documents/StandardDocumentHeader.test.tsx frontend/src/standard-documents/StandardPdfReader.tsx frontend/src/standard-documents/StandardVersionTable.tsx frontend/src/standard-documents/StandardVersionTable.test.tsx frontend/src/standard-documents/StandardBasicInfo.tsx
```

提交信息：`feat(frontend): 规范原文库的只读展示组件`

---

### 任务 8：维护表单

**文件：**
- 新建：`frontend/src/standard-documents/StandardVersionFields.tsx`
- 新建：`frontend/src/standard-documents/StandardDocumentFormModal.tsx`、`StandardDocumentFormModal.test.tsx`
- 新建：`frontend/src/standard-documents/StandardVersionFormModal.tsx`、`StandardVersionFormModal.test.tsx`
- 新建：`frontend/src/standard-documents/DeleteStandardDocumentModal.tsx`

**接口：**
- 使用：任务 6 的写接口与 `standardDocumentsError`、`STANDARD_PDF_MAX_BYTES`；任务 7 的 `VERSION_KIND_LABELS`。
- 产出：
  - `StandardVersionFields.tsx`：
    - 常量与类型：`DEFAULT_ISSUER`、`interface VersionFormValues`
    - 转换函数：`toVersionInput(values)`、`versionFormValues(version)`、`pdfFromFileList(fileList)`
    - 组件：`PdfFileField()`、`StandardVersionFields()`
  - `StandardDocumentFormModal({ mode: StandardDocumentFormMode, onClose, onSaved(document) })`
    - `type StandardDocumentFormMode = { type: "create"; categories: string[] } | { type: "edit"; document; categories }`
  - `StandardVersionFormModal({ mode: StandardVersionFormMode, onClose, onSaved(document) })`
    - `type StandardVersionFormMode = { type: "add"; document } | { type: "edit"; document; version } | { type: "replace"; document; version }`
  - `DeleteStandardDocumentModal({ document, onClose, onDeleted })`

- [ ] **步骤 1：写失败的测试**

`frontend/src/standard-documents/StandardDocumentFormModal.test.tsx`：

```tsx
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { createStandardDocument, updateStandardDocument } from "../api/standardDocumentsApi";
import { h21Document } from "../test/standardDocumentFixtures";
import { StandardDocumentFormModal } from "./StandardDocumentFormModal";

vi.mock("../api/standardDocumentsApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/standardDocumentsApi")>();
  return { ...original, createStandardDocument: vi.fn(), updateStandardDocument: vi.fn() };
});

const pdf = () => new File(["%PDF-1.4"], "h21.pdf", { type: "application/pdf" });
const fileInput = () => document.querySelector<HTMLInputElement>('input[type="file"]') as HTMLInputElement;

describe("StandardDocumentFormModal", () => {
  beforeEach(() => {
    vi.resetAllMocks();
  });

  it("submits the standard together with its first version and the PDF", async () => {
    vi.mocked(createStandardDocument).mockResolvedValue(h21Document);
    const onSaved = vi.fn();
    render(<StandardDocumentFormModal mode={{ type: "create", categories: ["技术状况评定"] }} onClose={vi.fn()} onSaved={onSaved} />);

    await userEvent.type(screen.getByLabelText("规范名称"), "公路桥梁技术状况评定标准");
    await userEvent.type(screen.getByRole("combobox", { name: "分类" }), "技术状况评定");
    await userEvent.type(screen.getByLabelText("版本名称"), "2011 版");
    await userEvent.type(screen.getByLabelText("标准编号"), "JTG/T H21—2011");
    await userEvent.type(screen.getByLabelText("实施日期"), "2011-09-01{Enter}");
    await userEvent.upload(fileInput(), pdf());
    await userEvent.click(screen.getByRole("button", { name: /^上\s?传$/ }));

    expect(createStandardDocument).toHaveBeenCalledWith(
      expect.any(String),
      { name: "公路桥梁技术状况评定标准", english_name: null, category: "技术状况评定" },
      expect.objectContaining({
        label: "2011 版", code: "JTG/T H21—2011", kind: "official",
        issuer: "中华人民共和国交通运输部", effective_date: "2011-09-01", withdrawn_date: null,
      }),
      expect.objectContaining({ name: "h21.pdf" }),
    );
    expect(onSaved).toHaveBeenCalledWith(h21Document);
  });

  it("refuses to submit without the required fields and a PDF", async () => {
    render(<StandardDocumentFormModal mode={{ type: "create", categories: [] }} onClose={vi.fn()} onSaved={vi.fn()} />);
    await userEvent.click(screen.getByRole("button", { name: /^上\s?传$/ }));

    for (const message of ["请填写规范名称", "请填写分类", "请填写版本名称", "请填写标准编号", "正式版必须填写实施日期", "请选择 PDF 文件"]) {
      expect(await screen.findByText(message)).toBeInTheDocument();
    }
    expect(createStandardDocument).not.toHaveBeenCalled();
  });

  it("rejects a file that is not a PDF", async () => {
    const user = userEvent.setup({ applyAccept: false });
    render(<StandardDocumentFormModal mode={{ type: "create", categories: [] }} onClose={vi.fn()} onSaved={vi.fn()} />);
    await user.upload(fileInput(), new File(["hello"], "notes.txt", { type: "text/plain" }));
    await user.click(screen.getByRole("button", { name: /^上\s?传$/ }));
    expect(await screen.findByText("只能上传 PDF 文件")).toBeInTheDocument();
  });

  it("edits only the standard information", async () => {
    vi.mocked(updateStandardDocument).mockResolvedValue({ ...h21Document, name: "新名称" });
    render(<StandardDocumentFormModal mode={{ type: "edit", document: h21Document, categories: [] }} onClose={vi.fn()} onSaved={vi.fn()} />);

    expect(screen.queryByLabelText("版本名称")).not.toBeInTheDocument();
    const name = screen.getByLabelText("规范名称");
    await userEvent.clear(name);
    await userEvent.type(name, "新名称");
    await userEvent.click(screen.getByRole("button", { name: /^保\s?存$/ }));

    expect(updateStandardDocument).toHaveBeenCalledWith(expect.any(String), "h21", {
      name: "新名称",
      english_name: "Standards for Technical Condition Evaluation of Highway Bridges",
      category: "技术状况评定",
    });
  });
});
```

`frontend/src/standard-documents/StandardVersionFormModal.test.tsx`：

```tsx
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { ApiError } from "../api/apiClient";
import { addStandardVersion, replaceStandardVersionFile, updateStandardVersion } from "../api/standardDocumentsApi";
import { chooseOption } from "../test/antd";
import { h21Document, maintenanceDocument } from "../test/standardDocumentFixtures";
import { StandardVersionFormModal } from "./StandardVersionFormModal";

vi.mock("../api/standardDocumentsApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/standardDocumentsApi")>();
  return { ...original, addStandardVersion: vi.fn(), updateStandardVersion: vi.fn(), replaceStandardVersionFile: vi.fn() };
});

const fileInput = () => document.querySelector<HTMLInputElement>('input[type="file"]') as HTMLInputElement;

describe("StandardVersionFormModal", () => {
  beforeEach(() => {
    vi.resetAllMocks();
  });

  it("prefills a new version from the latest one and relabels dates for drafts", async () => {
    render(<StandardVersionFormModal mode={{ type: "add", document: h21Document }} onClose={vi.fn()} onSaved={vi.fn()} />);

    expect(screen.getByLabelText("发布部门")).toHaveValue("中华人民共和国交通运输部");
    expect(screen.getByLabelText("标准性质")).toHaveValue("推荐性行业标准");
    expect(screen.getByLabelText("实施日期")).toBeInTheDocument();

    await chooseOption(screen.getByRole("combobox", { name: "稿件类型" }), "报批稿");
    expect(await screen.findByLabelText("稿件日期")).toBeInTheDocument();
    expect(screen.queryByLabelText("实施日期")).not.toBeInTheDocument();
  });

  it("keeps a replacement-derived withdrawal date out of the edit form", async () => {
    vi.mocked(updateStandardVersion).mockResolvedValue(maintenanceDocument);
    const onSaved = vi.fn();
    render(
      <StandardVersionFormModal
        mode={{ type: "edit", document: maintenanceDocument, version: maintenanceDocument.versions[1] }}
        onClose={vi.fn()}
        onSaved={onSaved}
      />,
    );

    expect(screen.getByLabelText("版本名称")).toHaveValue("2004 版");
    expect(screen.getByLabelText(/废止日期/)).toHaveValue("");
    expect(document.querySelector('input[type="file"]')).toBeNull();
    await userEvent.click(screen.getByRole("button", { name: /^保\s?存$/ }));

    expect(updateStandardVersion).toHaveBeenCalledWith(
      expect.any(String),
      "h11",
      expect.objectContaining({ label: "2004 版", effective_date: "2004-10-01", withdrawn_date: null }),
    );
    expect(onSaved).toHaveBeenCalledWith(maintenanceDocument);
  });

  it("replaces only the file", async () => {
    vi.mocked(replaceStandardVersionFile).mockResolvedValue(maintenanceDocument);
    render(
      <StandardVersionFormModal
        mode={{ type: "replace", document: maintenanceDocument, version: maintenanceDocument.versions[0] }}
        onClose={vi.fn()}
        onSaved={vi.fn()}
      />,
    );

    expect(screen.queryByLabelText("版本名称")).not.toBeInTheDocument();
    await userEvent.upload(fileInput(), new File(["%PDF-1.7"], "5120.pdf", { type: "application/pdf" }));
    await userEvent.click(screen.getByRole("button", { name: /^上\s?传$/ }));

    expect(replaceStandardVersionFile).toHaveBeenCalledWith(expect.any(String), "jtg-5120", expect.objectContaining({ name: "5120.pdf" }));
  });

  it("shows the server error above the form and keeps the input", async () => {
    vi.mocked(addStandardVersion).mockRejectedValue(new ApiError("standard_version_label_taken", "重名"));
    render(<StandardVersionFormModal mode={{ type: "add", document: maintenanceDocument }} onClose={vi.fn()} onSaved={vi.fn()} />);

    await userEvent.type(screen.getByLabelText("版本名称"), "2021 版");
    await userEvent.type(screen.getByLabelText("标准编号"), "JTG 5120—2021");
    await userEvent.type(screen.getByLabelText("实施日期"), "2021-11-01{Enter}");
    await userEvent.upload(fileInput(), new File(["%PDF-1.7"], "5120.pdf", { type: "application/pdf" }));
    await userEvent.click(screen.getByRole("button", { name: /^上\s?传$/ }));

    expect(await screen.findByText("这部规范里已有同名版本。")).toBeInTheDocument();
    expect(screen.getByLabelText("版本名称")).toHaveValue("2021 版");
  });
});
```

- [ ] **步骤 2：跑测试，确认失败**

运行（frontend 目录）：`npx vitest run src/standard-documents/StandardDocumentFormModal.test.tsx src/standard-documents/StandardVersionFormModal.test.tsx`
预期：FAIL，找不到 `./StandardDocumentFormModal`、`./StandardVersionFormModal`。

- [ ] **步骤 3：写共用字段**

`frontend/src/standard-documents/StandardVersionFields.tsx`：

```tsx
import { InboxOutlined } from "@ant-design/icons";
import { DatePicker, Flex, Form, Input, Select, Typography, Upload, theme, type UploadFile } from "antd";
import dayjs, { type Dayjs } from "dayjs";

import {
  STANDARD_PDF_MAX_BYTES,
  type StandardVersion,
  type StandardVersionInput,
  type StandardVersionKind,
} from "../api/standardDocumentsApi";
import { VERSION_KIND_LABELS } from "./versionStatus";

export const DEFAULT_ISSUER = "中华人民共和国交通运输部";

export interface VersionFormValues {
  label: string;
  code: string;
  kind: StandardVersionKind;
  nature?: string;
  issuer?: string;
  editor?: string;
  publish_date?: Dayjs | null;
  effective_date?: Dayjs | null;
  withdrawn_date?: Dayjs | null;
  announcement?: string;
  source?: string;
  note?: string;
  file?: UploadFile[];
}

function textOrNull(value: string | undefined): string | null {
  const trimmed = value?.trim() ?? "";
  return trimmed === "" ? null : trimmed;
}

function dateOrNull(value: Dayjs | null | undefined): string | null {
  return value ? value.format("YYYY-MM-DD") : null;
}

/** 表单值 → 接口入参。稿件不带实施、废止日期（设计 §5.7）。 */
export function toVersionInput(values: VersionFormValues): StandardVersionInput {
  const draft = values.kind !== "official";
  return {
    label: values.label.trim(),
    code: values.code.trim(),
    kind: values.kind,
    nature: textOrNull(values.nature),
    issuer: textOrNull(values.issuer),
    editor: textOrNull(values.editor),
    publish_date: dateOrNull(values.publish_date),
    effective_date: draft ? null : dateOrNull(values.effective_date),
    withdrawn_date: draft ? null : dateOrNull(values.withdrawn_date),
    announcement: textOrNull(values.announcement),
    source: textOrNull(values.source),
    note: textOrNull(values.note),
  };
}

/** 版本 → 编辑表单的初始值。 */
export function versionFormValues(version: StandardVersion): VersionFormValues {
  const parse = (value: string | null) => (value ? dayjs(value) : null);
  return {
    label: version.label,
    code: version.code,
    kind: version.kind,
    nature: version.nature ?? undefined,
    issuer: version.issuer ?? undefined,
    editor: version.editor ?? undefined,
    publish_date: parse(version.publish_date),
    effective_date: parse(version.effective_date),
    // 只回填手工填的废止日期。被新版本替代推算出来的那个一旦回填再保存，就成了手工值，
    // 以后删掉新版本，旧版本也恢复不了现行（设计 §6.5）。
    withdrawn_date: version.withdrawn_date_source === "manual" ? parse(version.withdrawn_date) : null,
    announcement: version.announcement ?? undefined,
    source: version.source ?? undefined,
    note: version.note ?? undefined,
  };
}

export function pdfFromFileList(fileList: UploadFile[] | undefined): File | null {
  return (fileList?.[0]?.originFileObj as File | undefined) ?? null;
}

/** 选 PDF：只在本地选中，随表单一起提交。前端先查扩展名和大小，后端再查文件头。 */
export function PdfFileField() {
  const { token } = theme.useToken();
  return (
    <Form.Item
      label="原文 PDF"
      name="file"
      required
      valuePropName="fileList"
      getValueFromEvent={(event: { fileList: UploadFile[] }) => event.fileList.slice(-1)}
      rules={[{
        validator: async (_, fileList: UploadFile[] | undefined) => {
          const file = pdfFromFileList(fileList);
          if (!file) throw new Error("请选择 PDF 文件");
          if (!file.name.toLowerCase().endsWith(".pdf")) throw new Error("只能上传 PDF 文件");
          if (file.size === 0) throw new Error("文件是空的");
          if (file.size > STANDARD_PDF_MAX_BYTES) throw new Error("文件超过 256 MB 上限");
        },
      }]}
    >
      <Upload.Dragger accept=".pdf" maxCount={1} beforeUpload={() => false}>
        <Flex vertical align="center" gap={4}>
          <InboxOutlined style={{ fontSize: token.fontSizeHeading2, color: token.colorPrimary }} />
          <Typography.Text>点击或拖入 PDF 文件</Typography.Text>
          <Typography.Text type="secondary">不超过 256 MB</Typography.Text>
        </Flex>
      </Upload.Dragger>
    </Form.Item>
  );
}

const LENGTH_200 = { max: 200, message: "不超过 200 个字" };

/** 版本字段。稿件类型选征求意见稿或报批稿时，「发布日期」改称「稿件日期」，实施、废止日期隐藏。 */
export function StandardVersionFields() {
  const form = Form.useFormInstance<VersionFormValues>();
  const kind = Form.useWatch("kind", form);
  const draft = kind !== undefined && kind !== "official";
  const kinds = Object.keys(VERSION_KIND_LABELS) as StandardVersionKind[];
  return (
    <>
      <Flex gap={12} wrap>
        <Form.Item
          label="版本名称"
          name="label"
          style={{ flex: "1 1 200px" }}
          rules={[{ required: true, whitespace: true, message: "请填写版本名称" }, { max: 40, message: "不超过 40 个字" }]}
        >
          <Input placeholder="如：2011 版、2026 报批稿" />
        </Form.Item>
        <Form.Item
          label="标准编号"
          name="code"
          style={{ flex: "1 1 200px" }}
          rules={[{ required: true, whitespace: true, message: "请填写标准编号" }, { max: 60, message: "不超过 60 个字" }]}
        >
          <Input placeholder="如：JTG/T H21—2011" />
        </Form.Item>
        <Form.Item label="稿件类型" name="kind" style={{ flex: "0 0 150px" }} rules={[{ required: true, message: "请选择稿件类型" }]}>
          <Select options={kinds.map((value) => ({ value, label: VERSION_KIND_LABELS[value] }))} />
        </Form.Item>
      </Flex>
      <Flex gap={12} wrap>
        <Form.Item label={draft ? "稿件日期" : "发布日期"} name="publish_date" style={{ flex: "1 1 160px" }}>
          <DatePicker style={{ width: "100%" }} />
        </Form.Item>
        {draft ? null : (
          <>
            <Form.Item
              label="实施日期"
              name="effective_date"
              style={{ flex: "1 1 160px" }}
              rules={[{ required: true, message: "正式版必须填写实施日期" }]}
            >
              <DatePicker style={{ width: "100%" }} />
            </Form.Item>
            <Form.Item
              label="废止日期"
              name="withdrawn_date"
              style={{ flex: "1 1 160px" }}
              tooltip="被新版本替代的不用填，系统按新版本的实施日期推算"
              dependencies={["effective_date"]}
              rules={[({ getFieldValue }) => ({
                validator: async (_, value: Dayjs | null | undefined) => {
                  const effective = getFieldValue("effective_date") as Dayjs | null | undefined;
                  if (value && effective && value.isBefore(effective, "day")) throw new Error("废止日期不能早于实施日期");
                },
              })]}
            >
              <DatePicker style={{ width: "100%" }} />
            </Form.Item>
          </>
        )}
      </Flex>
      <Flex gap={12} wrap>
        <Form.Item label="标准性质" name="nature" style={{ flex: "1 1 200px" }} rules={[LENGTH_200]}>
          <Input placeholder="如：推荐性行业标准" />
        </Form.Item>
        <Form.Item label="发布部门" name="issuer" style={{ flex: "1 1 200px" }} rules={[LENGTH_200]}>
          <Input />
        </Form.Item>
      </Flex>
      <Flex gap={12} wrap>
        <Form.Item label="主编单位" name="editor" style={{ flex: "1 1 200px" }} rules={[LENGTH_200]}>
          <Input />
        </Form.Item>
        <Form.Item label="公告文号" name="announcement" style={{ flex: "1 1 200px" }} rules={[LENGTH_200]}>
          <Input placeholder="如：交通运输部公告2011年第41号" />
        </Form.Item>
      </Flex>
      <Form.Item label="原文来源" name="source" rules={[LENGTH_200]}>
        <Input placeholder="如：交通运输部政府信息公开" />
      </Form.Item>
      <Form.Item label="备注" name="note" rules={[{ max: 500, message: "不超过 500 个字" }]}>
        <Input.TextArea rows={2} />
      </Form.Item>
    </>
  );
}
```

- [ ] **步骤 4：写三个弹窗**

`frontend/src/standard-documents/StandardDocumentFormModal.tsx`：

```tsx
import { Alert, AutoComplete, Divider, Form, Input, Modal } from "antd";
import { useState } from "react";

import { type StandardDocument, createStandardDocument, standardDocumentsError, updateStandardDocument } from "../api/standardDocumentsApi";
import { backendBaseUrl } from "../config";
import {
  DEFAULT_ISSUER,
  PdfFileField,
  StandardVersionFields,
  type VersionFormValues,
  pdfFromFileList,
  toVersionInput,
} from "./StandardVersionFields";

export type StandardDocumentFormMode =
  | { type: "create"; categories: string[] }
  | { type: "edit"; document: StandardDocument; categories: string[] };

interface DocumentFormValues extends Partial<VersionFormValues> {
  name: string;
  english_name?: string;
  category: string;
}

interface Props {
  mode: StandardDocumentFormMode;
  onClose: () => void;
  onSaved: (document: StandardDocument) => void;
}

/** 新增规范（连同首个版本和 PDF 一次提交）与编辑规范信息（设计 §5.7）。 */
export function StandardDocumentFormModal({ mode, onClose, onSaved }: Props) {
  const [form] = Form.useForm<DocumentFormValues>();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const initialValues: Partial<DocumentFormValues> = mode.type === "create"
    ? { kind: "official", issuer: DEFAULT_ISSUER }
    : { name: mode.document.name, english_name: mode.document.english_name ?? undefined, category: mode.document.category };

  async function submit() {
    let values: DocumentFormValues;
    try {
      values = await form.validateFields();
    } catch {
      return;
    }
    const document = {
      name: values.name.trim(),
      english_name: values.english_name?.trim() ? values.english_name.trim() : null,
      category: values.category.trim(),
    };
    setBusy(true);
    setError(null);
    try {
      const saved = mode.type === "create"
        ? await createStandardDocument(backendBaseUrl, document, toVersionInput(values as VersionFormValues), pdfFromFileList(values.file) as File)
        : await updateStandardDocument(backendBaseUrl, mode.document.id, document);
      onSaved(saved);
    } catch (caught) {
      setError(standardDocumentsError(caught));
      setBusy(false);
    }
  }

  return (
    <Modal
      open
      centered
      width={720}
      title={mode.type === "create" ? "新增规范" : "编辑规范信息"}
      maskClosable={false}
      closable={{ "aria-label": "关闭", disabled: busy }}
      onCancel={() => { if (!busy) onClose(); }}
      okText={mode.type === "create" ? "上传" : "保存"}
      cancelText="取消"
      okButtonProps={{ loading: busy }}
      cancelButtonProps={{ disabled: busy }}
      onOk={() => void submit()}
      destroyOnHidden
      styles={{ body: { maxHeight: "calc(100vh - 240px)", overflowY: "auto", overflowX: "hidden" } }}
    >
      <Form form={form} layout="vertical" initialValues={initialValues}>
        {error ? <Alert type="error" showIcon title={error} style={{ marginBottom: 16 }} /> : null}
        <Form.Item
          label="规范名称"
          name="name"
          rules={[{ required: true, whitespace: true, message: "请填写规范名称" }, { max: 100, message: "不超过 100 个字" }]}
        >
          <Input placeholder="如：公路桥梁技术状况评定标准" />
        </Form.Item>
        <Form.Item label="英文名称" name="english_name" rules={[{ max: 200, message: "不超过 200 个字" }]}>
          <Input />
        </Form.Item>
        <Form.Item
          label="分类"
          name="category"
          rules={[{ required: true, whitespace: true, message: "请填写分类" }, { max: 30, message: "不超过 30 个字" }]}
        >
          <AutoComplete options={mode.categories.map((value) => ({ value }))} placeholder="如：技术状况评定" />
        </Form.Item>
        {mode.type === "create" ? (
          <>
            <Divider titlePlacement="start" plain>首个版本</Divider>
            <StandardVersionFields />
            <PdfFileField />
          </>
        ) : null}
      </Form>
    </Modal>
  );
}
```

`frontend/src/standard-documents/StandardVersionFormModal.tsx`：

```tsx
import { Alert, Form, Modal } from "antd";
import { useState } from "react";

import {
  type StandardDocument,
  type StandardVersion,
  addStandardVersion,
  replaceStandardVersionFile,
  standardDocumentsError,
  updateStandardVersion,
} from "../api/standardDocumentsApi";
import { backendBaseUrl } from "../config";
import {
  DEFAULT_ISSUER,
  PdfFileField,
  StandardVersionFields,
  type VersionFormValues,
  pdfFromFileList,
  toVersionInput,
  versionFormValues,
} from "./StandardVersionFields";

export type StandardVersionFormMode =
  | { type: "add"; document: StandardDocument }
  | { type: "edit"; document: StandardDocument; version: StandardVersion }
  | { type: "replace"; document: StandardDocument; version: StandardVersion };

interface Props {
  mode: StandardVersionFormMode;
  onClose: () => void;
  onSaved: (document: StandardDocument) => void;
}

function initialValuesFor(mode: StandardVersionFormMode): Partial<VersionFormValues> {
  if (mode.type === "edit") return versionFormValues(mode.version);
  if (mode.type === "replace") return {};
  // 新版本多半和上一版同一个发布部门、同一种性质，先替管理员填上。
  const latest = mode.document.versions[0];
  return {
    kind: "official",
    nature: latest?.nature ?? undefined,
    issuer: latest?.issuer ?? DEFAULT_ISSUER,
    editor: latest?.editor ?? undefined,
  };
}

function titleFor(mode: StandardVersionFormMode): string {
  if (mode.type === "add") return `上传新版本 · ${mode.document.name}`;
  if (mode.type === "edit") return `编辑版本 · ${mode.version.label}`;
  return `替换原文 · ${mode.version.label}`;
}

/** 上传新版本、编辑版本、替换原文（设计 §5.7）。 */
export function StandardVersionFormModal({ mode, onClose, onSaved }: Props) {
  const [form] = Form.useForm<VersionFormValues>();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function submit() {
    let values: VersionFormValues;
    try {
      values = await form.validateFields();
    } catch {
      return;
    }
    setBusy(true);
    setError(null);
    try {
      let saved: StandardDocument;
      if (mode.type === "add") {
        saved = await addStandardVersion(backendBaseUrl, mode.document.id, toVersionInput(values), pdfFromFileList(values.file) as File);
      } else if (mode.type === "edit") {
        saved = await updateStandardVersion(backendBaseUrl, mode.version.id, toVersionInput(values));
      } else {
        saved = await replaceStandardVersionFile(backendBaseUrl, mode.version.id, pdfFromFileList(values.file) as File);
      }
      onSaved(saved);
    } catch (caught) {
      setError(standardDocumentsError(caught));
      setBusy(false);
    }
  }

  return (
    <Modal
      open
      centered
      width={720}
      title={titleFor(mode)}
      maskClosable={false}
      closable={{ "aria-label": "关闭", disabled: busy }}
      onCancel={() => { if (!busy) onClose(); }}
      okText={mode.type === "edit" ? "保存" : "上传"}
      cancelText="取消"
      okButtonProps={{ loading: busy }}
      cancelButtonProps={{ disabled: busy }}
      onOk={() => void submit()}
      destroyOnHidden
      styles={{ body: { maxHeight: "calc(100vh - 240px)", overflowY: "auto", overflowX: "hidden" } }}
    >
      <Form form={form} layout="vertical" initialValues={initialValuesFor(mode)}>
        {error ? <Alert type="error" showIcon title={error} style={{ marginBottom: 16 }} /> : null}
        {mode.type === "replace" ? null : <StandardVersionFields />}
        {mode.type === "edit" ? null : <PdfFileField />}
      </Form>
    </Modal>
  );
}
```

`frontend/src/standard-documents/DeleteStandardDocumentModal.tsx`：

```tsx
import { Alert, Flex, Modal, Typography } from "antd";
import { useState } from "react";

import { type StandardDocument, deleteStandardDocument, standardDocumentsError } from "../api/standardDocumentsApi";
import { backendBaseUrl } from "../config";

interface Props {
  document: StandardDocument;
  onClose: () => void;
  onDeleted: () => void;
}

/** 删除整部规范：全部版本和原文文件一并删除，先确认。 */
export function DeleteStandardDocumentModal({ document, onClose, onDeleted }: Props) {
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function confirm() {
    setBusy(true);
    setError(null);
    try {
      await deleteStandardDocument(backendBaseUrl, document.id);
      onDeleted();
    } catch (caught) {
      setError(standardDocumentsError(caught));
      setBusy(false);
    }
  }

  return (
    <Modal
      open
      centered
      width={480}
      title="删除规范"
      maskClosable={false}
      closable={{ "aria-label": "关闭", disabled: busy }}
      onCancel={() => { if (!busy) onClose(); }}
      okText="删除"
      okButtonProps={{ danger: true, loading: busy }}
      cancelText="取消"
      cancelButtonProps={{ disabled: busy }}
      onOk={() => void confirm()}
      destroyOnHidden
    >
      <Flex vertical gap={12}>
        <Typography.Text>
          确定删除「{document.name}」吗？全部 {document.versions.length} 个版本和原文文件会一并删除，无法恢复。
        </Typography.Text>
        {error ? <Alert type="error" showIcon title={error} /> : null}
      </Flex>
    </Modal>
  );
}
```

- [ ] **步骤 5：跑测试和类型检查，确认通过**

运行（frontend 目录）：
`npx vitest run src/standard-documents`
`npx tsc -b`
预期：本任务 8 个用例加任务 7 的 11 个全部 PASS；类型检查无错误。日期输入若因 rc-picker 版本差异没有在回车时提交，就在 `type` 之后补一行 `await userEvent.tab()`，让输入框失焦提交。

- [ ] **步骤 6：提交**

```bash
git add -- frontend/src/standard-documents/StandardVersionFields.tsx frontend/src/standard-documents/StandardDocumentFormModal.tsx frontend/src/standard-documents/StandardDocumentFormModal.test.tsx frontend/src/standard-documents/StandardVersionFormModal.tsx frontend/src/standard-documents/StandardVersionFormModal.test.tsx frontend/src/standard-documents/DeleteStandardDocumentModal.tsx
```

提交信息：`feat(frontend): 规范原文库的维护表单`

---

### 任务 9：规范管理页面与路由

**文件：**
- 新建：`frontend/src/pages/StandardDocumentsPage.tsx`、`StandardDocumentsPage.test.tsx`
- 修改（共享）：`frontend/src/App.tsx`

**接口：**
- 使用：任务 6–8 的全部产出；`useAuth`（`../auth/AuthContext`）；`PageHeader`、`useShellMetrics`（`../design-system`）。
- 产出：`StandardDocumentsPage()`；路由 `/standards`。查询串：`doc`、`version`、`tab`（取值 `read`、`versions`、`info`）、`page`。

- [ ] **步骤 1：写失败的测试**

`frontend/src/pages/StandardDocumentsPage.test.tsx`：

```tsx
import { render, screen, waitFor, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { MemoryRouter, useLocation } from "react-router-dom";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { createStandardDocument, deleteStandardVersion, fetchStandardDocuments } from "../api/standardDocumentsApi";
import { chooseOption, emulateViewport } from "../test/antd";
import { allStandardDocuments, h21Document, standardVersion } from "../test/standardDocumentFixtures";
import { StandardDocumentsPage } from "./StandardDocumentsPage";

const authState = vi.hoisted(() => ({ role: "admin" }));

vi.mock("../auth/AuthContext", () => ({
  useAuth: () => ({ user: { username: "tester", display_name: "测试用户", role: authState.role } }),
}));

vi.mock("../api/standardDocumentsApi", async (importOriginal) => {
  const original = await importOriginal<typeof import("../api/standardDocumentsApi")>();
  return {
    ...original,
    fetchStandardDocuments: vi.fn(),
    deleteStandardVersion: vi.fn(),
    createStandardDocument: vi.fn(),
  };
});

function LocationProbe() {
  const location = useLocation();
  return <output aria-label="当前地址">{location.pathname + location.search}</output>;
}

function renderPage(path = "/standards") {
  return render(
    <MemoryRouter initialEntries={[path]}>
      <StandardDocumentsPage />
      <LocationProbe />
    </MemoryRouter>,
  );
}

describe("StandardDocumentsPage", () => {
  beforeEach(() => {
    vi.resetAllMocks();
    authState.role = "admin";
    emulateViewport(1920);
    vi.mocked(fetchStandardDocuments).mockResolvedValue(allStandardDocuments);
  });

  it("groups standards by category and opens the current version by default", async () => {
    renderPage();

    const reader = await screen.findByTitle("规范原文：JTG/T H21—2011 公路桥梁技术状况评定标准");
    expect(reader).toHaveAttribute("src", expect.stringContaining("/api/standard-document-versions/h21-2011/content"));
    expect(screen.getByRole("group", { name: "技术状况评定" })).toBeInTheDocument();
    expect(screen.getByRole("group", { name: "养护与检查" })).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "公路桥梁技术状况评定标准" })).toBeInTheDocument();
  });

  it("switches to a draft, warns about it and goes back to the current version", async () => {
    renderPage();
    await chooseOption(await screen.findByRole("combobox", { name: "版本" }), /2026 报批稿/);

    expect(await screen.findByText(/2026 报批稿尚未正式发布/)).toBeInTheDocument();
    expect(screen.getByTitle(/规范原文：JTG\/T 52XX—2026/)).toBeInTheDocument();
    expect(screen.getByLabelText("当前地址")).toHaveTextContent("version=h21-2026");

    await userEvent.click(screen.getByRole("button", { name: "查看现行版本" }));
    expect(await screen.findByTitle(/规范原文：JTG\/T H21—2011/)).toBeInTheDocument();
  });

  it("opens the standard, version and tab named in the address bar", async () => {
    renderPage("/standards?doc=maintenance&version=h11&tab=info");

    expect(await screen.findByText(/JTG H11—2004 已于 2021-11-01 废止，由 JTG 5120—2021 替代/)).toBeInTheDocument();
    expect(screen.getByRole("tab", { name: /基本信息/, selected: true })).toBeInTheDocument();
    expect(screen.getByText("本地上传")).toBeInTheDocument();
  });

  it("drops address-bar parameters that point nowhere", async () => {
    renderPage("/standards?doc=missing&version=nothing");

    expect(await screen.findByTitle(/规范原文：JTG\/T H21—2011/)).toBeInTheDocument();
    await waitFor(() => expect(screen.getByLabelText("当前地址").textContent).toBe("/standards"));
  });

  it("keeps maintenance actions away from normal users", async () => {
    authState.role = "normal";
    renderPage("/standards?tab=versions");

    expect(await screen.findByRole("table")).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: /新增规范/ })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "更多" })).not.toBeInTheDocument();
    expect(screen.queryByText("替换原文")).not.toBeInTheDocument();
  });

  it("deletes a version after confirmation and refreshes the table", async () => {
    vi.mocked(deleteStandardVersion).mockResolvedValue({
      ...h21Document,
      versions: h21Document.versions.filter((version) => version.id !== "h21-2017"),
    });
    renderPage("/standards?tab=versions");

    const row = (await screen.findByText("2017 征求意见稿")).closest("tr") as HTMLElement;
    await userEvent.click(within(row).getByText("删除"));
    await userEvent.click(await screen.findByRole("button", { name: /^删\s?除$/ }));

    expect(deleteStandardVersion).toHaveBeenCalledWith(expect.any(String), "h21-2017");
    await waitFor(() => expect(screen.queryByText("2017 征求意见稿")).not.toBeInTheDocument());
  });

  it("creates a standard and opens it", async () => {
    const created = {
      id: "j21", name: "公路桥梁承载能力检测评定规程", english_name: null, category: "技术状况评定",
      default_version_id: "j21-draft",
      versions: [standardVersion({ id: "j21-draft", label: "征求意见稿", code: "JTG/T J21—20XX", kind: "draft_comment" as const, status: "draft_comment" as const })],
    };
    vi.mocked(createStandardDocument).mockResolvedValue(created);
    renderPage();

    await userEvent.click(await screen.findByRole("button", { name: /新增规范/ }));
    const dialog = await screen.findByRole("dialog");
    await userEvent.type(within(dialog).getByLabelText("规范名称"), created.name);
    await userEvent.type(within(dialog).getByRole("combobox", { name: "分类" }), "技术状况评定");
    await userEvent.type(within(dialog).getByLabelText("版本名称"), "征求意见稿");
    await userEvent.type(within(dialog).getByLabelText("标准编号"), "JTG/T J21—20XX");
    await chooseOption(within(dialog).getByRole("combobox", { name: "稿件类型" }), "征求意见稿");
    await userEvent.upload(
      document.querySelector<HTMLInputElement>('input[type="file"]') as HTMLInputElement,
      new File(["%PDF-1.4"], "j21.pdf", { type: "application/pdf" }),
    );
    await userEvent.click(within(dialog).getByRole("button", { name: /^上\s?传$/ }));

    expect(await screen.findByRole("heading", { name: created.name })).toBeInTheDocument();
    expect(screen.getByLabelText("当前地址")).toHaveTextContent("doc=j21");
  });
});
```

- [ ] **步骤 2：跑测试，确认失败**

运行（frontend 目录）：`npx vitest run src/pages/StandardDocumentsPage.test.tsx`
预期：FAIL，找不到 `./StandardDocumentsPage`。

- [ ] **步骤 3：写页面**

`frontend/src/pages/StandardDocumentsPage.tsx`：

```tsx
import { HistoryOutlined, InfoCircleOutlined, PlusOutlined, ReadOutlined } from "@ant-design/icons";
import { Alert, Button, Card, Col, Empty, Flex, Grid, Row, Skeleton, Tabs, Typography } from "antd";
import { useCallback, useEffect, useMemo, useState, type ReactNode } from "react";
import { useSearchParams } from "react-router-dom";

import {
  type StandardDocument,
  type StandardVersion,
  deleteStandardVersion,
  fetchStandardDocuments,
  standardDocumentsError,
} from "../api/standardDocumentsApi";
import { useAuth } from "../auth/AuthContext";
import { backendBaseUrl } from "../config";
import { PageHeader, useShellMetrics } from "../design-system";
import { DeleteStandardDocumentModal } from "../standard-documents/DeleteStandardDocumentModal";
import { StandardBasicInfo } from "../standard-documents/StandardBasicInfo";
import { StandardDocumentFormModal } from "../standard-documents/StandardDocumentFormModal";
import { StandardDocumentHeader } from "../standard-documents/StandardDocumentHeader";
import { StandardDocumentList } from "../standard-documents/StandardDocumentList";
import { StandardPdfReader } from "../standard-documents/StandardPdfReader";
import { StandardVersionFormModal, type StandardVersionFormMode } from "../standard-documents/StandardVersionFormModal";
import { StandardVersionTable } from "../standard-documents/StandardVersionTable";

/** 页头（标题行 + 下方间距）在外壳之外另占的高度，与评定树页同值。 */
const PAGE_HEADER_CHROME = 54;
const TABS = ["read", "versions", "info"] as const;
type Tab = (typeof TABS)[number];

type ModalState =
  | { type: "create-document" }
  | { type: "edit-document"; document: StandardDocument }
  | { type: "delete-document"; document: StandardDocument }
  | StandardVersionFormMode
  | null;

function sortDocuments(documents: StandardDocument[]): StandardDocument[] {
  return [...documents].sort((left, right) =>
    left.category.localeCompare(right.category, "zh-CN") || left.name.localeCompare(right.name, "zh-CN"));
}

/**
 * 规范管理：查阅规范原文，同一规范的各个版本并列归档
 * （设计 docs/superpowers/specs/2026-10-09-standard-documents-library-design.md §5）。
 *
 * 选中的规范、版本、页签、页码都记在查询串里（§5.8），刷新不丢，评定树以后也按这个格式链过来。
 */
export function StandardDocumentsPage() {
  const { user } = useAuth();
  const isAdmin = user?.role === "admin";
  const stacked = Grid.useBreakpoint().lg === false;
  const { pageOffset } = useShellMetrics();
  const [documents, setDocuments] = useState<StandardDocument[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [actionError, setActionError] = useState<string | null>(null);
  const [modal, setModal] = useState<ModalState>(null);
  const [searchParams, setSearchParams] = useSearchParams();

  const load = useCallback(() => {
    setError(null);
    fetchStandardDocuments(backendBaseUrl)
      .then((items) => setDocuments(sortDocuments(items)))
      .catch((caught) => setError(standardDocumentsError(caught)));
  }, []);
  useEffect(load, [load]);

  const docParam = searchParams.get("doc");
  const versionParam = searchParams.get("version");
  const tabParam = searchParams.get("tab");
  const pageParam = searchParams.get("page");
  const document = documents?.find((item) => item.id === docParam) ?? documents?.[0] ?? null;
  const version = document?.versions.find((item) => item.id === versionParam)
    ?? document?.versions.find((item) => item.id === document.default_version_id)
    ?? null;
  const tab: Tab = TABS.includes(tabParam as Tab) ? (tabParam as Tab) : "read";
  const page = pageParam && /^[1-9]\d*$/.test(pageParam) ? Number(pageParam) : undefined;

  // 地址栏里指向不存在的规范或版本：按缺省显示，并把无效参数清掉（§5.8）。
  useEffect(() => {
    if (documents === null) return;
    const next = new URLSearchParams(searchParams);
    if (docParam && !documents.some((item) => item.id === docParam)) {
      next.delete("doc");
      next.delete("version");
      next.delete("page");
    } else if (versionParam && document && !document.versions.some((item) => item.id === versionParam)) {
      next.delete("version");
      next.delete("page");
    }
    if (next.toString() !== searchParams.toString()) setSearchParams(next, { replace: true });
  }, [documents, docParam, versionParam, document, searchParams, setSearchParams]);

  function show(changes: { doc?: string; version?: string | null; tab?: Tab }) {
    const next = new URLSearchParams(searchParams);
    if (changes.doc !== undefined) {
      next.set("doc", changes.doc);
      next.delete("version");
      next.delete("page");
    }
    if (changes.version !== undefined) {
      if (changes.version === null) next.delete("version");
      else next.set("version", changes.version);
      next.delete("page");
    }
    if (changes.tab !== undefined) next.set("tab", changes.tab);
    setSearchParams(next);
  }

  function upsert(updated: StandardDocument) {
    setDocuments((current) => sortDocuments([...(current ?? []).filter((item) => item.id !== updated.id), updated]));
  }

  async function deleteVersion(target: StandardVersion) {
    setActionError(null);
    try {
      upsert(await deleteStandardVersion(backendBaseUrl, target.id));
      if (target.id === version?.id) show({ version: null });
    } catch (caught) {
      setActionError(standardDocumentsError(caught));
    }
  }

  const categories = useMemo(() => [...new Set((documents ?? []).map((item) => item.category))], [documents]);
  const panelRowStyle = stacked ? undefined : { height: `max(560px, calc(100dvh - ${pageOffset + PAGE_HEADER_CHROME}px))` };
  const panelColStyle = stacked ? undefined : { height: "100%" };

  let detail: ReactNode;
  if (documents === null && !error) {
    detail = <Skeleton active paragraph={{ rows: 8 }} />;
  } else if (document === null) {
    detail = <Empty description={isAdmin ? "还没有规范，先新增一部" : "暂无规范"} />;
  } else {
    detail = (
      <>
        <StandardDocumentHeader
          document={document}
          version={version}
          isAdmin={isAdmin}
          onSelectVersion={(id) => show({ version: id })}
          onEditDocument={() => setModal({ type: "edit-document", document })}
          onDeleteDocument={() => setModal({ type: "delete-document", document })}
        />
        {version === null ? (
          <Empty description="暂无版本" style={{ marginBlock: 48 }}>
            {isAdmin ? <Button type="primary" onClick={() => setModal({ type: "add", document })}>上传新版本</Button> : null}
          </Empty>
        ) : (
          <>
            <Tabs
              activeKey={tab}
              onChange={(key) => show({ tab: key as Tab })}
              items={[
                { key: "read", icon: <ReadOutlined />, label: "原文阅读" },
                {
                  key: "versions",
                  icon: <HistoryOutlined />,
                  label: (
                    <span>
                      版本记录<Typography.Text type="secondary" style={{ marginInlineStart: 6 }}>{document.versions.length}</Typography.Text>
                    </span>
                  ),
                },
                { key: "info", icon: <InfoCircleOutlined />, label: "基本信息" },
              ]}
            />
            <div style={{ flex: 1, minHeight: 0, overflow: tab === "read" ? "hidden" : "auto" }}>
              {tab === "read" ? (
                <StandardPdfReader document={document} version={version} page={page} height={stacked ? "80dvh" : "100%"} />
              ) : tab === "versions" ? (
                <StandardVersionTable
                  document={document}
                  viewingId={version.id}
                  isAdmin={isAdmin}
                  onRead={(id) => {
                    const next = new URLSearchParams(searchParams);
                    next.set("version", id);
                    next.set("tab", "read");
                    next.delete("page");
                    setSearchParams(next);
                  }}
                  onUpload={() => setModal({ type: "add", document })}
                  onEdit={(target) => setModal({ type: "edit", document, version: target })}
                  onReplace={(target) => setModal({ type: "replace", document, version: target })}
                  onDelete={deleteVersion}
                />
              ) : (
                <StandardBasicInfo document={document} version={version} />
              )}
            </div>
          </>
        )}
      </>
    );
  }

  return (
    <Flex vertical gap={14}>
      <PageHeader
        title="规范管理"
        description="查阅检测评定依据的规范原文，同一规范的各个版本并列归档"
        extra={isAdmin ? (
          <Button type="primary" icon={<PlusOutlined />} onClick={() => setModal({ type: "create-document" })}>新增规范</Button>
        ) : null}
      />
      {error ? <Alert type="error" showIcon title={error} action={<Button size="small" onClick={load}>重新加载</Button>} /> : null}
      {actionError ? <Alert type="error" showIcon closable title={actionError} onClose={() => setActionError(null)} /> : null}
      <Row gutter={[14, 14]} style={panelRowStyle}>
        <Col xs={24} lg={6} xxl={5} style={panelColStyle}>
          <StandardDocumentList
            documents={documents}
            selectedId={document?.id ?? null}
            isAdmin={isAdmin}
            onSelect={(id) => show({ doc: id })}
            onCreate={() => setModal({ type: "create-document" })}
          />
        </Col>
        <Col xs={24} lg={18} xxl={19} style={panelColStyle}>
          <Card
            style={{ height: "100%" }}
            styles={{
              root: { display: "flex", flexDirection: "column" },
              body: { flex: 1, minHeight: 0, display: "flex", flexDirection: "column", gap: 4 },
            }}
          >
            {detail}
          </Card>
        </Col>
      </Row>

      {modal?.type === "create-document" || modal?.type === "edit-document" ? (
        <StandardDocumentFormModal
          mode={modal.type === "create-document"
            ? { type: "create", categories }
            : { type: "edit", document: modal.document, categories }}
          onClose={() => setModal(null)}
          onSaved={(saved) => {
            const created = modal.type === "create-document";
            upsert(saved);
            setModal(null);
            if (created) show({ doc: saved.id });
          }}
        />
      ) : null}
      {modal?.type === "delete-document" ? (
        <DeleteStandardDocumentModal
          document={modal.document}
          onClose={() => setModal(null)}
          onDeleted={() => {
            const removedId = modal.document.id;
            setDocuments((current) => (current ?? []).filter((item) => item.id !== removedId));
            setModal(null);
            setSearchParams(new URLSearchParams(), { replace: true });
          }}
        />
      ) : null}
      {modal?.type === "add" || modal?.type === "edit" || modal?.type === "replace" ? (
        <StandardVersionFormModal
          mode={modal}
          onClose={() => setModal(null)}
          onSaved={(saved) => {
            upsert(saved);
            setModal(null);
          }}
        />
      ) : null}
    </Flex>
  );
}
```

- [ ] **步骤 4：加路由（`App.tsx` 是共享文件，只加两行）**

- 在 `const RatingTreePage = createLazyFeature(...)` 那一行之后加：
  `const StandardDocumentsPage = createLazyFeature(async () => ({ default: (await import("./pages/StandardDocumentsPage")).StandardDocumentsPage }), { label: "规范管理" });`
- 在 `        <Route path="/rating-trees" element={<RatingTreePage />} />` 之后加：
  `        <Route path="/standards" element={<StandardDocumentsPage />} />`

- [ ] **步骤 5：跑测试和类型检查，确认通过**

运行（frontend 目录）：
`npx vitest run src/pages/StandardDocumentsPage.test.tsx src/App.test.tsx`
`npx tsc -b`
预期：页面 7 个用例 PASS；`App.test.tsx`（他人未提交的测试）不受影响；类型检查无错误。

- [ ] **步骤 6：提交（`App.tsx` 留到任务 12）**

```bash
git add -- frontend/src/pages/StandardDocumentsPage.tsx frontend/src/pages/StandardDocumentsPage.test.tsx
```

提交信息：`feat(frontend): 规范管理页面`

---

### 任务 10：菜单调整与「规则管理」改名

**文件：**
- 修改：`frontend/src/layouts/AppLayout.tsx`、`frontend/src/layouts/AppLayout.test.tsx`
- 修改（共享）：`frontend/src/pages/BridgesPage.tsx`
- 修改：`frontend/src/pages/BridgesPage.test.tsx`
- 修改：`frontend/src/standards/StandardsAdminPanel.tsx`、`frontend/src/standards/StandardsAdminPanel.test.tsx`
- 修改：`frontend/src/pages/RatingTreePage.tsx`
- 修改（不提交）：`frontend/scripts/check-loading-browser.mjs`
- 修改（共享）：`docs/ui-design-system.md`

**接口：**
- 产出：侧栏「业务中心」下新增「规范管理」→ `/standards`。规则包弹窗的深链从 `/bridges?standards=1` 改为 `/bridges?rules=1`。

- [ ] **步骤 1：先改测试（写出期望）**

`frontend/src/layouts/AppLayout.test.tsx`：把用例 `highlights exactly one navigation entry`（含上方注释）整段替换为：

```tsx
  // 规范管理是独立页面，所有登录用户都看得到；规则包启停的深链挂在桥梁档案页上，亮的是桥梁档案。
  it("highlights exactly one navigation entry", () => {
    renderAt("/standards?doc=h21");
    const selected = within(navigation())
      .getAllByRole("menuitem")
      .filter((item) => item.classList.contains("ant-menu-item-selected"));
    expect(selected).toHaveLength(1);
    expect(selected[0]).toHaveTextContent("规范管理");
  });

  it("keeps the bridge archive highlighted on the rule management link", () => {
    renderAt("/bridges?rules=1");
    const selected = within(navigation())
      .getAllByRole("menuitem")
      .filter((item) => item.classList.contains("ant-menu-item-selected"));
    expect(selected.map((item) => item.textContent)).toEqual(["桥梁档案"]);
  });

  it("shows the standards library to every signed-in user", () => {
    authState.role = "normal";
    renderAt("/workbench");
    expect(within(navigation()).getByRole("link", { name: "规范管理" })).toHaveAttribute("href", "/standards");
  });
```

`frontend/src/pages/BridgesPage.test.tsx`：
- 把用例名 `"shows standards management only to administrators"` 改为 `"shows rule management only to administrators"`，用例里两处 `{ name: "规范管理" }` 改为 `{ name: "规则管理" }`。
- 在该用例之后加：

```tsx
  it("opens rule management from the ?rules=1 link for administrators", async () => {
    render(<MemoryRouter initialEntries={["/bridges?rules=1"]}><BridgesPage /></MemoryRouter>);
    expect(await screen.findByRole("button", { name: "关闭规则管理" })).toBeInTheDocument();
  });
```

`frontend/src/standards/StandardsAdminPanel.test.tsx`：把 `{ name: "关闭规范管理" }` 改为 `{ name: "关闭规则管理" }`。

- [ ] **步骤 2：跑测试，确认失败**

运行（frontend 目录）：`npx vitest run src/layouts/AppLayout.test.tsx src/pages/BridgesPage.test.tsx src/standards/StandardsAdminPanel.test.tsx`
预期：这几条 FAIL——「规范管理」菜单高亮、普通用户看得到「规范管理」、按钮改名、`?rules=1` 深链、弹窗关闭按钮改名（菜单里还没有指向 `/standards` 的项，按钮和弹窗还叫「规范管理」）。「规则管理深链仍亮桥梁档案」这条改动前就能通过，它防的是以后回归。

- [ ] **步骤 3：改 `AppLayout.tsx`**

- 图标导入：删掉 `SettingOutlined,`，在 `MenuUnfoldOutlined,` 之后加 `ReadOutlined,`。
- `navigation` 改为：

```tsx
const navigation = [
  { to: "/workbench", label: "工作台", icon: <DashboardOutlined /> },
  { to: "/bridges", label: "桥梁档案", icon: <DatabaseOutlined /> },
  { to: "/rating-trees", label: "评定树", icon: <ApartmentOutlined /> },
  { to: "/standards", label: "规范管理", icon: <ReadOutlined /> },
];
```

- 删掉 `const STANDARDS_KEY = "/bridges?standards=1";`，`systemNavigation` 改为：

```tsx
/** 系统管理里的维护页。只有管理员看得到（设计 §22）。 */
const systemNavigation = [
  { to: "/settings/report-templates", label: "报告模板", icon: <FileWordOutlined /> },
  { to: "/settings/report-directory", label: "报告人员与设备", icon: <TeamOutlined /> },
];
```

- `currentPageName` 里，在 `if (pathname.startsWith("/rating-trees")) return "评定树";` 之前加：`  if (pathname.startsWith("/standards")) return "规范管理";`
- `selectedNavigationKey` 连同上方注释替换为：

```tsx
/** 侧栏里哪一项亮。 */
function selectedNavigationKey(pathname: string): string {
  const all = [...navigation, ...systemNavigation];
  return all.find((item) => pathname === item.to || pathname.startsWith(`${item.to}/`))?.to ?? "";
}
```

- 调用处 `selectedNavigationKey(location.pathname, location.search)` 改为 `selectedNavigationKey(location.pathname)`。

- [ ] **步骤 4：改名**

`frontend/src/pages/BridgesPage.tsx`（共享文件，只改这四处）：
- `{ label: "规范管理", cancel: (props) => props.onClose() },` → `{ label: "规则管理", cancel: (props) => props.onClose() },`
- `if (isAdmin && searchParams.get("standards") === "1") setStandardsOpen(true);` → `if (isAdmin && searchParams.get("rules") === "1") setStandardsOpen(true);`
- `if (searchParams.has("standards")) setSearchParams({}, { replace: true });` → `if (searchParams.has("rules")) setSearchParams({}, { replace: true });`
- `<Button aria-label="规范管理" icon={<SafetyCertificateOutlined />} onClick={() => setStandardsOpen(true)}>规范管理</Button>` → `<Button aria-label="规则管理" icon={<SafetyCertificateOutlined />} onClick={() => setStandardsOpen(true)}>规则管理</Button>`

`frontend/src/standards/StandardsAdminPanel.tsx`：
- `title="规范管理"` → `title="规则管理"`
- `closable={{ "aria-label": "关闭规范管理", disabled: busyId !== null }}` → `closable={{ "aria-label": "关闭规则管理", disabled: busyId !== null }}`
- `停用只影响新建年度；历史年度仍保留原规范版本。` → `停用只影响新建年度；历史年度仍保留原规则包版本。`

`frontend/src/pages/RatingTreePage.tsx`：
- `<Button icon={<FileTextOutlined />} onClick={() => navigate("/bridges?standards=1")}>查看规范信息</Button>` → `<Button icon={<FileTextOutlined />} onClick={() => navigate("/bridges?rules=1")}>规则管理</Button>`

`frontend/scripts/check-loading-browser.mjs`（未入库，不提交）：
- `getByRole("button",{name:"规范管理",exact:true})` → `getByRole("button",{name:"规则管理",exact:true})`
- `getByRole("dialog",{name:"规范管理"})` → `getByRole("dialog",{name:"规则管理"})`

`docs/ui-design-system.md`（共享文件）：
- §6.2 里 `用于桥梁档案、规范管理和其他数据管理页。` → `用于桥梁档案和其他数据管理页；规则包的启停是桥梁档案页上的「规则管理」弹窗。`
- §6.4 的 `- 评定树可以采用同类全高双栏结构，但保留自己的树导航语义。` 之后加一行：`- 规范管理（规范原文库）同样采用全高双栏：左栏规范目录，右栏版本切换与原文阅读；原文用浏览器自带的 PDF 阅读器显示。`

- [ ] **步骤 5：跑前端全量测试和类型检查，确认通过**

运行（frontend 目录）：
`npx vitest run`
`npx tsc -b`
预期：全部测试 PASS，包括本次改动的 5 处断言；没有用例再引用「规范管理」按钮或 `?standards=1`。类型检查无错误。

- [ ] **步骤 6：提交（`BridgesPage.tsx`、`docs/ui-design-system.md` 留到任务 12；`check-loading-browser.mjs` 不提交）**

```bash
git add -- frontend/src/layouts/AppLayout.tsx frontend/src/layouts/AppLayout.test.tsx frontend/src/pages/BridgesPage.test.tsx frontend/src/standards/StandardsAdminPanel.tsx frontend/src/standards/StandardsAdminPanel.test.tsx frontend/src/pages/RatingTreePage.tsx
```

提交信息：`feat(frontend): 规范管理移到业务中心，规则包启停改名规则管理`

---

### 任务 11：实测验收（真实后端 + 浏览器）

**文件：**
- 新建（暂存目录，不入库）：`<scratchpad>/make_test_pdf.py`、`<scratchpad>/standard-test.pdf`

**接口：**
- 使用：任务 1–10 的全部产出。

- [ ] **步骤 1：后端全量回归**

先确认没有正在运行的 `bridge-report-backend.exe`；有的话先问用户再停。然后运行：
`cmake --build backend-cpp/build/vs-debug --config Debug --target bridge_report_backend_tests bridge-report-backend`
`powershell -ExecutionPolicy Bypass -File scripts/dev/check-backend-tests.ps1`
预期：`Isolated backend check passed ...`，所有用例通过，路由预检审计不报缺 OPTIONS 的路径。

- [ ] **步骤 2：开发库执行 038（先征得用户同意，只跑这一个文件）**

```powershell
& "D:\PostgreSQL\18\bin\psql.exe" "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system" -v ON_ERROR_STOP=1 -f database/migrations/038_standard_documents.sql
```

预期：两条 `CREATE TABLE` 与 `CREATE INDEX`，没有错误。

- [ ] **步骤 3：生成带书签的测试 PDF**

用 Write 工具写 `<scratchpad>/make_test_pdf.py`：

```python
"""生成一份 3 页、带中文书签的测试 PDF（只用标准库），用来实测规范原文阅读。"""
import sys

TITLES = ["1 总则", "2 术语和符号", "3 评定方法及等级分类"]


def utf16_hex(text: str) -> str:
    return "<FEFF" + text.encode("utf-16-be").hex().upper() + ">"


def build() -> bytes:
    objects: list[bytes] = []

    def add(body: str) -> int:
        objects.append(body.encode("latin-1"))
        return len(objects)

    catalog = add("")  # 1：目录，最后回填
    pages = add("")  # 2：页树，最后回填
    outlines = add("")  # 3：书签根，最后回填
    font = add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    page_ids = []
    for index in range(len(TITLES)):
        stream = f"BT /F1 28 Tf 72 760 Td (Chapter {index + 1}) Tj ET"
        content = add(f"<< /Length {len(stream)} >>\nstream\n{stream}\nendstream")
        page_ids.append(add(
            f"<< /Type /Page /Parent {pages} 0 R /MediaBox [0 0 595 842] "
            f"/Contents {content} 0 R /Resources << /Font << /F1 {font} 0 R >> >> >>"
        ))
    item_ids = list(range(len(objects) + 1, len(objects) + 1 + len(TITLES)))
    for index, title in enumerate(TITLES):
        links = ""
        if index > 0:
            links += f" /Prev {item_ids[index - 1]} 0 R"
        if index + 1 < len(TITLES):
            links += f" /Next {item_ids[index + 1]} 0 R"
        add(f"<< /Title {utf16_hex(title)} /Parent {outlines} 0 R{links} /Dest [{page_ids[index]} 0 R /Fit] >>")
    objects[catalog - 1] = f"<< /Type /Catalog /Pages {pages} 0 R /Outlines {outlines} 0 R /PageMode /UseOutlines >>".encode("latin-1")
    kids = " ".join(f"{page} 0 R" for page in page_ids)
    objects[pages - 1] = f"<< /Type /Pages /Kids [{kids}] /Count {len(page_ids)} >>".encode("latin-1")
    objects[outlines - 1] = f"<< /Type /Outlines /First {item_ids[0]} 0 R /Last {item_ids[-1]} 0 R /Count {len(item_ids)} >>".encode("latin-1")

    output = bytearray(b"%PDF-1.4\n")
    offsets = []
    for number, body in enumerate(objects, start=1):
        offsets.append(len(output))
        output += f"{number} 0 obj\n".encode("latin-1") + body + b"\nendobj\n"
    xref = len(output)
    output += f"xref\n0 {len(objects) + 1}\n0000000000 65535 f \n".encode("latin-1")
    for offset in offsets:
        output += f"{offset:010d} 00000 n \n".encode("latin-1")
    output += f"trailer\n<< /Size {len(objects) + 1} /Root {catalog} 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode("latin-1")
    return bytes(output)


if __name__ == "__main__":
    with open(sys.argv[1], "wb") as handle:
        handle.write(build())
```

运行：`tools-python/.venv/Scripts/python.exe -I <scratchpad>/make_test_pdf.py <scratchpad>/standard-test.pdf`
预期：生成一个约 1.5 KB 的 PDF。用 Edge 打开时，侧栏能看到三条中文书签。

- [ ] **步骤 4：起服务并打开页面**

- 后端：`preview_start` 启动 `c++-backend`。如果用户自己的后端占着 18080，先问是否重启。
- 前端：用户已经开着 5173 时直接用；没开就 `preview_start` 启动 `frontend`。
- 在浏览器面板打开 `http://127.0.0.1:5173/standards`。出现登录页时，请用户用管理员账号登录；不代填任何凭据。

- [ ] **步骤 5：走一遍管理员流程，并逐项核对**

1. 「新增规范」：名称填「测试规范」，分类「技术状况评定」，正式版，实施日期 2011-09-01，选测试 PDF。
   - 选文件：请用户在面板里点选。用户希望全自动时，用 `javascript_tool` 把测试 PDF 的 base64 用 `DataTransfer` 注入 `input[type=file]`，再派发 `change` 事件。
   - 核对：左栏出现该规范；右栏 iframe 显示 PDF，阅读器侧栏能看到书签。
2. 「上传新版本」：报批稿，稿件日期 2026-02-11。
   - 核对：版本下拉分「正式版本」「修订稿（未发布）」两组；切到报批稿后出现黄色提示，点「查看现行版本」能回到 2011 版。
3. 再上传一个正式版，实施日期 2021-11-01。
   - 核对：2011 版变为「已废止」，版本记录写「2021-11-01 起由 … 替代」。
4. 删掉 2021 正式版。
   - 核对：2011 版恢复「现行」。
5. 「下载原文」。
   - 核对：在网络请求里看到 `Content-Disposition: attachment; filename*=UTF-8''…`，文件名是「{编号} 测试规范.pdf」。
6. 地址栏加 `&page=2` 后刷新。
   - 核对：阅读器跳到第 2 页。
7. 「替换原文」。
   - 核对：iframe 地址里的 `v=` 变了。
8. 「更多 → 删除规范」。
   - 核对：规范从左栏消失。
9. 桥梁档案页。
   - 核对：按钮叫「规则管理」，点开是规则包启停弹窗；评定树页的「规则管理」按钮也能打开同一个弹窗。
10. 用户若有普通账号，请其登录一次。
    - 核对：能看到「规范管理」，但没有新增、编辑、替换、删除等按钮。

每一项截一张图，最后挑关键的几张交给用户。

- [ ] **步骤 6：记录结果**

在交付说明里逐项写明「通过」或「未通过（原因）」。未通过的回到对应任务修复，然后重跑相关测试。

---

### 任务 12：共享文件的外科式提交与收尾

**文件：**
- 提交：`backend-cpp/CMakeLists.txt`、`backend-cpp/src/main.cpp`、`frontend/src/App.tsx`、`frontend/src/pages/BridgesPage.tsx`、`docs/ui-design-system.md` 中属于本功能的行。

**接口：**
- 无代码接口。前提：任务 1–11 完成，用户同意提交。

- [ ] **步骤 1：核对工作区**

运行：`git status --short` 与 `git diff --stat -- backend-cpp/CMakeLists.txt backend-cpp/src/main.cpp frontend/src/App.tsx frontend/src/pages/BridgesPage.tsx docs/ui-design-system.md`
预期：这 5 个文件既有他人的改动，也有本功能的改动。本功能在每个文件里加的行数如下，核对差值时用：
- `backend-cpp/CMakeLists.txt`：+8 行（4 个源文件、4 个测试）
- `backend-cpp/src/main.cpp`：+2 行
- `frontend/src/App.tsx`：+2 行
- `frontend/src/pages/BridgesPage.tsx`：改 4 行
- `docs/ui-design-system.md`：改 1 行、加 1 行

- [ ] **步骤 2：逐个文件构造「HEAD + 只有本功能的改动」并写入索引**

在 Git Bash 里，对每个文件 `P`（以 `backend-cpp/CMakeLists.txt` 为例）：

```bash
S="<scratchpad>"
git show HEAD:backend-cpp/CMakeLists.txt > "$S/staged-CMakeLists.txt"
```

然后用 Edit 工具，在 `$S/staged-CMakeLists.txt` 上重放本功能对该文件的编辑，内容与任务 2–5、9、10 写明的完全相同。重放完成后：

```bash
sha=$(git hash-object -w --path backend-cpp/CMakeLists.txt "$S/staged-CMakeLists.txt")
git update-index --cacheinfo 100644,"$sha",backend-cpp/CMakeLists.txt
```

其余四个文件照同样做法处理。`main.cpp` 重放任务 5 步骤 6；`App.tsx` 重放任务 9 步骤 4；`BridgesPage.tsx` 和 `docs/ui-design-system.md` 重放任务 10 步骤 4。

- [ ] **步骤 3：核对暂存内容**

运行：`git diff --cached -- backend-cpp/CMakeLists.txt backend-cpp/src/main.cpp frontend/src/App.tsx frontend/src/pages/BridgesPage.tsx docs/ui-design-system.md`
预期：只有本功能的行，行数与步骤 1 列出的一致。再运行 `git diff -- <同样 5 个文件>`，剩下的应只有他人的改动。

- [ ] **步骤 4：提交**

提交信息：`chore: 规范原文库接入构建、路由与菜单`（正文写明：只含本功能的行，同文件里他人的在制品未纳入）。

- [ ] **步骤 5：收尾**

- 再跑一次 `git status`，确认他人的文件和改动都原样留在工作区；`frontend/scripts/check-loading-browser.mjs` 的两处改名作为未入库文件留在工作区，交付时告诉用户。
- 告诉用户三件事：
  - 开发库已执行 038。
  - 上线前要把 H21 等原文上传进去。
  - 评定树按页码链接原文、自绘目录树都是下一步，属于设计 §12 的升级项。
