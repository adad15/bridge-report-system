-- 028 解析审计留存与人工裁决复核提示
--
-- 两件事，都属于 027 落地后暴露出来的缺口。

-- ---------------------------------------------------------------------------
-- 1. 审计事件不再随业务对象一起消失
-- ---------------------------------------------------------------------------
-- import_resolution_events 是追加式审计：当前状态表只表达当前结果，这里只表达历史。
-- 但它对组和实例用的是 on delete cascade，而重绑定（删目标重建实例）、删候选、重开
-- 恢复快照这些**正常业务操作**都会删掉那些行——历史于是跟着一起没了，恰恰在最需要
-- 回溯"这条绑定是怎么变成今天这样"的时候。
--
-- 改成 set null：事件行留下来，指向的实体没了就置空。为此另存一份 id 快照，
-- 置空之后仍看得出这条事件当初说的是哪个对象。
alter table import_resolution_events
  add column if not exists group_id_snapshot uuid,
  add column if not exists resolved_defect_instance_id_snapshot uuid;

-- 存量事件回填一次，避免新旧行口径不一。
update import_resolution_events
   set group_id_snapshot = coalesce(group_id_snapshot, group_id),
       resolved_defect_instance_id_snapshot =
         coalesce(resolved_defect_instance_id_snapshot, resolved_defect_instance_id)
 where group_id_snapshot is null
    or resolved_defect_instance_id_snapshot is null;

alter table import_resolution_events
  drop constraint if exists import_resolution_events_group_id_fkey;
alter table import_resolution_events
  add constraint import_resolution_events_group_id_fkey
  foreign key (group_id) references import_component_resolution_groups(id)
  on delete set null;

alter table import_resolution_events
  drop constraint if exists import_resolution_events_resolved_defect_instance_id_fkey;
alter table import_resolution_events
  add constraint import_resolution_events_resolved_defect_instance_id_fkey
  foreign key (resolved_defect_instance_id) references import_resolved_defect_instances(id)
  on delete set null;

-- ---------------------------------------------------------------------------
-- 2. 人工裁决时的输入哈希
-- ---------------------------------------------------------------------------
-- §8.5 说人工选择要在文字变化后继续保留，同时让界面提示"内容变过，请复核"。前一半
-- 做到了，后一半没有：保留裁决时代码把 match_input_hash 一起更新成了最新值，于是
-- "裁决当时的输入是什么"这个信息当场丢失，无从比较，工作区里那个
-- content_changed_after_manual_resolution 永远是 false。
--
-- 单独存一份裁决时的哈希。match_input_hash 继续跟着当前有效事实走（失效判定要用它），
-- 这一列只在人工裁决那一刻写入，之后不动。
alter table import_rating_resolutions
  add column if not exists resolved_match_input_hash text;

comment on column import_rating_resolutions.resolved_match_input_hash is
  '人工裁决那一刻的 match_input_hash；与当前值不同即表示裁决后内容变过，供界面提示复核。仅 match_method = manual 时有值。';

-- 存量的人工裁决按"裁决后未变过"回填：没有历史可查，假设变过会让每条既有人工结果
-- 都挂上复核提示，那是噪声而不是信息。
update import_rating_resolutions
   set resolved_match_input_hash = match_input_hash
 where match_method = 'manual'
   and resolved_match_input_hash is null;

-- 非人工结果不该带这一列：它只在人工裁决时有意义。
alter table import_rating_resolutions
  drop constraint if exists import_rating_resolution_manual_hash_scope;
alter table import_rating_resolutions
  add constraint import_rating_resolution_manual_hash_scope
  check (resolved_match_input_hash is null or match_method = 'manual');
