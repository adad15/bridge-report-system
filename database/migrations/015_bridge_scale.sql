-- 015：桥梁规模（大桥/中桥/小桥），由用户手工录入，非从尺寸推导。
-- 可空；幂等（迁移会被跑两遍）。

alter table bridges
  add column if not exists bridge_scale text
    check (bridge_scale is null or bridge_scale in ('大桥', '中桥', '小桥'));

comment on column bridges.bridge_scale is '桥梁规模：大桥/中桥/小桥，用户手工录入';
