-- 尺寸结构化值扩展：单值与区间互斥保存，同时保留约数语义。

alter table defect_measurements
  add column if not exists value_type text,
  add column if not exists minimum_value numeric(12, 4),
  add column if not exists maximum_value numeric(12, 4),
  add column if not exists is_approximate boolean not null default false;

-- 既有正式数据只有 numeric_value；有数值的行可无损归类为单值。
-- “未识别尺寸”等只有原文的旧行保持 value_type=null，不能伪造结构化结果。
update defect_measurements
set value_type = 'single'
where value_type is null and numeric_value is not null;

alter table defect_measurements
  drop constraint if exists defect_measurements_value_shape_check;

alter table defect_measurements
  add constraint defect_measurements_value_shape_check check (
    (value_type is null and numeric_value is null and minimum_value is null and maximum_value is null)
    or
    (value_type = 'single' and numeric_value is not null and minimum_value is null and maximum_value is null)
    or
    (value_type = 'range' and numeric_value is null and minimum_value is not null and maximum_value is not null
      and minimum_value <= maximum_value)
  );

comment on column defect_measurements.value_type is '结构化值类型：single 或 range；未识别原文可为空';
comment on column defect_measurements.minimum_value is '区间最小值，仅 value_type=range 时有值';
comment on column defect_measurements.maximum_value is '区间最大值，仅 value_type=range 时有值';
comment on column defect_measurements.is_approximate is '原文是否表达为约数';
