-- 003 冒烟测试：迁移 003 留下的构件评分唯一约束与病害线索字段仍存在。
-- 评分值现由 013/014 的系统评定运行负责，本测试不再构造 Word 来源评分。

begin;

do $$
begin
  if to_regclass('ux_condition_ratings_component_per_inspection') is null then
    raise exception '003 smoke: component rating uniqueness index is missing';
  end if;

  if not exists (
    select 1 from information_schema.columns
    where table_schema = current_schema()
      and table_name = 'defect_observations'
      and column_name = 'defect_thread_id'
  ) then
    raise exception '003 smoke: defect_observations.defect_thread_id is missing';
  end if;

  if to_regclass('ix_defect_observations_thread') is null then
    raise exception '003 smoke: defect observation thread index is missing';
  end if;

  raise notice '003 smoke passed';
end
$$;

rollback;
