-- 每桥至多一条构件台账草稿。
--
-- 这个不变量一直被代码假设、却没人保证：generate_draft() 见到任何草稿就返回
-- Conflict，而派生草稿的路径按 baseline_revision_id 查找现有草稿——桥上已有基于
-- R2 的草稿时，客户端拿较旧的已确认版本 R1 写入，按 baseline 找不到匹配，于是
-- 新建第二条以 R1 为 baseline 的草稿。get_latest_revision() 的
-- "order by (status='草稿') desc, revision_number desc limit 1" 只挑得中一条，
-- 另一条从此隐形。
--
-- 索引建立前先自检：若目标库已存在一桥多草稿的历史数据，这里会主动报错，
-- 而不是让 create index 抛一个看不出所以然的唯一冲突。
do $$
declare
  offending integer;
begin
  select count(*) into offending from (
    select bridge_id from bridge_component_inventory_revisions
    where status = '草稿' group by bridge_id having count(*) > 1
  ) t;
  if offending > 0 then
    raise exception '存在 % 座桥有多于一条草稿台账，需先人工合并后再执行本迁移', offending;
  end if;
end $$;

create unique index if not exists ux_component_inventory_single_draft
  on bridge_component_inventory_revisions (bridge_id)
  where status = '草稿';
