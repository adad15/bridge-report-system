-- 036: 桥梁图件。
--
-- 报告 §1.1 除了那几段叙述，还要几张图：地理位置图、桥型布置图、横断面图，以及桥梁
-- 全貌、桥面、桥下三张照片。它们和病害照片不是一回事——病害照片属于某个年度的某条
-- 病害，这些图属于桥本身，换一个年度还是同一张。
--
-- 文件本身走已有的 `archived_files`，落盘、去重、清理都不用另起一套；这张表只记
-- 「哪座桥的哪个槽位用哪个文件」。
--
-- **一个槽位一张图。** 报告里每张图有固定的图号和题注，多放一张就不知道该印哪张。
-- 要换图就覆盖，旧的归档文件由删除流程清掉。

create table if not exists bridge_media (
  id uuid primary key default gen_random_uuid(),
  bridge_id uuid not null references bridges(id) on delete cascade,
  slot text not null check (slot in (
    'LOCATION_MAP',     -- 地理位置图
    'LAYOUT_DRAWING',   -- 桥型布置图
    'CROSS_SECTION',    -- 横断面图
    'OVERVIEW_PHOTO',   -- 桥梁全貌照片
    'DECK_PHOTO',       -- 桥面照片
    'UNDERSIDE_PHOTO'   -- 桥下照片
  )),
  -- restrict 而不是 cascade：归档文件先删、这条记录还在，报告就会指向一个不存在的
  -- 文件。删图必须走「先删这条、再删文件」的顺序。
  archived_file_id uuid not null references archived_files(id) on delete restrict,
  -- 图的来源，排障用：人工上传还是按坐标生成的。
  source text not null default '人工上传' check (source in ('人工上传', '按坐标生成')),
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  constraint bridge_media_one_per_slot unique (bridge_id, slot)
);

comment on table bridge_media is
  '桥梁图件：报告 §1.1 用的地理位置图、桥型布置图、横断面图与三张桥梁照片。'
  '一个槽位一张图，换图即覆盖。文件存在 archived_files 里。';
comment on column bridge_media.slot is
  '槽位代码，决定这张图在报告里的图号与题注。与 Python 侧的报告契约共用同一批代码。';
comment on column bridge_media.source is
  '人工上传，或在桥梁总览的地图上按坐标生成。生成的图同样是一个普通归档文件。';

create index if not exists bridge_media_bridge_idx on bridge_media (bridge_id);
