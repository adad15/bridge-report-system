import pytest

from bridge_report_tools.importers.source_db.defects import (
    build_defect_candidates,
    resolve_component,
)
from bridge_report_tools.importers.source_db.reader import ComponentNode, SourceDefect

PART = ComponentNode("t-part", "上部结构", "001", 1, None, None)
CATEGORY = ComponentNode("t-cat", "上部承重构件", "001001", 2, 825, None)
COMPONENT = ComponentNode("t-a", "25-1#板", "001001001", 3, None, "空心板")
NAMELESS = ComponentNode("t-x", "1-1#板", "001001002", 3, None, "空心板")
ORPHAN = ComponentNode("t-y", "9#台帽", "009009009", 3, None, None)
TREE = [PART, CATEGORY, COMPONENT, NAMELESS, ORPHAN]

CODES = {
    "idx-spall": ("5.1.1-2", "剥落、掉角"),
    "idx-water": ("5.1.1-13", "水损（参照混凝土碳化执行）"),
    "idx-other": ("11", "其他"),
    "judgeIndex_other": ("11", "其他"),
}
GROUP_CODES = {"jt-1": ("5.1.1", "板式构件")}


def defect(**overrides) -> SourceDefect:
    base = dict(
        id="d-1", tree_id="t-a", name="受渗水侵蚀，混凝土剥蚀破损",
        description="受渗水侵蚀，总面积：2.5㎡", position="左侧翼缘板", degree=2,
        judge_index_id="idx-spall", judge_tree_id="jt-1",
        template_definition="受渗水侵蚀，总面积：${面积一}",
        dimensions={"面积一": ("2.5", "㎡")}, direction="",
    )
    base.update(overrides)
    return SourceDefect(**base)


def build(defects, **kwargs):
    candidates, _ = build_defect_candidates(
        defects, TREE, GROUP_CODES, CODES, **kwargs)
    return candidates


def test_carries_the_raw_source_group_and_indicator_identity() -> None:
    candidate = build([defect()])[0]

    assert candidate["source_defect_group_id"] == "jt-1"
    assert candidate["source_defect_group_number"] == "5.1.1"
    assert candidate["source_defect_indicator_id"] == "idx-spall"
    assert candidate["source_defect_indicator_number"] == "5.1.1-2"


def test_carries_a_unit_extension_indicator_as_is() -> None:
    candidate = build([defect(judge_index_id="idx-water", name="渗水泛碱")])[0]

    assert candidate["source_defect_indicator_id"] == "idx-water"
    assert candidate["source_defect_indicator_number"] == "5.1.1-13"
    assert candidate["defect_type"] == "渗水泛碱"


def test_never_emits_resolution_fields() -> None:
    """选节点要看桥型与构件类别，那是 C++ 的上下文；解析器不越权。

    5.0 之后这些字段连合同都没有了，解析器只要写出任意一个，整份候选就会被
    ``extra="forbid"`` 挡在导入之外。
    """
    candidate = build([defect()])[0]

    for field_name in (
        "bridge_component_id",
        "standard_component_category_id",
        "resolved_structure_part",
        "component_inventory_revision_id",
        "component_match_candidate_ids",
        "component_match_method",
        "component_match_confirmed_by",
        "rating_tree_version_id",
        "rating_tree_node_id",
        "rating_tree_match_method",
        "rating_tree_match_evidence",
        "standard_defect_indicator_id",
        "range_split_origin",
    ):
        assert field_name not in candidate


def test_takes_the_component_number_and_category_from_the_tree() -> None:
    candidate = build([defect()])[0]

    assert candidate["component_number"] == "25-1#板"
    assert candidate["component_name"] == "上部承重构件"


def test_falls_back_when_the_parent_category_is_missing() -> None:
    """component_name 在契约里必须非空，取不到就沿回退链走，不能让整次导入失败。"""
    orphan = build([defect(tree_id="t-y")])[0]

    # 父级 009009 不存在 → 退到构件类型 → 也没有 → 退到部位名 → 仍没有 → 用构件名兜底
    assert orphan["component_name"] == "9#台帽"
    assert any(w["code"] == "source_component_category_missing" for w in orphan["warnings"])


def test_uses_the_member_type_when_the_parent_has_no_name() -> None:
    candidate = build([defect(tree_id="t-x")], category_names={"001001": ""})[0]

    assert candidate["component_name"] == "空心板"


def test_fills_an_explicit_source_other_indicator_as_other_disease() -> None:
    candidate = build([defect(
        name="", description="存在熏黑痕迹", judge_index_id="judgeIndex_other")])[0]

    assert candidate["defect_type"] == "其它病害"
    assert candidate["defect_description"] == "存在熏黑痕迹"


def test_keeps_an_unclassified_blank_defect_type_for_manual_review() -> None:
    """没有明确“其它”来源指标时，不能把所有空类型都吞进其它病害。"""
    candidate = build([defect(
        name="", description="待人工判断", judge_index_id="idx-unknown")])[0]

    assert candidate["defect_type"] == ""
    assert candidate["defect_description"] == "待人工判断"


def test_builds_measurements_from_the_split_columns() -> None:
    candidate = build([defect(dimensions={
        "长度": ("11", "m"), "宽度": ("3", "mm"), "数量": ("1", "条")},
        description="裂缝")])[0]

    kinds = {m["dimension_type"]: m for m in candidate["measurements"]}
    assert kinds["长度"]["value"] == 11.0
    assert kinds["长度"]["unit"] == "m"
    assert kinds["宽度"]["value"] == 3.0
    assert kinds["数量"]["unit"] == "条"
    assert all(m["value_type"] == "single" for m in candidate["measurements"])


def test_deduplicates_source_area_and_description_total_area() -> None:
    candidate = build([defect(
        dimensions={"面积一": ("2.5", "㎡")},
        description="受渗水侵蚀，总面积：2.5㎡",
    )])[0]

    assert len(candidate["measurements"]) == 1
    assert candidate["measurements"][0]["dimension_type"] == "面积"
    assert candidate["measurements"][0]["source_text"] == "2.5㎡"


def test_supplements_missing_range_measurements_from_description() -> None:
    candidate = build([defect(
        dimensions={},
        description="多条纵、横向裂缝，长度范围：0.5~4.0m，宽度范围：0.5~1.0cm",
    )])[0]

    assert candidate["measurements"] == [
        {
            "dimension_type": "长度", "value_type": "range", "value": None,
            "minimum_value": 0.5, "maximum_value": 4.0, "unit": "m",
            "is_approximate": False, "source_text": "长度范围：0.5~4.0m",
        },
        {
            "dimension_type": "宽度", "value_type": "range", "value": None,
            "minimum_value": 0.5, "maximum_value": 1.0, "unit": "cm",
            "is_approximate": False, "source_text": "宽度范围：0.5~1.0cm",
        },
    ]
    assert not any(
        warning["code"] == "measurement_parse_low_confidence"
        for warning in candidate["warnings"])


def test_source_columns_win_while_description_supplements_other_dimensions() -> None:
    candidate = build([defect(
        dimensions={"长度": ("3", "m")},
        description="裂缝长度：3.0m，宽度范围：0.5~1.0cm",
    )])[0]

    assert [(item["dimension_type"], item["value_type"])
            for item in candidate["measurements"]] == [
        ("长度", "single"), ("宽度", "range")]
    assert candidate["measurements"][0]["source_text"] == "3m"
    assert not any(
        warning["code"] == "source_measurement_conflict"
        for warning in candidate["warnings"])


def test_keeps_multiple_description_measurements_when_source_dimension_is_missing() -> None:
    candidate = build([defect(
        dimensions={},
        description="两条裂缝，长度：0.5m，长度：1.0m",
    )])[0]

    lengths = [
        item for item in candidate["measurements"]
        if item["dimension_type"] == "长度"
    ]
    assert [item["value"] for item in lengths] == [0.5, 1.0]


def test_warns_when_source_column_conflicts_with_description() -> None:
    candidate = build([defect(
        dimensions={"长度": ("3", "m")},
        description="裂缝长度范围：2~4m",
    )])[0]

    assert len(candidate["measurements"]) == 1
    assert candidate["measurements"][0]["value"] == 3.0
    assert any(
        warning["code"] == "source_measurement_conflict"
        and warning["target_candidate_id"] == candidate["candidate_id"]
        for warning in candidate["warnings"])


def test_carries_the_scale_the_inspector_recorded() -> None:
    assert build([defect(degree=3)])[0]["defect_scale"] == 3
    assert build([defect(degree=None)])[0]["defect_scale"] is None


def test_keeps_the_range_notation_for_the_backend_to_split() -> None:
    """拆分由 C++ 现有的 ComponentRangeParser 做，解析器原样带过去。"""
    ranged = ComponentNode("t-r", "1-1#板~1-25#板", "001001003", 3, None, "空心板")
    candidates, _ = build_defect_candidates(
        [defect(tree_id="t-r")], TREE + [ranged], GROUP_CODES, CODES)

    assert candidates[0]["component_number"] == "1-1#板~1-25#板"


def test_candidate_ids_are_stable_across_runs() -> None:
    first = build([defect(), defect(id="d-2")])
    second = build([defect(), defect(id="d-2")])

    assert [c["candidate_id"] for c in first] == [c["candidate_id"] for c in second]
    assert len({c["candidate_id"] for c in first}) == 2


def test_marks_every_candidate_as_imported_rather_than_hand_written() -> None:
    """契约的 source_ref.source_type 只有 word / manual 两个值，没有"源系统"。

    写 manual 会让这 279 条被当成人工新增的病害（将来若有"人工新增可删"之类的规则
    就会误伤）；写 word 至少表达的是"来自导入"。真实来源另记在 remark 里，不藏。
    """
    candidate = build([defect()])[0]

    assert candidate["source_ref"]["source_type"] == "word"
    assert "来源软件" in candidate["remark"]
    assert candidate["review_status"] == "待确认"
    assert candidate["group_review_status"] == "待确认"


def test_reports_a_defect_whose_component_is_unknown() -> None:
    candidates = build([defect(tree_id="missing")])

    assert candidates[0]["component_number"] is None
    assert any(w["code"] == "source_component_not_found" for w in candidates[0]["warnings"])


def test_resolve_component_returns_none_for_an_unknown_tree_id() -> None:
    assert resolve_component(TREE, "nope") is None
    assert resolve_component(TREE, "t-a").name == "25-1#板"


def test_returns_a_link_keyed_by_the_source_defect_id() -> None:
    """照片是按源病害 id 绑定的；映射若按候选 id 索引，照片一张也挂不上。"""
    _, links = build_defect_candidates(
        [defect(id="src-9")], TREE, GROUP_CODES, CODES)

    assert "src-9" in links
    assert links["src-9"].candidate_id == "source_defect_0001"
    assert links["src-9"].tree_id == "t-a"
