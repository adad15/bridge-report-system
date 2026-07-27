-- 待校对年度若尚未锁定台账版本，但所属桥梁已经有已确认台账，
-- 则补齐为该桥最新的已确认版本。已确认/已归档年度以及已有锁定版本均不覆盖。
with latest_confirmed_revision as (
    select distinct on (bridge_id)
        bridge_id,
        id as revision_id
    from bridge_component_inventory_revisions
    where status in ('已确认', 'confirmed')
    order by bridge_id, revision_number desc
)
update inspection_years iy
set component_inventory_revision_id = latest.revision_id,
    updated_at = now()
from latest_confirmed_revision latest
where iy.bridge_id = latest.bridge_id
  and iy.status = '待校对'
  and iy.component_inventory_revision_id is null
  and exists (
      select 1
      from import_records ir
      where ir.inspection_year_id = iy.id
        and ir.import_status in ('解析中', '待校对')
  );
