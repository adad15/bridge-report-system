-- 013：系统自主评定运行、各级结果、单项控制和规则执行轨迹。
-- 依赖 002、003、010、011；幂等可复跑，不迁移或删除旧 Word 评分。

create table if not exists assessment_runs (
  id uuid primary key default gen_random_uuid(),
  inspection_year_id uuid not null
    references inspection_years(id) on delete restrict,
  source_import_record_id uuid
    references import_records(id) on delete set null,
  run_kind text not null check (run_kind in ('试算', '正式')),
  formal_revision_number integer check (formal_revision_number > 0),
  supersedes_run_id uuid references assessment_runs(id) on delete restrict,
  technical_condition_package_id uuid
    references standard_packages(id) on delete restrict,
  standard_profile_id uuid
    references project_standard_profiles(id) on delete restrict,
  component_inventory_revision_id uuid
    references bridge_component_inventory_revisions(id) on delete restrict,
  result_status text not null
    check (result_status in ('运行中', '成功', '阻断', '失败')),
  is_current boolean not null default false,
  input_summary_json jsonb not null,
  input_checksum text not null
    check (input_checksum ~ '^sha256:[0-9a-f]{64}$'),
  rule_package_summary_json jsonb not null,
  rule_package_checksum text not null
    check (rule_package_checksum ~ '^sha256:[0-9a-f]{64}$'),
  result_summary_json jsonb not null default '{}'::jsonb,
  error_code text,
  error_message text,
  created_by_user_id uuid not null references users(id) on delete restrict,
  confirmed_by_user_id uuid references users(id) on delete restrict,
  confirmed_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  constraint assessment_runs_input_summary_check check (
    jsonb_typeof(input_summary_json) = 'object'
    and input_summary_json <> '{}'::jsonb
  ),
  constraint assessment_runs_rule_package_summary_check check (
    jsonb_typeof(rule_package_summary_json) = 'object'
    and rule_package_summary_json <> '{}'::jsonb
  ),
  constraint assessment_runs_result_summary_check check (
    jsonb_typeof(result_summary_json) = 'object'
  ),
  constraint assessment_runs_kind_context_check check (
    (
      run_kind = '试算'
      and formal_revision_number is null
      and supersedes_run_id is null
      and not is_current
      and confirmed_by_user_id is null
      and confirmed_at is null
    )
    or (
      run_kind = '正式'
      and formal_revision_number is not null
      and technical_condition_package_id is not null
      and standard_profile_id is not null
      and component_inventory_revision_id is not null
    )
  ),
  constraint assessment_runs_confirmation_check check (
    (
      run_kind = '正式'
      and result_status = '成功'
      and confirmed_by_user_id is not null
      and confirmed_at is not null
    )
    or (
      (run_kind <> '正式' or result_status <> '成功')
      and confirmed_by_user_id is null
      and confirmed_at is null
      and not is_current
    )
  ),
  constraint assessment_runs_current_check check (
    not is_current or (run_kind = '正式' and result_status = '成功')
  ),
  constraint assessment_runs_error_detail_check check (
    (result_status = '成功' and error_code is null and error_message is null)
    or result_status <> '成功'
  ),
  constraint assessment_runs_supersedes_self_check
    check (supersedes_run_id is null or supersedes_run_id <> id)
);

create unique index if not exists ux_assessment_runs_formal_revision
  on assessment_runs (inspection_year_id, formal_revision_number)
  where run_kind = '正式';
create unique index if not exists ux_assessment_runs_current_formal
  on assessment_runs (inspection_year_id)
  where run_kind = '正式' and is_current;
create index if not exists ix_assessment_runs_year_created
  on assessment_runs (inspection_year_id, created_at desc);
create index if not exists ix_assessment_runs_package
  on assessment_runs (technical_condition_package_id)
  where technical_condition_package_id is not null;
create index if not exists ix_assessment_runs_inventory_revision
  on assessment_runs (component_inventory_revision_id)
  where component_inventory_revision_id is not null;

create or replace function validate_assessment_run_context()
returns trigger
language plpgsql
as $$
declare
  v_year_bridge_id uuid;
  v_year_profile_id uuid;
  v_profile_technical_package_id uuid;
  v_package_family text;
  v_package_checksum text;
  v_inventory_bridge_id uuid;
  v_inventory_status text;
  v_superseded_year_id uuid;
  v_superseded_kind text;
  v_superseded_status text;
  v_superseded_revision integer;
begin
  select bridge_id, standard_profile_id
    into v_year_bridge_id, v_year_profile_id
  from inspection_years where id = new.inspection_year_id;

  if new.technical_condition_package_id is not null then
    select standard_family, content_checksum
      into v_package_family, v_package_checksum
    from standard_packages where id = new.technical_condition_package_id;
    if v_package_family is distinct from 'technical_condition' then
      raise exception using
        errcode = '23514',
        constraint = 'assessment_runs_technical_package_family',
        message = 'assessment run requires a technical-condition package';
    end if;
    if v_package_checksum is distinct from new.rule_package_checksum then
      raise exception using
        errcode = '23514',
        constraint = 'assessment_runs_rule_package_checksum',
        message = 'assessment run rule package checksum does not match the locked standard package';
    end if;
  end if;

  if new.standard_profile_id is not null then
    select technical_condition_package_id
      into v_profile_technical_package_id
    from project_standard_profiles where id = new.standard_profile_id;
    if v_profile_technical_package_id is distinct from new.technical_condition_package_id then
      raise exception using
        errcode = '23514',
        constraint = 'assessment_runs_profile_package_context',
        message = 'assessment run package does not match its project standard profile';
    end if;
    if v_year_profile_id is not null
       and v_year_profile_id is distinct from new.standard_profile_id then
      raise exception using
        errcode = '23514',
        constraint = 'assessment_runs_year_profile_context',
        message = 'assessment run profile does not match its inspection year';
    end if;
  end if;

  if new.component_inventory_revision_id is not null then
    select bridge_id, status into v_inventory_bridge_id, v_inventory_status
    from bridge_component_inventory_revisions
    where id = new.component_inventory_revision_id;
    if v_inventory_bridge_id is distinct from v_year_bridge_id then
      raise exception using
        errcode = '23514',
        constraint = 'assessment_runs_inventory_bridge_context',
        message = 'assessment run inventory and inspection year must belong to the same bridge';
    end if;
    if v_inventory_status is distinct from '已确认' then
      raise exception using
        errcode = '23514',
        constraint = 'assessment_runs_inventory_confirmed',
        message = 'assessment run requires a confirmed component inventory revision';
    end if;
  end if;

  if new.supersedes_run_id is not null then
    select inspection_year_id, run_kind, result_status, formal_revision_number
      into v_superseded_year_id, v_superseded_kind,
           v_superseded_status, v_superseded_revision
    from assessment_runs where id = new.supersedes_run_id;
    if v_superseded_year_id is distinct from new.inspection_year_id
       or v_superseded_kind is distinct from '正式'
       or v_superseded_status is distinct from '成功'
       or v_superseded_revision >= new.formal_revision_number then
      raise exception using
        errcode = '23514',
        constraint = 'assessment_runs_supersedes_context',
        message = 'formal assessment revision must supersede an earlier successful run of the same year';
    end if;
  end if;

  return new;
end
$$;

create or replace function protect_completed_formal_assessment_run()
returns trigger
language plpgsql
as $$
declare
  v_normalized assessment_runs%rowtype;
begin
  if old.run_kind <> '正式' or old.result_status <> '成功' then
    return case when tg_op = 'DELETE' then old else new end;
  end if;

  if tg_op = 'DELETE' then
    raise exception using
      errcode = '23514',
      constraint = 'assessment_runs_completed_formal_immutable',
      message = 'completed formal assessment run cannot be deleted';
  end if;

  v_normalized := new;
  v_normalized.is_current := old.is_current;
  v_normalized.updated_at := old.updated_at;
  if old.is_current and not new.is_current
     and v_normalized is not distinct from old then
    return new;
  end if;

  if new is distinct from old then
    raise exception using
      errcode = '23514',
      constraint = 'assessment_runs_completed_formal_immutable',
      message = 'completed formal assessment run is immutable except for superseding its current marker';
  end if;
  return new;
end
$$;

drop trigger if exists trg_assessment_runs_context on assessment_runs;
create trigger trg_assessment_runs_context
before insert or update of
  inspection_year_id, run_kind, formal_revision_number, supersedes_run_id,
  technical_condition_package_id, standard_profile_id,
  component_inventory_revision_id, rule_package_checksum
on assessment_runs
for each row execute function validate_assessment_run_context();

drop trigger if exists trg_assessment_runs_completed_formal_immutable
  on assessment_runs;
create trigger trg_assessment_runs_completed_formal_immutable
before update or delete on assessment_runs
for each row execute function protect_completed_formal_assessment_run();

create table if not exists assessment_component_results (
  id uuid primary key default gen_random_uuid(),
  assessment_run_id uuid not null
    references assessment_runs(id) on delete cascade,
  bridge_component_id uuid not null
    references bridge_components(id) on delete restrict,
  standard_component_category_id text not null
    check (length(btrim(standard_component_category_id)) > 0),
  structure_part text not null
    check (structure_part in ('superstructure', 'substructure', 'deck_system', 'overall', 'other')),
  score numeric(12, 6) not null,
  grade text,
  deduction numeric(12, 6),
  result_json jsonb not null default '{}'::jsonb,
  created_at timestamptz not null default now(),
  constraint assessment_component_results_score_check check (
    score between 0 and 100 and score <> 'NaN'::numeric
  ),
  constraint assessment_component_results_deduction_check check (
    deduction is null
    or (deduction between 0 and 100 and deduction <> 'NaN'::numeric)
  ),
  constraint assessment_component_results_json_check check (
    jsonb_typeof(result_json) = 'object'
  ),
  unique (assessment_run_id, bridge_component_id)
);

create index if not exists ix_assessment_component_results_component
  on assessment_component_results (bridge_component_id, assessment_run_id);

create table if not exists assessment_part_results (
  id uuid primary key default gen_random_uuid(),
  assessment_run_id uuid not null
    references assessment_runs(id) on delete cascade,
  result_level text not null check (result_level in ('部件', '结构', '全桥')),
  result_key text not null check (length(btrim(result_key)) > 0),
  parent_result_key text,
  structure_part text not null
    check (structure_part in ('superstructure', 'substructure', 'deck_system', 'overall', 'other')),
  standard_component_category_id text,
  score numeric(12, 6) not null,
  grade text,
  weight numeric(12, 8),
  result_json jsonb not null default '{}'::jsonb,
  created_at timestamptz not null default now(),
  constraint assessment_part_results_score_check check (
    score between 0 and 100 and score <> 'NaN'::numeric
  ),
  constraint assessment_part_results_weight_check check (
    weight is null
    or (weight between 0 and 1 and weight <> 'NaN'::numeric)
  ),
  constraint assessment_part_results_json_check check (
    jsonb_typeof(result_json) = 'object'
  ),
  unique (assessment_run_id, result_level, result_key)
);

create index if not exists ix_assessment_part_results_run_level
  on assessment_part_results (assessment_run_id, result_level);

create table if not exists assessment_control_results (
  id uuid primary key default gen_random_uuid(),
  assessment_run_id uuid not null
    references assessment_runs(id) on delete cascade,
  rule_id text not null check (length(btrim(rule_id)) > 0),
  control_level text not null
    check (control_level in ('构件', '部件', '结构', '全桥')),
  target_key text not null check (length(btrim(target_key)) > 0),
  triggered boolean not null,
  score_before numeric(12, 6),
  score_after numeric(12, 6),
  grade_before text,
  grade_after text,
  message text not null check (length(btrim(message)) > 0),
  input_json jsonb not null default '{}'::jsonb,
  output_json jsonb not null default '{}'::jsonb,
  created_at timestamptz not null default now(),
  constraint assessment_control_results_score_before_check check (
    score_before is null
    or (score_before between 0 and 100 and score_before <> 'NaN'::numeric)
  ),
  constraint assessment_control_results_score_after_check check (
    score_after is null
    or (score_after between 0 and 100 and score_after <> 'NaN'::numeric)
  ),
  constraint assessment_control_results_json_check check (
    jsonb_typeof(input_json) = 'object'
    and jsonb_typeof(output_json) = 'object'
  ),
  unique (assessment_run_id, rule_id, control_level, target_key)
);

create index if not exists ix_assessment_control_results_run_triggered
  on assessment_control_results (assessment_run_id, triggered);

create table if not exists assessment_rule_traces (
  id uuid primary key default gen_random_uuid(),
  assessment_run_id uuid not null
    references assessment_runs(id) on delete cascade,
  sequence_number integer not null check (sequence_number > 0),
  rule_id text not null check (length(btrim(rule_id)) > 0),
  trace_stage text not null check (length(btrim(trace_stage)) > 0),
  target_type text not null
    check (target_type in ('病害', '构件', '部件', '结构', '全桥', '等级', '控制')),
  target_key text not null check (length(btrim(target_key)) > 0),
  input_json jsonb not null,
  output_json jsonb not null,
  created_at timestamptz not null default now(),
  constraint assessment_rule_traces_json_check check (
    jsonb_typeof(input_json) = 'object'
    and jsonb_typeof(output_json) = 'object'
  ),
  unique (assessment_run_id, sequence_number)
);

create index if not exists ix_assessment_rule_traces_run_rule
  on assessment_rule_traces (assessment_run_id, rule_id, sequence_number);

create or replace function validate_assessment_component_result_context()
returns trigger
language plpgsql
as $$
declare
  v_inventory_revision_id uuid;
  v_package_id uuid;
begin
  select component_inventory_revision_id, technical_condition_package_id
    into v_inventory_revision_id, v_package_id
  from assessment_runs where id = new.assessment_run_id;

  if v_inventory_revision_id is null or not exists (
    select 1
    from bridge_component_inventory_entries e
    join bridge_component_standard_mappings m
      on m.inventory_entry_id = e.id
    where e.inventory_revision_id = v_inventory_revision_id
      and e.bridge_component_id = new.bridge_component_id
      and e.is_active
      and m.is_active
      and m.confirmation_status = '已确认'
      and m.standard_package_id = v_package_id
      and m.standard_component_category_id = new.standard_component_category_id
      and m.structure_part = new.structure_part
  ) then
    raise exception using
      errcode = '23514',
      constraint = 'assessment_component_results_inventory_context',
      message = 'assessment component result does not match the locked inventory mapping';
  end if;
  return new;
end
$$;

create or replace function protect_completed_formal_assessment_detail()
returns trigger
language plpgsql
as $$
declare
  v_run_kind text;
  v_result_status text;
begin
  if tg_op <> 'INSERT' then
    select run_kind, result_status into v_run_kind, v_result_status
    from assessment_runs where id = old.assessment_run_id;
    if v_run_kind = '正式' and v_result_status = '成功' then
      raise exception using
        errcode = '23514',
        constraint = 'assessment_completed_formal_detail_immutable',
        message = 'details of a completed formal assessment run are immutable';
    end if;
  end if;
  if tg_op <> 'DELETE' then
    select run_kind, result_status into v_run_kind, v_result_status
    from assessment_runs where id = new.assessment_run_id;
    if v_run_kind = '正式' and v_result_status = '成功' then
      raise exception using
        errcode = '23514',
        constraint = 'assessment_completed_formal_detail_immutable',
        message = 'details of a completed formal assessment run are immutable';
    end if;
  end if;
  return case when tg_op = 'DELETE' then old else new end;
end
$$;

drop trigger if exists trg_assessment_component_results_context
  on assessment_component_results;
create trigger trg_assessment_component_results_context
before insert or update of
  assessment_run_id, bridge_component_id,
  standard_component_category_id, structure_part
on assessment_component_results
for each row execute function validate_assessment_component_result_context();

drop trigger if exists trg_assessment_component_results_formal_immutable
  on assessment_component_results;
create trigger trg_assessment_component_results_formal_immutable
before insert or update or delete on assessment_component_results
for each row execute function protect_completed_formal_assessment_detail();

drop trigger if exists trg_assessment_part_results_formal_immutable
  on assessment_part_results;
create trigger trg_assessment_part_results_formal_immutable
before insert or update or delete on assessment_part_results
for each row execute function protect_completed_formal_assessment_detail();

drop trigger if exists trg_assessment_control_results_formal_immutable
  on assessment_control_results;
create trigger trg_assessment_control_results_formal_immutable
before insert or update or delete on assessment_control_results
for each row execute function protect_completed_formal_assessment_detail();

drop trigger if exists trg_assessment_rule_traces_formal_immutable
  on assessment_rule_traces;
create trigger trg_assessment_rule_traces_formal_immutable
before insert or update or delete on assessment_rule_traces
for each row execute function protect_completed_formal_assessment_detail();

alter table condition_ratings
  add column if not exists assessment_run_id uuid;

do $$
begin
  if not exists (
    select 1 from pg_constraint
    where conname = 'condition_ratings_assessment_run_id_fkey'
      and conrelid = 'condition_ratings'::regclass
  ) then
    alter table condition_ratings
      add constraint condition_ratings_assessment_run_id_fkey
      foreign key (assessment_run_id)
      references assessment_runs(id)
      on delete restrict;
  end if;
end
$$;

create index if not exists ix_condition_ratings_assessment_run
  on condition_ratings (assessment_run_id)
  where assessment_run_id is not null;

create or replace function validate_condition_rating_assessment_run()
returns trigger
language plpgsql
as $$
declare
  v_run_year_id uuid;
  v_run_kind text;
  v_run_status text;
begin
  if new.assessment_run_id is null then
    return new;
  end if;
  select inspection_year_id, run_kind, result_status
    into v_run_year_id, v_run_kind, v_run_status
  from assessment_runs where id = new.assessment_run_id;
  if v_run_year_id is distinct from new.inspection_year_id
     or v_run_kind is distinct from '正式'
     or v_run_status is distinct from '成功'
     or new.review_status is distinct from '已确认' then
    raise exception using
      errcode = '23514',
      constraint = 'condition_ratings_assessment_run_context',
      message = 'condition rating projection requires a successful formal run of the same year';
  end if;
  return new;
end
$$;

drop trigger if exists trg_condition_ratings_assessment_run
  on condition_ratings;
create trigger trg_condition_ratings_assessment_run
before insert or update of
  inspection_year_id, assessment_run_id, review_status
on condition_ratings
for each row execute function validate_condition_rating_assessment_run();

comment on table assessment_runs is
  '系统自主评定的试算或正式运行；锁定输入摘要、规范包、项目规范组合和构件台账版本。';
comment on table assessment_component_results is
  '系统评定运行产生的实际物理构件级结果。';
comment on table assessment_part_results is
  '系统评定运行产生的规范部件、结构分部和全桥结果。';
comment on table assessment_control_results is
  '系统评定运行中单项控制规则的触发和调整结果。';
comment on table assessment_rule_traces is
  '系统评定运行按顺序保存的结构化规则输入和输出轨迹。';
comment on column condition_ratings.assessment_run_id is
  '兼容既有档案查询的正式系统评定结果投影来源；旧评分保持为空。';
