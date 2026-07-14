-- 003：合同 1.2 构件评分双值校验列 + 构件病害档案支撑（模块 06）
--
-- 依赖 002_core_schema_and_archive.sql；幂等可复跑（psql -v ON_ERROR_STOP=1）。
--
-- 1.1 历史数据兼容策略：
--   * 已确认 1.1 年度的 condition_ratings 行保持可读，新增 5 列保持 NULL/'{}'；
--     读取侧按 score_validation_status is null 显示“历史数据缺少评分校验明细”，
--     不做任何猜测性回填 UPDATE。
--   * 尚未确认的 1.0/1.1 草稿由 C++ 标记 legacy_pending_reparse，经 parse-word
--     重新解析为 1.2；已确认年度补齐走同桥同年显式修订版重新导入。
--   * 本文件末尾对 defect_observations.scale 的 UPDATE 不是回填：仅清除历史上
--     severity（info/warning/error 提示级别）被误写入规范标度列的已知错误值。

-- 构件评分三值校验：来源分（Word）、复算分（JTG/T H21-2011 4.1.1，未舍入）、
-- 校验状态、人工处理原因与计算明细（降序扣分序列、参与病害、公式标准）。
alter table condition_ratings
  add column if not exists source_score numeric(8, 2),
  add column if not exists calculated_score numeric(18, 10),
  add column if not exists score_validation_status text,
  add column if not exists score_resolution_reason text,
  add column if not exists calculation_details_json jsonb not null default '{}'::jsonb;

do $$
begin
  if not exists (
    select 1 from pg_constraint
    where conname = 'condition_ratings_score_validation_status_check'
      and conrelid = 'condition_ratings'::regclass
  ) then
    alter table condition_ratings
      add constraint condition_ratings_score_validation_status_check
      check (
        score_validation_status is null
        or score_validation_status in ('一致', '不一致', '无法复算', '人工接受Word值', '人工采用复算值')
      );
  end if;
end
$$;

-- 构件级评分必须绑定具体构件；其余层级（全桥/结构分部/部件/项目）不受影响。
do $$
begin
  if not exists (
    select 1 from pg_constraint
    where conname = 'condition_ratings_component_link_check'
      and conrelid = 'condition_ratings'::regclass
  ) then
    alter table condition_ratings
      add constraint condition_ratings_component_link_check
      check (rating_level <> '构件' or bridge_component_id is not null);
  end if;
end
$$;

-- 同一检测版本（inspection_years 行）内，同一构件只允许一条构件级评分。
create unique index if not exists ux_condition_ratings_component_per_inspection
  on condition_ratings (inspection_year_id, bridge_component_id)
  where rating_level = '构件';

-- 构件病害档案按线索分组读取观测，为绑定查询建部分索引。
create index if not exists ix_defect_observations_thread
  on defect_observations (defect_thread_id)
  where defect_thread_id is not null;

-- 历史纠错：模块 05 早期版本曾把校对提示级别 severity 写入规范标度列 scale。
-- 仅清除这三个已知误写值；正常标度是十进制数字文本，不受影响。
update defect_observations set scale = null where scale in ('info', 'warning', 'error');
