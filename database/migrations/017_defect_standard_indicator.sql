begin;

alter table defect_observations
    add column if not exists standard_defect_indicator_id text;

create index if not exists idx_defect_observations_year_component_indicator
    on defect_observations (
        inspection_year_id,
        bridge_component_id,
        standard_defect_indicator_id
    );

commit;
