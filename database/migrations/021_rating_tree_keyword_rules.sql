-- 021：受控关键词匹配规则随不可变评定树版本发布。
-- 依赖 018；幂等可复跑，不回填历史数据。
--
-- 规则只能由规范包同步写入，页面运行时不可编辑：与节点、别名一样挂在
-- rating_tree_versions 下，并复用 018 的"已发布内容不可变"触发器。

create table if not exists rating_tree_keyword_rules (
  id uuid primary key default gen_random_uuid(),
  rating_tree_version_id uuid not null
    references rating_tree_versions(id) on delete cascade,
  target_node_id uuid not null
    references rating_tree_nodes(id) on delete cascade,
  rule_key text not null check (length(btrim(rule_key)) > 0),
  bridge_type_id text not null check (length(btrim(bridge_type_id)) > 0),
  component_category_id text not null
    check (length(btrim(component_category_id)) > 0),
  positive_keywords text[] not null
    check (cardinality(positive_keywords) > 0),
  excluded_keywords text[] not null default '{}'::text[],
  auto_bind boolean not null default false,
  sort_order integer not null default 0,
  rule_note text not null default '',
  created_at timestamptz not null default now(),
  constraint rating_tree_keyword_rules_key_unique
    unique (rating_tree_version_id, rule_key)
);

create index if not exists ix_rating_tree_keyword_rules_scope
  on rating_tree_keyword_rules (
    rating_tree_version_id, bridge_type_id, component_category_id, sort_order, rule_key
  );
create index if not exists ix_rating_tree_keyword_rules_target
  on rating_tree_keyword_rules (target_node_id);

create or replace function validate_rating_tree_keyword_rule_target()
returns trigger
language plpgsql
as $$
declare
  v_target_version_id uuid;
  v_target_selectable boolean;
  v_target_bridges text[];
  v_target_components text[];
begin
  select rating_tree_version_id, is_selectable, bridge_type_ids, component_category_ids
    into v_target_version_id, v_target_selectable, v_target_bridges, v_target_components
  from rating_tree_nodes where id = new.target_node_id;
  if v_target_version_id is distinct from new.rating_tree_version_id
     or not coalesce(v_target_selectable, false) then
    raise exception using
      errcode = '23514',
      constraint = 'rating_tree_keyword_rules_target_check',
      message = 'rating tree keyword rule must target a selectable node in the same version';
  end if;
  if not (new.bridge_type_id = any(v_target_bridges))
     or not (new.component_category_id = any(v_target_components)) then
    raise exception using
      errcode = '23514',
      constraint = 'rating_tree_keyword_rules_scope_check',
      message = 'rating tree keyword rule scope must stay inside its target node scope';
  end if;
  return new;
end
$$;

-- 018 的 protect_published_rating_tree_child 按表名判断版本来源，这里补上新表分支。
create or replace function protect_published_rating_tree_child()
returns trigger
language plpgsql
as $$
declare
  v_version_id uuid;
  v_status text;
begin
  if tg_table_name in (
    'rating_tree_nodes', 'rating_tree_aliases', 'rating_tree_keyword_rules'
  ) then
    v_version_id := case when tg_op = 'DELETE'
      then old.rating_tree_version_id else new.rating_tree_version_id end;
  else
    select rating_tree_version_id into v_version_id
    from rating_tree_nodes
    where id = case when tg_op = 'DELETE'
      then old.rating_tree_node_id else new.rating_tree_node_id end;
  end if;

  select status into v_status from rating_tree_versions where id = v_version_id;
  if v_status = 'published' then
    raise exception using
      errcode = '23514',
      constraint = 'rating_tree_published_children_immutable',
      message = 'published rating tree contents are immutable';
  end if;
  return case when tg_op = 'DELETE' then old else new end;
end
$$;

drop trigger if exists trg_rating_tree_keyword_rules_target
  on rating_tree_keyword_rules;
create trigger trg_rating_tree_keyword_rules_target
before insert or update of rating_tree_version_id, target_node_id,
  bridge_type_id, component_category_id
on rating_tree_keyword_rules
for each row execute function validate_rating_tree_keyword_rule_target();

drop trigger if exists trg_rating_tree_keyword_rules_published_immutable
  on rating_tree_keyword_rules;
create trigger trg_rating_tree_keyword_rules_published_immutable
before insert or update or delete on rating_tree_keyword_rules
for each row execute function protect_published_rating_tree_child();

comment on table rating_tree_keyword_rules is
  '随不可变评定树版本发布的受控关键词匹配规则；auto_bind 为真且结果唯一时才允许自动绑定。';
