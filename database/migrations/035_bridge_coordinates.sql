-- 035: 桥梁经纬度的基准、含义与取值范围。
--
-- `bridges.longitude` / `latitude` 从 002 起就在表里，但一直没人写也没人读，也没有说明
-- 存的是哪个坐标基准。地图卡片和报告 §1.1 的「图 1-1 地理位置图」都要用它们，含义必须
-- 先定死，否则两边各按各的理解转换。
--
-- **存的是 WGS-84。** 现场 GPS、设计图和大部分测绘资料给的都是这个基准；高德、百度的
-- 底图用的是 GCJ-02，两者在东北地区相差一两百米，正好够把图钉甩到桥外面去。转换只在
-- 前端画图前做一次，库里和接口上一律是 WGS-84，避免同一个数在不同地方含义不同。
--
-- 可空：档案是慢慢补起来的，没录坐标就不出地图，也不编一个（设计 §14 第 5 条）。

comment on column bridges.longitude is
  '桥位经度，WGS-84，单位度。给桥梁总览的地图卡片和报告图 1-1 用；到 GCJ-02 的转换在前端做。';
comment on column bridges.latitude is
  '桥位纬度，WGS-84，单位度。';

-- 经纬度填反是最常见的录入错误，反了之后纬度会超出 ±90，这条约束当场挡住。
alter table bridges
  drop constraint if exists bridges_coordinates_in_range;

alter table bridges
  add constraint bridges_coordinates_in_range check (
    (longitude is null or (longitude >= -180 and longitude <= 180))
    and (latitude is null or (latitude >= -90 and latitude <= 90))
  );
