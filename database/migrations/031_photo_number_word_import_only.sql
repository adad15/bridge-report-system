-- 031：照片编号降级为 Word 导入路的内部匹配键。
-- 依赖 002、025；幂等可复跑。
--
-- 背景：系统有两条导入路，对照片编号的依赖完全不同。
--
--   Word 导入：Word 病害表的「照片编号」列写着 2.1-5，图片题注写着「照片2.1-5」，
--     除这个字符串外没有任何线索能把图和病害对上。编号是不可替代的匹配键。
--
--   来源软件导入（source_type='接口同步'）：来源库用 images.ForeignKey 直接指向
--     outerCheckData.id，归属由外键确定。来源库自己的 photoNum 列 887 张全为空，
--     noteContent 与 oriFileName 同样全空——来源软件根本没有照片编号这个概念。
--
-- 早先为了填满契约的必填字段，来源软件那条路按结构部位造过 "2.1-1" 这样的号。
-- 它不是来源事实，而且按病害 UUID 顺序发放：实测同一年度上部结构里，编号 1..10 对应的
-- 构件台账顺序是 1319、1086、1086、1174、308、407、1296、1375、1111……完全跳跃。
-- 拿它当报告图号会做出一份表里编号乱跳、读者无法按号找图的报告。
--
-- 因此：
--   1. 去掉非空约束，来源软件导入的照片不再有编号；
--   2. 清掉已经造出来的那批号，避免日后有人当成业务编号使用。
-- 报告里的图号在生成时按模板结构和病害表行序重排，与本列无关。

alter table defect_photos alter column photo_number drop not null;

comment on column defect_photos.photo_number is
  'Word 导入路的照片匹配键：病害表「照片编号」列与图片题注靠它对上。'
  '来源软件导入靠外键绑定，此列为空。不是面向用户的业务编号，也不是报告里的图号。';

-- 只清来源软件导入的。Word 导入的编号必须留着——重新打开导入记录时还要靠它匹配。
-- source_import_record_id 为空的行（来源导入记录已被合规删除）无法判断来路，保持原样。
update defect_photos p
set photo_number = null
from import_records ir
where ir.id = p.source_import_record_id
  and ir.source_type = '接口同步'
  and p.photo_number is not null;
