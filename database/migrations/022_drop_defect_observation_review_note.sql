-- 022：移除病害观测的校对备注列。
-- 依赖 002；幂等可复跑。
--
-- 校对备注在页面上只有一个空输入框，没有任何读取方：报告不印它，档案页不显示，
-- 也没有查询按它筛选。字段留着只会让每次入库都多写一个恒为 null 的列。
-- defect_observations 当前无正式事实行，删列不丢历史数据。

alter table defect_observations drop column if exists review_note;
