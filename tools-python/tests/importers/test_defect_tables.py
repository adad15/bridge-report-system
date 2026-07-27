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
    assert defect.bridge_component_id is None
    assert defect.standard_component_category_id is None
    assert defect.resolved_structure_part is None
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
