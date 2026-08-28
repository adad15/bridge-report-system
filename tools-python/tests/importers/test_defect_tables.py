from bridge_report_tools.importers.defect_tables import derive_quantity_text, parse_defect_tables
from bridge_report_tools.importers.docx_reader import DocxTable
from bridge_report_tools.importers.word_rules import select_rule_set


LIAONING_HEADERS = [
    "部件名称",
    "构件编号",
    "病害位置",
    "病害类型",
    "数量",
    "病害特征",
    "标度",
    "病害扣分",
    "构件评分",
    "照片编号",
]


def make_liaoning_table(
    rows: list[list[str]],
    *,
    title: str = "表2.1-1 上部结构病害检查表",
) -> DocxTable:
    return DocxTable(
        index=0,
        title=title,
        chapter="桥梁外观检查",
        rows=[LIAONING_HEADERS, *rows],
    )


def parse_single_table(table: DocxTable):
    return parse_defect_tables([table], select_rule_set("辽宁国省干线"))


def test_derives_only_explicit_quantity_expressions() -> None:
    assert derive_quantity_text("1处蜂窝、麻面，S=0.6×0.1m²") == "1处"
    assert derive_quantity_text("多条横向裂缝,L=1.2m") == "多条"
    assert derive_quantity_text("勾缝砂浆脱落,长度：5m") is None
    assert derive_quantity_text(None) is None


def test_extracts_scale_component_number_and_internal_fields() -> None:
    table = make_liaoning_table(
        [["上部承重构件", "1-1#板", "底板", "蜂窝、麻面", "1处", "S=0.3m²", "2", "35", "65", "2.1-1"]]
    )

    defects, warnings, errors = parse_single_table(table)

    assert errors == []
    assert not any("扣分" in warning.message or "评分" in warning.message for warning in warnings)
    assert len(defects) == 1
    defect = defects[0]
    assert defect.source_structure_part == "上部结构"
    assert defect.component_name == "上部承重构件"
    assert defect.component_number == "1-1#板"
    assert defect.defect_scale == 2
    assert defect.defect_location == "底板"
    # 5.0 起构件解析不在来源事实里，模型上连字段都不该存在。
    assert not hasattr(defect, "bridge_component_id")
    assert not hasattr(defect, "standard_component_category_id")
    assert not hasattr(defect, "resolved_structure_part")
    assert not hasattr(defect, "defect_deduction")
    assert not any("扣分" in warning.message or "评分" in warning.message for warning in defect.warnings)


def test_invalid_scale_keeps_none_and_warns_without_reading_deduction() -> None:
    table = make_liaoning_table(
        [["上部承重构件", "1-1#板", "底板", "蜂窝、麻面", "1处", "S=0.3m²", "轻微", "坏扣分", "坏评分", "2.1-1"]]
    )

    defects, _warnings, errors = parse_single_table(table)

    assert errors == []
    defect = defects[0]
    assert defect.defect_scale is None
    assert [warning.code for warning in defect.warnings] == ["defect_scale_invalid"]


def test_blank_photo_number_is_a_normal_defect_without_warning() -> None:
    table = make_liaoning_table(
        [["上部承重构件", "1-1#板", "底板", "蜂窝、麻面", "1处", "S=0.3m²", "2", "35", "65", ""]]
    )

    defects, warnings, errors = parse_single_table(table)

    assert errors == []
    assert "photo_number_missing" not in {warning.code for warning in warnings}
    assert len(defects) == 1
    assert defects[0].photo_references == []
    assert "photo_number_missing" not in {warning.code for warning in defects[0].warnings}


def test_placeholder_location_cell_becomes_empty_and_drops_out_of_the_description() -> None:
    table = make_liaoning_table(
        [["上部承重构件", "1-1#板", "/", "渗水泛碱", "1处", "S=0.3m²", "2", "35", "65", "2.1-1"]]
    )

    defects, _warnings, errors = parse_single_table(table)

    assert errors == []
    defect = defects[0]
    assert defect.defect_location == ""
    assert defect.defect_type == "渗水泛碱"
    assert defect.defect_description == "渗水泛碱"
    # Word 原文仍然完整保留在来源证据里，清洗只影响业务字段。
    assert "/" in defect.source_ref.raw_row_text


def test_every_full_width_and_dash_placeholder_is_treated_as_empty() -> None:
    for placeholder in ["／", " / ", "—", "–", "-", "  "]:
        table = make_liaoning_table(
            [["上部承重构件", "1-1#板", placeholder, "渗水泛碱", "1处", "S=0.3m²", "2", "35", "65", "2.1-1"]]
        )

        defects, _warnings, errors = parse_single_table(table)

        assert errors == []
        assert defects[0].defect_location == "", placeholder
        assert defects[0].defect_description == "渗水泛碱", placeholder


def test_placeholder_defect_type_never_produces_a_trailing_slash_description() -> None:
    table = make_liaoning_table(
        [["上部承重构件", "1-1#板", "板底", "/", "1处", "S=0.3m²", "2", "35", "65", "2.1-1"]]
    )

    defects, _warnings, errors = parse_single_table(table)

    assert errors == []
    defect = defects[0]
    assert defect.defect_type == ""
    assert defect.defect_location == "板底"
    assert defect.defect_description == "板底"


def test_two_placeholder_cells_fall_back_to_the_existing_missing_value_hint() -> None:
    table = make_liaoning_table(
        [["上部承重构件", "1-1#板", "/", "／", "1处", "S=0.3m²", "2", "35", "65", "2.1-1"]]
    )

    defects, _warnings, errors = parse_single_table(table)

    assert errors == []
    assert defects[0].defect_description == "未识别病害描述"


def test_meaningful_slashes_and_dashes_inside_a_cell_are_never_stripped() -> None:
    table = make_liaoning_table(
        [["上部承重构件", "1-1#板", "板底/腹板交界处", "裂缝", "1处", "L/W=2", "2", "35", "65", "2.1-1"]]
    )

    defects, _warnings, errors = parse_single_table(table)

    assert errors == []
    defect = defects[0]
    assert defect.defect_location == "板底/腹板交界处"
    assert defect.measurement_text == "L/W=2"
    assert defect.defect_description == "板底/腹板交界处裂缝"


def test_table_without_score_columns_still_produces_version_three_defect() -> None:
    table = DocxTable(
        index=0,
        title="表2.1-1 上部结构病害检查表",
        chapter="桥梁外观检查",
        rows=[
            ["构件", "构件编号", "位置", "病害", "数量", "尺寸", "照片编号"],
            ["主梁", "2-3#梁", "第二跨左幅梁底", "裂缝", "1处", "L=0.8m，W=0.12mm", "2.1-1"],
        ],
    )

    defects, _warnings, errors = parse_single_table(table)

    assert errors == []
    assert len(defects) == 1
    assert defects[0].component_number == "2-3#梁"
    assert defects[0].defect_scale is None
