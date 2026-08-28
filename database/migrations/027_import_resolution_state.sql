-- 027：把构件解析与评分树解析从 import_records.parsed_result_json 拆成关系表。
-- 设计：docs/superpowers/specs/2026-08-27-import-component-rating-resolution-separation-design.md
-- 依赖 002（import_records / bridge_components）、004（users）、011（台账版本）、018（评定树）。
-- 幂等可复跑；不回填既有导入记录——5.0 是破坏性合同升级，存量测试数据重新导入。

-- ---------------------------------------------------------------------------
-- 8.0 来源草稿并发版本
-- ---------------------------------------------------------------------------
-- 普通草稿保存和手工新增病害都会改 parsed_result_json。编辑锁只管"谁在编辑"，
-- 挡不住同一用户两个标签页拿着同一个 token 先后覆盖：后提交的那份整份草稿会把
-- 手工新增刚写进去的候选当成"用户删掉了"。
alter table import_records
  add column if not exists draft_version integer not null default 1;

alter table import_records
  drop constraint if exists import_records_draft_version_positive;
alter table import_records
  add constraint import_records_draft_version_positive
  check (draft_version >= 1);

-- ---------------------------------------------------------------------------
-- 8.1 来源构件组
-- ---------------------------------------------------------------------------
create table if not exists import_component_resolution_groups (
  id uuid primary key default gen_random_uuid(),
  import_record_id uuid not null references import_records(id) on delete cascade,
  source_component_name text not null,
  -- 报告原文，仅供展示与审计；任何判定都走 normalized_component_number。
  source_component_number text,
  -- 非空是唯一约束能成立的前提：编号缺失时落空串。写 NULL 的话 PostgreSQL 认为
  -- NULL 互不相等，唯一约束整条失效，每条无编号病害各成一组。
  normalized_component_number text not null,
  resolution_mode text not null default 'single'
    check (resolution_mode in ('single', 'multi', 'range')),
  status text not null default 'unresolved'
    check (status in ('unresolved', 'bound', 'missing')),
  match_method text
    check (match_method is null or match_method in
      ('exact', 'confirmed_alias', 'manual', 'side_pair', 'range')),
  -- 可空：导入时该桥可能还没有已确认台账版本，那种情况下校对照常进行，
  -- 组停在 unresolved，等台账确认后由 inventory_repoint 计划统一重指。
  inventory_revision_id uuid
    references bridge_component_inventory_revisions(id) on delete restrict,
  version integer not null default 1 check (version >= 1),
  resolved_by_user_id uuid references users(id) on delete restrict,
  resolved_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  unique (import_record_id, source_component_name, normalized_component_number),
  -- 成员表要靠它做"成员与组同属一个导入"的复合外键。
  unique (id, import_record_id),
  constraint import_resolution_group_bound_requires_revision
    check (status <> 'bound' or inventory_revision_id is not null),
  constraint import_resolution_group_unbound_has_no_method
    check (status <> 'unresolved' or match_method is null)
);

create index if not exists ix_import_resolution_groups_record_status
  on import_component_resolution_groups (import_record_id, status);

create index if not exists ix_import_resolution_groups_record_part
  on import_component_resolution_groups (import_record_id, source_component_name);

-- ---------------------------------------------------------------------------
-- 8.2 来源病害成员
-- ---------------------------------------------------------------------------
create table if not exists import_component_group_members (
  id uuid primary key default gen_random_uuid(),
  import_record_id uuid not null references import_records(id) on delete cascade,
  group_id uuid not null,
  -- parsed_result_json.defects[].candidate_id；JSON 内的身份，做不了真外键。
  source_candidate_id text not null check (length(btrim(source_candidate_id)) > 0),
  source_order integer not null check (source_order >= 0),
  created_at timestamptz not null default now(),
  unique (import_record_id, source_candidate_id),
  unique (id, group_id),
  -- 复合外键同时锁住"组存在"和"成员与组同属一个导入记录"。
  foreign key (group_id, import_record_id)
    references import_component_resolution_groups (id, import_record_id)
    on delete cascade
);

create index if not exists ix_import_group_members_group_order
  on import_component_group_members (group_id, source_order);

-- ---------------------------------------------------------------------------
-- 8.3 构件解析目标
-- ---------------------------------------------------------------------------
create table if not exists import_component_resolution_targets (
  id uuid primary key default gen_random_uuid(),
  group_id uuid not null
    references import_component_resolution_groups(id) on delete cascade,
  bridge_component_id uuid not null references bridge_components(id) on delete restrict,
  target_order integer not null check (target_order >= 1),
  target_role text not null default 'primary'
    check (target_role in ('primary', 'left', 'right', 'range_member')),
  created_at timestamptz not null default now(),
  unique (group_id, bridge_component_id),
  unique (group_id, target_order),
  unique (id, group_id)
);

-- 目标必须属于同一桥梁，且属于组当时钉住的台账版本。跨表条件，只能用触发器。
create or replace function validate_import_resolution_target()
returns trigger
language plpgsql
as $$
declare
  v_group_bridge_id uuid;
  v_group_revision_id uuid;
  v_component_bridge_id uuid;
begin
  select r.bridge_id, g.inventory_revision_id
    into v_group_bridge_id, v_group_revision_id
  from import_component_resolution_groups g
  join import_records r on r.id = g.import_record_id
  where g.id = new.group_id;

  select bridge_id into v_component_bridge_id
  from bridge_components where id = new.bridge_component_id;

  if v_component_bridge_id is distinct from v_group_bridge_id then
    raise exception using
      errcode = '23514',
      constraint = 'import_resolution_target_same_bridge',
      message = 'resolution target must belong to the same bridge as the import record';
  end if;

  if v_group_revision_id is null then
    raise exception using
      errcode = '23514',
      constraint = 'import_resolution_target_requires_revision',
      message = 'resolution target requires the group to pin a confirmed inventory revision';
  end if;

  if not exists (
    select 1 from bridge_component_inventory_entries e
    where e.inventory_revision_id = v_group_revision_id
      and e.bridge_component_id = new.bridge_component_id
      and e.is_active
  ) then
    raise exception using
      errcode = '23514',
      constraint = 'import_resolution_target_in_group_revision',
      message = 'resolution target must be an active entry of the revision pinned by the group';
  end if;

  return new;
end
$$;

drop trigger if exists trg_import_resolution_target_scope
  on import_component_resolution_targets;
create trigger trg_import_resolution_target_scope
before insert or update of group_id, bridge_component_id
on import_component_resolution_targets
for each row execute function validate_import_resolution_target();

-- 组状态与目标集合必须一致。绑定是"先写组、再写目标"，所以只能延迟到事务末尾检查。
create or replace function validate_import_resolution_group_targets()
returns trigger
language plpgsql
as $$
declare
  v_group_id uuid;
  v_status text;
  v_target_count integer;
begin
  -- 不能把两张表的取值写进一个 CASE：plpgsql 对 record 的字段访问是提前解析的，
  -- 在组表上求值 new.group_id 会直接报"没有字段"，哪怕那个分支根本不该走到。
  if tg_table_name = 'import_component_resolution_groups' then
    v_group_id := new.id;
  elsif tg_op = 'DELETE' then
    v_group_id := old.group_id;
  else
    v_group_id := new.group_id;
  end if;

  select status into v_status
  from import_component_resolution_groups where id = v_group_id;
  -- 组已在同一事务里被删掉：级联删除会带走目标，没有什么可校验的。
  if v_status is null then
    return null;
  end if;

  select count(*) into v_target_count
  from import_component_resolution_targets where group_id = v_group_id;

  if v_status = 'bound' and v_target_count = 0 then
    raise exception using
      errcode = '23514',
      constraint = 'import_resolution_group_bound_needs_target',
      message = 'a bound component resolution group must keep at least one target';
  end if;

  if v_status <> 'bound' and v_target_count > 0 then
    raise exception using
      errcode = '23514',
      constraint = 'import_resolution_group_unbound_has_targets',
      message = 'only a bound component resolution group may keep resolution targets';
  end if;

  return null;
end
$$;

drop trigger if exists trg_import_resolution_group_targets
  on import_component_resolution_groups;
create constraint trigger trg_import_resolution_group_targets
after insert or update of status
on import_component_resolution_groups
deferrable initially deferred
for each row execute function validate_import_resolution_group_targets();

drop trigger if exists trg_import_resolution_target_group_status
  on import_component_resolution_targets;
create constraint trigger trg_import_resolution_target_group_status
after insert or delete
on import_component_resolution_targets
deferrable initially deferred
for each row execute function validate_import_resolution_group_targets();

-- ---------------------------------------------------------------------------
-- 8.4 病害解析实例
-- ---------------------------------------------------------------------------
-- 覆盖 JSON 的白名单：字段名、类型、可空性一起校验。CHECK 里不能写子查询，
-- 所以把判定收进一个 immutable 函数。
create or replace function import_resolution_overrides_are_valid(overrides jsonb)
returns boolean
language sql
immutable
as $$
  select overrides is not null
     and jsonb_typeof(overrides) = 'object'
     and not exists (
       select 1
       from jsonb_each(overrides) as entry(key, value)
       where
         -- 未知字段一律拒绝：覆盖 JSON 不是第二个 parsed_result。
         key not in (
           'defect_type', 'defect_location', 'defect_scale', 'defect_description',
           'quantity_text', 'measurement_text', 'measurements', 'remark'
         )
         -- 必填事实不能借覆盖写成 null；"清除覆盖"是删掉这个键。
         or (key in ('defect_type', 'defect_location', 'defect_description')
             and jsonb_typeof(value) <> 'string')
         or (key in ('quantity_text', 'measurement_text', 'remark')
             and jsonb_typeof(value) not in ('string', 'null'))
         or (key = 'measurements' and jsonb_typeof(value) <> 'array')
         or (key = 'defect_scale'
             and (jsonb_typeof(value) not in ('number', 'null')
                  or (jsonb_typeof(value) = 'number'
                      and (value::numeric <> trunc(value::numeric)
                           or value::numeric <= 0))))
     );
$$;

create table if not exists import_resolved_defect_instances (
  id uuid primary key default gen_random_uuid(),
  group_member_id uuid not null
    references import_component_group_members(id) on delete cascade,
  target_id uuid not null
    references import_component_resolution_targets(id) on delete cascade,
  instance_order integer not null check (instance_order >= 1),
  instance_status text not null default 'active'
    check (instance_status in ('active', 'ignored')),
  is_photo_owner boolean not null default false,
  fact_overrides_json jsonb not null default '{}'::jsonb,
  -- 生成/重刷这条实例时依据的构件解析世代，不是本行的并发版本。
  component_resolution_version integer not null check (component_resolution_version >= 1),
  version integer not null default 1 check (version >= 1),
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  unique (group_member_id, target_id),
  unique (group_member_id, instance_order),
  constraint import_resolved_instance_overrides_allowed
    check (import_resolution_overrides_are_valid(fact_overrides_json)),
  -- 被忽略的实例不入库，也就不可能继承照片。
  constraint import_resolved_instance_photo_owner_is_active
    check (not is_photo_owner or instance_status = 'active')
);

-- 一条来源病害至多一个照片归属者。"至少一个"由服务层在重算时保证——
-- 全部实例被忽略时本就该没有归属者。
create unique index if not exists ux_import_resolved_instance_photo_owner
  on import_resolved_defect_instances (group_member_id)
  where is_photo_owner;

create index if not exists ix_import_resolved_instance_target
  on import_resolved_defect_instances (target_id);

-- ---------------------------------------------------------------------------
-- 8.5 评分树解析
-- ---------------------------------------------------------------------------
create table if not exists import_rating_resolutions (
  resolved_defect_instance_id uuid primary key
    references import_resolved_defect_instances(id) on delete cascade,
  rating_tree_version_id uuid not null
    references rating_tree_versions(id) on delete restrict,
  rating_tree_node_id uuid references rating_tree_nodes(id) on delete restrict,
  standard_defect_indicator_id text,
  status text not null default 'unresolved'
    check (status in ('unresolved', 'matched')),
  match_method text
    check (match_method is null or match_method in
      ('exact', 'controlled_alias', 'controlled_keyword', 'fuzzy_candidate',
       'source_indicator', 'manual')),
  match_evidence_json jsonb not null default '{}'::jsonb
    check (jsonb_typeof(match_evidence_json) = 'object'),
  component_resolution_version integer not null check (component_resolution_version >= 1),
  -- 适用性输入与文字输入分开存：人工选的节点只在适用性变化时失效，
  -- 改几个字不该把人工判断冲掉。
  applicability_hash text not null check (length(btrim(applicability_hash)) > 0),
  match_input_hash text not null check (length(btrim(match_input_hash)) > 0),
  version integer not null default 1 check (version >= 1),
  resolved_by_user_id uuid references users(id) on delete restrict,
  resolved_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  constraint import_rating_resolution_matched_needs_node
    check (status <> 'matched' or rating_tree_node_id is not null),
  constraint import_rating_resolution_unresolved_keeps_nothing
    check (status <> 'unresolved'
           or (rating_tree_node_id is null and standard_defect_indicator_id is null)),
  constraint import_rating_resolution_manual_needs_actor
    check (match_method is distinct from 'manual' or resolved_by_user_id is not null)
);

-- 节点必须属于本行声明的评定树版本：跨版本引用会让确认期的适用性校验失去意义。
create or replace function validate_import_rating_resolution_node()
returns trigger
language plpgsql
as $$
declare
  v_node_version_id uuid;
begin
  if new.rating_tree_node_id is null then
    return new;
  end if;

  select rating_tree_version_id into v_node_version_id
  from rating_tree_nodes where id = new.rating_tree_node_id;

  if v_node_version_id is distinct from new.rating_tree_version_id then
    raise exception using
      errcode = '23514',
      constraint = 'import_rating_resolution_node_version',
      message = 'rating tree node must belong to the declared rating tree version';
  end if;

  return new;
end
$$;

drop trigger if exists trg_import_rating_resolution_node
  on import_rating_resolutions;
create trigger trg_import_rating_resolution_node
before insert or update of rating_tree_node_id, rating_tree_version_id
on import_rating_resolutions
for each row execute function validate_import_rating_resolution_node();

-- ---------------------------------------------------------------------------
-- 8.6 预览计划
-- ---------------------------------------------------------------------------
create table if not exists import_resolution_operation_plans (
  id uuid primary key default gen_random_uuid(),
  import_record_id uuid not null references import_records(id) on delete cascade,
  actor_user_id uuid not null references users(id) on delete restrict,
  operation_type text not null
    check (operation_type in ('bulk_replace', 'range_expand', 'inventory_repoint')),
  -- 绑在锁的身份而不是锁的到期时刻上：锁 TTL 只有 2 分钟并靠心跳续租，
  -- 按到期时刻截断会让任何计划都活不过两分钟。
  lock_token_hash text not null check (length(btrim(lock_token_hash)) > 0),
  request_json jsonb not null check (jsonb_typeof(request_json) = 'object'),
  plan_json jsonb not null check (jsonb_typeof(plan_json) = 'object'),
  preconditions_json jsonb not null check (jsonb_typeof(preconditions_json) = 'object'),
  status text not null default 'ready'
    check (status in ('ready', 'applied', 'expired', 'invalidated')),
  expires_at timestamptz not null,
  applied_at timestamptz,
  apply_result_json jsonb
    check (apply_result_json is null or jsonb_typeof(apply_result_json) = 'object'),
  invalidated_reason text,
  created_at timestamptz not null default now(),
  -- 幂等重放要拿得到首次结果，所以 applied 必须同时留下时间和结果。
  constraint import_resolution_plan_applied_keeps_result
    check (status <> 'applied'
           or (applied_at is not null and apply_result_json is not null)),
  constraint import_resolution_plan_unapplied_has_no_result
    check (status = 'applied' or (applied_at is null and apply_result_json is null))
);

create index if not exists ix_import_resolution_plans_record_status
  on import_resolution_operation_plans (import_record_id, status);

-- ---------------------------------------------------------------------------
-- 8.7 解析审计事件
-- ---------------------------------------------------------------------------
-- 追加式：当前状态表只表达当前结果，这里只表达历史，两者不混用。
create table if not exists import_resolution_events (
  id uuid primary key default gen_random_uuid(),
  import_record_id uuid not null references import_records(id) on delete cascade,
  group_id uuid references import_component_resolution_groups(id) on delete cascade,
  resolved_defect_instance_id uuid
    references import_resolved_defect_instances(id) on delete cascade,
  plan_id uuid references import_resolution_operation_plans(id) on delete set null,
  operation_type text not null check (length(btrim(operation_type)) > 0),
  before_json jsonb not null default '{}'::jsonb
    check (jsonb_typeof(before_json) = 'object'),
  after_json jsonb not null default '{}'::jsonb
    check (jsonb_typeof(after_json) = 'object'),
  actor_user_id uuid references users(id) on delete set null,
  occurred_at timestamptz not null default now()
);

create index if not exists ix_import_resolution_events_record_time
  on import_resolution_events (import_record_id, occurred_at desc);

create index if not exists ix_import_resolution_events_group
  on import_resolution_events (group_id, occurred_at desc);

-- ---------------------------------------------------------------------------
-- 8.8 重开校对快照
-- ---------------------------------------------------------------------------
-- 现有 reopen_backup_parsed_result_json 只还原来源草稿，还原不了关系表。
create table if not exists import_resolution_reopen_snapshots (
  import_record_id uuid primary key references import_records(id) on delete cascade,
  snapshot_json jsonb not null check (jsonb_typeof(snapshot_json) = 'object'),
  checksum text not null check (length(btrim(checksum)) > 0),
  created_by_user_id uuid references users(id) on delete set null,
  created_at timestamptz not null default now()
);
