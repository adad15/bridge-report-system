begin;

do $$
declare
  v_nullable text;
begin
  if exists (
    select 1 from information_schema.columns
    where table_schema = current_schema()
      and table_name = 'condition_ratings'
      and column_name in (
        'source_score', 'calculated_score', 'score_validation_status',
        'score_resolution_reason', 'calculation_details_json')
  ) then
    raise exception '014 smoke: legacy condition_ratings columns still exist';
  end if;

  if exists (
    select 1 from information_schema.columns
    where table_schema = current_schema()
      and table_name = 'defect_observations'
      and column_name in ('defect_deduction', 'component_score')
  ) then
    raise exception '014 smoke: legacy defect_observations columns still exist';
  end if;

  select is_nullable into v_nullable
  from information_schema.columns
  where table_schema = current_schema()
    and table_name = 'condition_ratings'
    and column_name = 'assessment_run_id';

  if v_nullable is distinct from 'NO' then
    raise exception '014 smoke: condition_ratings.assessment_run_id must be NOT NULL';
  end if;
end
$$;

rollback;
