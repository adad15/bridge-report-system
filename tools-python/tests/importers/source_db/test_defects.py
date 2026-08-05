import pytest

from bridge_report_tools.importers.source_db.defects import (
    build_defect_candidates,
    h21_indicator_id,
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
}
H21_IDS = {"h21.defect.5_1_1_2"}


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
    return build_defect_candidates(defects, TREE, CODES, H21_IDS, **kwargs)


def test_maps_the_source_index_code_to_an_h21_indicator() -> None:
    assert h21_indicator_id("5.1.1-2") == "h21.defect.5_1_1_2"
    assert h21_indicator_id("10.1.2-7") == "h21.defect.10_1_2_7"


def test_carries_the_indicator_when_h21_has_it() -> None:
    candidate = build([defect()])[0]

    assert candidate["standard_defect_indicator_id"] == "h21.defect.5_1_1_2"


def test_leaves_the_indicator_blank_when_h21_lacks_it() -> None:
    """H21 的 5.1.1 只有 12 项，没有水损；硬凑一个不存在的编号只会误导下游。"""
    candidate = build([defect(judge_index_id="idx-water", name="渗水泛碱")])[0]

    assert candidate["standard_defect_indicator_id"] is None
    # 文字照常带出去——现有别名表认得「渗水泛碱」，这条走文字匹配。
    assert candidate["defect_type"] == "渗水泛碱"


def test_never_fills_the_rating_tree_node() -> None:
    """选节点要看桥型与构件类别，那是 C++ 的上下文；解析器不越权。"""
    candidate = build([defect()])[0]

    assert candidate["rating_tree_node_id"] is None
    assert candidate["rating_tree_version_id"] is None
    assert candidate["rating_tree_match_method"] is None


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


def test_keeps_a_blank_defect_type() -> None:
    """来源软件允许病害不选类型，空串是合法值。"""
    candidate = build([defect(name="", description="存在熏黑痕迹", judge_index_id="idx-other")])[0]

    assert candidate["defect_type"] == ""
    assert candidate["defect_description"] == "存在熏黑痕迹"


def test_builds_measurements_from_the_split_columns() -> None:
    candidate = build([defect(dimensions={
        "长度": ("11", "m"), "宽度": ("3", "mm"), "数量": ("1", "条")})])[0]

    kinds = {m["dimension_type"]: m for m in candidate["measurements"]}
    assert kinds["长度"]["value"] == 11.0
    assert kinds["长度"]["unit"] == "m"
    assert kinds["宽度"]["value"] == 3.0
    assert kinds["数量"]["unit"] == "条"
    # 尺寸已经是拆好的结构化数据，不能再回头去解析文字。
    assert all(m["value_type"] == "single" for m in candidate["measurements"])


def test_carries_the_scale_the_inspector_recorded() -> None:
    assert build([defect(degree=3)])[0]["defect_scale"] == 3
    assert build([defect(degree=None)])[0]["defect_scale"] is None


def test_keeps_the_range_notation_for_the_backend_to_split() -> None:
    """拆分由 C++ 现有的 ComponentRangeParser 做，解析器原样带过去。"""
    ranged = ComponentNode("t-r", "1-1#板~1-25#板", "001001003", 3, None, "空心板")
    candidates = build_defect_candidates(
        [defect(tree_id="t-r")], TREE + [ranged], CODES, H21_IDS)

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
