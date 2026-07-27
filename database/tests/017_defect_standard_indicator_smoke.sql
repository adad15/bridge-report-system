begin;

do $$
begin
    if not exists (
        select 1
        from information_schema.columns
        where table_schema = 'public'
          and table_name = 'defect_observations'
          and column_name = 'standard_defect_indicator_id'
          and data_type = 'text'
    ) then
        raise exception 'standard_defect_indicator_id column is missing';
    end if;

    if not exists (
        select 1
        from pg_indexes
        where schemaname = 'public'
          and tablename = 'defect_observations'
          and indexname = 'idx_defect_observations_year_component_indicator'
    ) then
        raise exception 'defect indicator index is missing';
    end if;
end
$$;

rollback;
