-- 034: 桥梁档案补齐第 1.1 节叙述所需的字段。
--
-- 报告 §1.1「桥梁概况」在正式报告里是三段叙述文字，不是一张两列表：
--
--   百股大桥位于大养线公路（S320）锦州段 K109+747 处，建成于 2002 年。跨径布置为
--   33*20.0m，桥梁全长为 664.6m，斜交角为 90°，属大桥。桥面净宽为 20m，左、右侧各
--   设置 2.5m 的人行道。桥面铺装采用沥青混凝土，2、6、10… 号墩顶设型钢伸缩缝，支座
--   为板式橡胶支座。上部结构为预应力砼简支空心板，每孔 25 片，梁高 0.85m；下部结构
--   为钢筋砼肋板台、桩基础，钢筋砼四柱式墩、桩基础。设计荷载为汽车-超 20 级、挂车-120。
--
-- 这段话里的每一个数都得有出处。下面这些列就是那些出处，`bridges` 里原来没有。
--
-- 同一批字段还能把附录2「桥梁基本状况卡片」里对应的格子填上（设计 §3 第 7 条把
-- 九十六格的完整填报排除在外，但这十几格顺手就有了）。加一次，两处都用。
--
-- 全部可空：档案是慢慢补起来的，缺哪一项就少写哪一句，不编数据（设计 §14 第 5 条）。

alter table bridges
  -- 平面与宽度
  add column if not exists skew_angle_deg numeric(6, 2),
  add column if not exists carriageway_width_m numeric(8, 2),
  add column if not exists sidewalk_width_m numeric(8, 2),
  -- 桥面系
  add column if not exists deck_pavement text,
  add column if not exists expansion_joint_type text,
  add column if not exists expansion_joint_piers text,
  add column if not exists bearing_type text,
  -- 上部结构
  add column if not exists superstructure_form text,
  add column if not exists girders_per_span integer,
  add column if not exists girder_height_m numeric(8, 2),
  -- 下部结构与基础
  add column if not exists abutment_form text,
  add column if not exists pier_form text,
  add column if not exists foundation_form text,
  -- 设计条件
  add column if not exists design_load text,
  -- 参建与管理单位
  add column if not exists design_org text,
  add column if not exists construction_org text,
  add column if not exists supervision_org text;

comment on column bridges.skew_angle_deg is '斜交角（度）。正交桥为 90。';
comment on column bridges.carriageway_width_m is
  '行车道宽度（m）。§1.1 印作「桥面净宽」，附录2 第 24 格印作「行车道宽」，是同一个量；'
  'bridge_width_m 是含人行道的桥面总宽，两者不要混。';
comment on column bridges.sidewalk_width_m is '单侧人行道宽度（m）。两侧等宽时只存一个数。';
comment on column bridges.deck_pavement is '桥面铺装，如「沥青混凝土」。';
comment on column bridges.expansion_joint_type is '伸缩缝型式，如「型钢伸缩缝」。';
comment on column bridges.expansion_joint_piers is
  '设伸缩缝的墩号，原样存用户录入的写法（如「2、6、10、14、16、19、23、27、31」）。'
  '不拆成数组：报告里原样印出，拆了再拼回去只会丢掉「～」这类写法。';
comment on column bridges.bearing_type is '支座型式，如「板式橡胶支座」。';
comment on column bridges.superstructure_form is '上部结构形式，如「预应力砼简支空心板」。';
comment on column bridges.girders_per_span is '每孔梁片数。';
comment on column bridges.girder_height_m is '梁高（m）。';
comment on column bridges.abutment_form is '桥台形式与基础，如「钢筋砼肋板台、桩基础」。';
comment on column bridges.pier_form is '桥墩形式与基础，如「钢筋砼四柱式墩、桩基础」。';
comment on column bridges.foundation_form is '基础形式，如「桩基础」。桥台桥墩各自写法不同时用这一列兜底。';
comment on column bridges.design_load is '设计荷载，如「汽车-超 20 级、挂车-120」。';
comment on column bridges.design_org is '设计单位。';
comment on column bridges.construction_org is '施工单位。';
comment on column bridges.supervision_org is '监管单位。maintenance_org 是管养单位，两者不同。';

-- 片数和梁高必须是正的；斜交角在 (0, 180] 之间。录错了要当场挡住，不能让报告里
-- 印出「每孔 -25 片」。
alter table bridges
  drop constraint if exists bridges_profile_measures_positive;

alter table bridges
  add constraint bridges_profile_measures_positive check (
    (girders_per_span is null or girders_per_span > 0)
    and (girder_height_m is null or girder_height_m > 0)
    and (carriageway_width_m is null or carriageway_width_m > 0)
    and (sidewalk_width_m is null or sidewalk_width_m >= 0)
    and (skew_angle_deg is null or (skew_angle_deg > 0 and skew_angle_deg <= 180))
  );
