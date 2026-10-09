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
