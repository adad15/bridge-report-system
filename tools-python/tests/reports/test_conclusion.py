"""结论与历史对比的措辞规则（设计 §12.2、§14）。

设计要求"规则输入、排序和措辞模板必须可测试"，并且第一版的对比能力边界
"必须写入实现测试，不得只作为容易遗失的代码注释"——这个文件就是那份实现测试。
"""

from __future__ import annotations

import pytest

from bridge_report_tools.reports.conclusion import (
    ASSESSMENT_RESULT_INTRO,
    COMPARISON_CAPABILITY,
    COMPARISON_CAVEAT,
    COMPONENT_WEIGHTS_INTRO,
    MAINTENANCE_ADVICE,
    NO_COMPARISON_TEXT,
    NO_CONTROL_INDICATOR_TEXT,
    STANDARD_CITATION,
    bridge_profile_paragraphs,
    comparison_paragraphs,
    conclusion_paragraphs,
    defect_summary_lines,
    format_score,
)
from bridge_report_tools.reports.report_context import ReportStructurePart

from tests.reports.context_fixtures import (
    NO_ASSESSMENT,
    assessment,
    comparison,
    context,
    defect_row,
    part,
)


def structure_part(current: int, previous: int) -> ReportStructurePart:
    return ReportStructurePart(
        **part(
            "SUPERSTRUCTURE",
            "上部结构",
            rows=[defect_row(1)],
            photos=[],
            comparison=comparison(current, previous),
        )
    )


# --------------------------------------------------------------------------
# §1.1 桥梁概况（设计 §8 第 1 章）
# --------------------------------------------------------------------------


#: 档案录满的一座桥，用来看每个分句都摆在什么位置。
FULL_PROFILE = {
    "station_mark": "K12+345",
    "bridge_scale": "中桥",
    "span_combination": "5×13m",
    "bridge_length_m": 68.5,
    "built_year": 1998,
    "skew_angle_deg": 90,
    "carriageway_width_m": 11.0,
    "sidewalk_width_m": 0.5,
    "deck_pavement": "水泥混凝土",
    "expansion_joint_type": "橡胶伸缩缝",
    "expansion_joint_piers": "1、4",
    "bearing_type": "板式橡胶支座",
    "superstructure_form": "预应力混凝土简支空心板",
    "girders_per_span": 9,
    "girder_height_m": 0.7,
    "abutment_form": "桩柱式桥台",
    "pier_form": "柱式墩",
    "foundation_form": "钻孔灌注桩基础",
    "design_load": "公路-Ⅰ级",
    "design_org": "某某设计院",
    "construction_org": "某某工程局",
    "maintenance_org": "太和公路段",
    "supervision_org": "某某公路管理处",
}


def test_bridge_profile_reads_as_the_reference_report_does() -> None:
    """档案录满时，两段话按位置、规模、宽度、桥面、结构、荷载、单位的次序排下来。"""
    location, orgs = bridge_profile_paragraphs(context(bridge_profile=FULL_PROFILE))

    assert location == (
        "百股大桥位于大养线公路（S320）太和区段 K12+345 处，建成于 1998 年。"
        "跨径布置为 5×13m，桥梁全长为 68.5m，斜交角为 90°，属中桥。"
        "桥面净宽为 11m，左、右侧各设置 0.5m 的人行道。"
        "桥面铺装采用水泥混凝土，1、4 号墩顶设橡胶伸缩缝，支座为板式橡胶支座。"
        "上部结构为预应力混凝土简支空心板，每孔 9 片，梁高 0.7m；"
        "下部结构为桩柱式桥台，柱式墩，钻孔灌注桩基础。"
        "设计荷载为公路-Ⅰ级。"
    )
    assert orgs == (
        "该桥设计单位为某某设计院，施工单位为某某工程局，"
        "管养单位为太和公路段，监管单位为某某公路管理处。"
    )


def test_bridge_profile_shortens_instead_of_inventing() -> None:
    """档案只录了两项，就只写这两项——不写「未知」，也不留半截话。"""
    paragraphs = bridge_profile_paragraphs(
        context(bridge_profile={"built_year": 1998, "design_org": "某某设计院"})
    )

    assert paragraphs == [
        "百股大桥位于大养线公路（S320）太和区段，建成于 1998 年。",
        "该桥设计单位为某某设计院。",
    ]
    assert not any("未知" in text or "，。" in text for text in paragraphs)


def test_bridge_profile_without_archive_data_writes_nothing() -> None:
    """一项都没有就整节不出。宁可空着等档案补，也不摆一段空话。"""
    assert bridge_profile_paragraphs(
        context(bridge_profile={}, scalars={"bridge_name": "百股大桥"})
    ) == []


def test_bridge_profile_measures_drop_meaningless_zeros() -> None:
    """664.60 印成 664.6，90.00 印成 90——数据库的 numeric 尾零不该进正文。"""
    (location,) = bridge_profile_paragraphs(
        context(
            bridge_profile={"bridge_length_m": 664.60, "skew_angle_deg": 90.00},
        )
    )

    assert "桥梁全长为 664.6m" in location
    assert "斜交角为 90°" in location


# --------------------------------------------------------------------------
# 历史对比（设计 §12.2）
# --------------------------------------------------------------------------


def test_first_version_capability_is_count_delta_only() -> None:
    """第一版只比来源病害条数。

    换成 confirmed_defect_comparison_v2 时这条会炸——那正是要人回来把措辞、
    免责说明和这份测试一起改掉的提醒（设计 §12.3）。
    """
    assert COMPARISON_CAPABILITY == "source_defect_count_delta_v1"


@pytest.mark.parametrize(
    ("current", "previous", "expected"),
    [
        (23, 18, "增加 5 条"),
        (18, 23, "减少 5 条"),
        (18, 18, "与所选检查记录持平"),
    ],
)
def test_delta_wording_follows_the_sign(current: int, previous: int, expected: str) -> None:
    text = comparison_paragraphs(structure_part(current, previous), "2025")[0]

    assert expected in text
    assert f"共记录病害 {previous} 条" in text
    assert f"本次检查共记录 {current} 条" in text


def test_caveat_always_accompanies_a_comparison() -> None:
    """条数变化不能证明病害身份关系，免责说明不能被单独丢掉（设计 §12.2）。"""
    paragraphs = comparison_paragraphs(structure_part(23, 18), "2025")

    assert paragraphs[-1] == COMPARISON_CAVEAT


def test_missing_history_gets_a_fixed_sentence_not_silence() -> None:
    """模板里留着的小节标题下面必须有话，否则读者以为内容漏掉了。"""
    without = ReportStructurePart(
        **part("SUPERSTRUCTURE", "上部结构", rows=[], photos=[])
    )

    assert comparison_paragraphs(without, None) == [NO_COMPARISON_TEXT]


def test_comparison_falls_back_when_the_year_is_unknown() -> None:
    text = comparison_paragraphs(structure_part(23, 18), None)[0]

    assert text.startswith("所选历史检查上部结构")


# --------------------------------------------------------------------------
# 病害检查表前的部件病害概要（设计 §10.4）
# --------------------------------------------------------------------------


def superstructure_with(rows: list[dict]) -> tuple:
    ctx = context(parts=[part("SUPERSTRUCTURE", "上部结构", rows=rows, photos=[])])
    return ctx, ctx.parts[0]


def test_summary_lists_defect_types_per_evaluated_component() -> None:
    ctx, sup = superstructure_with([
        defect_row(1, part_name="上部承重构件"),
        defect_row(2, part_name="上部一般构件"),
    ])
    ctx.parts[0].defect_rows[1].defect_type = "渗水泛碱"

    lines = defect_summary_lines(ctx, sup)

    assert lines[0] == "上部承重构件：裂缝。"
    assert lines[1] == "上部一般构件：渗水泛碱。"


def test_summary_lists_components_without_defects_too() -> None:
    """支座一条病害都没有也要列出来，否则读者不知道支座查没查过。"""
    ctx, sup = superstructure_with([defect_row(1, part_name="上部承重构件")])

    lines = defect_summary_lines(ctx, sup)

    assert "上部一般构件：上部一般构件状况良好，未见明显病害。" in lines


def test_summary_deduplicates_types_in_table_order() -> None:
    """同一类型出现多次只说一次；顺序跟病害表的行序走，重复生成结果相同。"""
    rows = [defect_row(index, part_name="上部承重构件") for index in range(1, 5)]
    ctx, sup = superstructure_with(rows)
    for row, kind in zip(ctx.parts[0].defect_rows, ["纵向裂缝", "渗水泛碱", "纵向裂缝", "水蚀"]):
        row.defect_type = kind

    line = defect_summary_lines(ctx, sup)[0]

    assert line == "上部承重构件：纵向裂缝、渗水泛碱、水蚀。"


def test_summary_drops_the_catch_all_type_when_others_exist() -> None:
    """「其它病害」是个标签，写进"查到了哪些病害"等于什么都没说。"""
    rows = [defect_row(index, part_name="上部承重构件") for index in range(1, 4)]
    ctx, sup = superstructure_with(rows)
    for row, kind in zip(ctx.parts[0].defect_rows, ["其它病害", "纵向裂缝", "其它病害"]):
        row.defect_type = kind

    assert defect_summary_lines(ctx, sup)[0] == "上部承重构件：纵向裂缝。"


def test_summary_keeps_the_catch_all_type_when_it_is_the_only_one() -> None:
    """滤空了就会说成"未见明显病害"，而病害确实记着——那是假话。

    实测百股大桥 2026：河床与照明、标志各只有一条病害，且正好都是「其它病害」。
    """
    ctx, sup = superstructure_with([defect_row(1, part_name="上部承重构件")])
    ctx.parts[0].defect_rows[0].defect_type = "其它病害"

    line = defect_summary_lines(ctx, sup)[0]

    assert line == "上部承重构件：其它病害。"
    assert "未见明显病害" not in line


def test_summary_follows_the_standard_component_order() -> None:
    ctx, sup = superstructure_with([defect_row(1, part_name="上部一般构件")])

    lines = defect_summary_lines(ctx, sup)

    # 权重表的顺序：上部承重构件、上部一般构件；不按"谁有病害谁在前"。
    assert lines[0].startswith("上部承重构件")
    assert lines[1].startswith("上部一般构件")


def test_summary_is_empty_without_the_component_list() -> None:
    """没有规范包就列不全部件；宁可不出概要，也不出一份漏了部件的清单。"""
    ctx = context(assessment=assessment(component_weights=[]))
    ctx.parts.append(
        ReportStructurePart(**part("SUPERSTRUCTURE", "上部结构",
                                   rows=[defect_row(1)], photos=[]))
    )

    assert defect_summary_lines(ctx, ctx.parts[0]) == []


def test_chapter_four_intros_cite_one_standard_and_the_real_table_number() -> None:
    """表号由生成器填，不写死在文字里；规范全称全篇统一（设计 §7.6）。"""
    for template in (COMPONENT_WEIGHTS_INTRO, ASSESSMENT_RESULT_INTRO):
        assert STANDARD_CITATION in template
        assert "{number}" in template
    assert STANDARD_CITATION in NO_CONTROL_INDICATOR_TEXT

    assert COMPONENT_WEIGHTS_INTRO.format(number="表4.1-1").endswith("见表4.1-1所列。")
    assert ASSESSMENT_RESULT_INTRO.format(number="表4.1-2").endswith("见表4.1-2所示。")


# --------------------------------------------------------------------------
# 第 5 章结论（设计 §14）
# --------------------------------------------------------------------------


def test_conclusion_has_the_four_fixed_paragraphs() -> None:
    paragraphs = conclusion_paragraphs(context())

    assert len(paragraphs) == 4
    assert paragraphs[0].startswith("本次检查，百股大桥全桥技术状况评分 86.4 分")
    assert paragraphs[1].startswith("各结构技术状况评定结果：")
    assert paragraphs[2].startswith("对评分影响较大的病害为：")
    assert paragraphs[3] == MAINTENANCE_ADVICE["2类"]


def test_top_deductions_are_ordered_and_capped() -> None:
    """扣分从大到小，只点名前三条——不同桥梁的结论长度要可预期。"""
    ctx = context(
        assessment=assessment(
            top_deductions=[
                {"part_code": "DECK", "component_number": f"C{n}",
                 "defect_type": "坑槽", "deduction": float(n)}
                for n in (9, 7, 5, 3)
            ]
        )
    )

    text = conclusion_paragraphs(ctx)[2]

    assert text.index("C9") < text.index("C7") < text.index("C5")
    assert "C3" not in text


def test_paragraphs_without_evidence_are_omitted_not_filled_with_placeholders() -> None:
    """没扣分病害就不出那一段，不写"无"或"暂无数据"占位。"""
    ctx = context(assessment=assessment(top_deductions=[]))

    paragraphs = conclusion_paragraphs(ctx)

    assert all("对评分影响较大" not in text for text in paragraphs)
    assert all("暂无数据" not in text for text in paragraphs)


def test_advice_comes_from_the_controlled_table() -> None:
    """建议只从受控文本里选，等级不认识就不给建议（设计 §14 第 4 条）。"""
    ctx = context(assessment=assessment(overall_grade="6类"))

    paragraphs = conclusion_paragraphs(ctx)

    assert all(text not in MAINTENANCE_ADVICE.values() for text in paragraphs)


def test_advice_table_covers_every_grade() -> None:
    assert set(MAINTENANCE_ADVICE) == {"1类", "2类", "3类", "4类", "5类"}


def test_no_advice_mentions_cost_schedule_or_safety() -> None:
    """设计 §14 第 5 条：工程量、预算、工期、结构安全结论都没有数据依据。"""
    for advice in MAINTENANCE_ADVICE.values():
        for forbidden in ("工程量", "预算", "万元", "工期", "承载能力", "安全性"):
            assert forbidden not in advice


def test_conclusion_is_deterministic() -> None:
    """同一份上下文不得因为运行时间或外部模型不同产生另一套结论（设计 §14）。"""
    ctx = context()

    assert conclusion_paragraphs(ctx) == conclusion_paragraphs(ctx)


# --------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("value", "expected"),
    [(78.25, "78.3"), (86.4321, "86.4"), (90.0, "90.0"), (None, "—")],
)
def test_scores_round_half_up(value: float | None, expected: str) -> None:
    """四舍五入，不是 Python 默认的"四舍六入五成双"。

    78.25 按默认格式化会印成 78.2；差的这 0.1 分可能正好跨过等级分界线。
    """
    assert format_score(value) == expected


def test_context_without_assessment_is_rejected_before_it_reaches_here() -> None:
    """没有正式评定却带着评分，只可能来自臆造或陈旧数据。"""
    with pytest.raises(Exception):
        context(assessment={**NO_ASSESSMENT, "overall_score": 88.0})
