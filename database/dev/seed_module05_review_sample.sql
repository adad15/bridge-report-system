-- 模块 05 评审工作台样例种子数据。
--
-- 用途：为 GET /api/import-records/{id}/review 等评审接口以及后续前端联调
-- 提供一条可重复执行（幂等）的样例数据：样例桥梁 + 2026 年度 + 一条待校对导入记录。
--
-- 幂等策略：先按“样例桥梁”专属的 bridge_name 定位并删除历史遗留数据（叶子表 -> 根表，
-- 顺序严格遵循 database/migrations/002_core_schema_and_archive.sql 中各表的外键
-- on delete 约束：凡是对样例桥梁子孙表带 restrict 的，都必须先删；带 cascade 的
-- 顺序其实无关紧要，这里仍显式列出以便审阅和防御“之前跑过一次确认流程、
-- 样例桥梁下已经沉淀出构件/病害/评分”这种更脏的历史状态），再重新插入样例桥梁 /
-- 年度 / 导入记录。
--
-- parsed_result_json 这里先写入占位值 '{}'::jsonb；真正的样例合同 JSON 由配套的
-- scripts/dev/seed-module05-review-sample.ps1 在本脚本执行完成后用第二条 UPDATE
-- 语句写入（原因见该脚本内注释：psql 没有真正的绑定参数，直接在这里用 \set 拼接
-- 含中文和引号的大段 JSON 并不可靠）。

\set ON_ERROR_STOP on

begin;

-- 1) 清理同名样例的历史遗留数据（叶子表 -> 根表，避免触发 restrict 外键报错）。

delete from defect_photos
where defect_observation_id in (
  select id from defect_observations
  where bridge_id in (select id from bridges where bridge_name = '绕阳河二号桥（模块05样例）')
);

delete from defect_measurements
where defect_observation_id in (
  select id from defect_observations
  where bridge_id in (select id from bridges where bridge_name = '绕阳河二号桥（模块05样例）')
);

delete from defect_comparisons
where bridge_id in (select id from bridges where bridge_name = '绕阳河二号桥（模块05样例）');

delete from defect_observations
where bridge_id in (select id from bridges where bridge_name = '绕阳河二号桥（模块05样例）');

delete from condition_ratings
where inspection_year_id in (
  select id from inspection_years
  where bridge_id in (select id from bridges where bridge_name = '绕阳河二号桥（模块05样例）')
);

delete from defect_threads
where bridge_id in (select id from bridges where bridge_name = '绕阳河二号桥（模块05样例）');

delete from import_record_files
where import_record_id in (
  select id from import_records
  where bridge_id in (select id from bridges where bridge_name = '绕阳河二号桥（模块05样例）')
);

delete from import_records
where bridge_id in (select id from bridges where bridge_name = '绕阳河二号桥（模块05样例）');

delete from component_aliases
where bridge_component_id in (
  select id from bridge_components
  where bridge_id in (select id from bridges where bridge_name = '绕阳河二号桥（模块05样例）')
);

delete from bridge_components
where bridge_id in (select id from bridges where bridge_name = '绕阳河二号桥（模块05样例）');

delete from bridge_aliases
where bridge_id in (select id from bridges where bridge_name = '绕阳河二号桥（模块05样例）');

delete from inspection_years
where bridge_id in (select id from bridges where bridge_name = '绕阳河二号桥（模块05样例）');

delete from bridges
where bridge_name = '绕阳河二号桥（模块05样例）';

-- 2) 重新插入样例桥梁 / 2026 年度 / 待校对导入记录。
--    system_number / id 全部使用数据库默认生成，不在这里手工指定。

insert into bridges (bridge_name, route_name, status)
values ('绕阳河二号桥（模块05样例）', '样例线路', '在用');

insert into inspection_years (bridge_id, inspection_year, status, is_current)
select id, 2026, '待校对', true
from bridges
where bridge_name = '绕阳河二号桥（模块05样例）';

insert into import_records (
  bridge_id,
  inspection_year_id,
  import_name,
  source_type,
  import_status,
  importer_name,
  parsed_result_json
)
select
  b.id,
  iy.id,
  '模块05评审工作台样例导入',
  '软件导出Word',
  '待校对',
  'word_importer',
  '{}'::jsonb
from bridges b
join inspection_years iy
  on iy.bridge_id = b.id
  and iy.inspection_year = 2026
  and iy.is_current
where b.bridge_name = '绕阳河二号桥（模块05样例）';

commit;
