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


def make_liaoning_table(rows: list[list[str]], *, title: str = "表2.1-1 上部结构病害检查表") -> DocxTable:
    return DocxTable(index=0, title=title, chapter="桥梁外观检查", rows=[LIAONING_HEADERS, *rows])


def parse_single_table(table: DocxTable):
    return parse_defect_tables([table], select_rule_set("辽宁国省干线"))


def test_derives_only_explicit_quantity_expressions() -> None:
    assert derive_quantity_text("1处蜂窝、麻面，S=0.6×0.1m²") == "1处"
    assert derive_quantity_text("多条横向裂缝,L=1.2m") == "多条"
    assert derive_quantity_text("勾缝砂浆脱落,长度：5m") is None
    assert derive_quantity_text(None) is None


def test_extracts_scale_deduction_and_component_score_columns() -> None:
    table = make_liaoning_table(
        [
            ["上部承重构件", "1-1#板", "底板", "蜂窝、麻面", "1处", "S=0.3m²", "2", "35", "65", "2.1-1"],
        ]
    )

    defects, groups, _warnings, errors = parse_single_table(table)

    assert errors == []
    assert len(defects) == 1
    assert defects[0].defect_scale == 2
    assert defects[0].defect_deduction == 35.0
    assert defects[0].defect_location == "底板"
    assert defects[0].severity is None
    assert len(groups) == 1
    group = groups[0]
    assert group.structure_part == "上部结构"
    assert group.component_name == "上部承重构件"
    assert group.component_alias == "1-1#板"
    assert group.source_score == 65.0
    assert group.defect_candidate_ids == ["defect_0001"]


def test_component_group_emits_single_rating_for_multiple_rows() -> None:
    table = make_liaoning_table(
        [
            ["上部承重构件", "2-1#板", "小桩号侧", "蜂窝、麻面", "1处", "S=0.6×0.1m²", "2", "35", "55.81", "2.1-1"],
            ["上部承重构件", "2-1#板", "左侧端部", "剥落、掉角", "1处", "长度：0.5m", "2", "20", "55.81", "2.1-2"],
            ["上部承重构件", "1-1#板", "底板", "蜂窝、麻面", "1处", "S=0.3m²", "2", "35", "65", "2.1-3"],
        ]
    )

    _defects, groups, _warnings, _errors = parse_single_table(table)

    assert [(group.component_alias, group.source_score) for group in groups] == [
        ("2-1#板", 55.81),
        ("1-1#板", 65.0),
    ]
    assert groups[0].defect_candidate_ids == ["defect_0001", "defect_0002"]
    assert groups[1].defect_candidate_ids == ["defect_0003"]
    assert groups[0].warnings == []


def test_component_score_propagates_from_group_first_row() -> None:
    # 仅组首行有构件评分、后续行为空（未合并单元格的形态）。
    table = make_liaoning_table(
        [
            ["上部承重构件", "2-1#板", "小桩号侧", "蜂窝、麻面", "1处", "S=0.6×0.1m²", "2", "35", "55.81", "2.1-1"],
            ["", "", "左侧端部", "剥落、掉角", "1处", "长度：0.5m", "2", "20", "", "2.1-2"],
        ]
    )

    _defects, groups, _warnings, _errors = parse_single_table(table)

    assert len(groups) == 1
    assert groups[0].source_score == 55.81
    assert groups[0].defect_candidate_ids == ["defect_0001", "defect_0002"]


def test_invalid_scale_and_deduction_keep_none_and_warn() -> None:
    table = make_liaoning_table(
        [
            ["上部承重构件", "1-1#板", "底板", "蜂窝、麻面", "1处", "S=0.3m²", "轻微", "150", "65", "2.1-1"],
        ]
    )

    defects, _groups, _warnings, errors = parse_single_table(table)

    assert errors == []
    defect = defects[0]
    assert defect.defect_scale is None
    assert defect.defect_deduction is None
    codes = {warning.code for warning in defect.warnings}
    assert "defect_scale_invalid" in codes
    assert "defect_deduction_invalid" in codes


def test_blank_photo_number_is_a_normal_defect_without_warning() -> None:
    table = make_liaoning_table(
        [
            ["上部承重构件", "1-1#板", "底板", "蜂窝、麻面", "1处", "S=0.3m²", "2", "35", "65", ""],
        ]
    )

    defects, _groups, warnings, errors = parse_single_table(table)

    assert errors == []
    assert "photo_number_missing" not in {warning.code for warning in warnings}
    assert len(defects) == 1
    assert defects[0].photo_numbers == []
    assert "photo_number_missing" not in {warning.code for warning in defects[0].warnings}


def test_conflicting_group_scores_keep_first_and_warn() -> None:
    table = make_liaoning_table(
        [
            ["上部承重构件", "2-1#板", "小桩号侧", "蜂窝、麻面", "1处", "S=0.6×0.1m²", "2", "35", "55.81", "2.1-1"],
            ["上部承重构件", "2-1#板", "左侧端部", "剥落、掉角", "1处", "长度：0.5m", "2", "20", "60", "2.1-2"],
        ]
    )

    _defects, groups, _warnings, _errors = parse_single_table(table)

    assert len(groups) == 1
    assert groups[0].source_score == 55.81
    assert [warning.code for warning in groups[0].warnings] == ["component_score_source_invalid"]


def test_table_without_score_columns_produces_no_groups() -> None:
    table = DocxTable(
        index=0,
        title="表2.1-1 上部结构病害检查表",
        chapter="桥梁外观检查",
        rows=[
            ["构件", "位置", "病害", "数量", "尺寸", "照片编号"],
            ["主梁", "第二跨左幅梁底", "裂缝", "1处", "L=0.8m，W=0.12mm", "2.1-1"],
        ],
    )

    defects, groups, _warnings, errors = parse_single_table(table)

    assert errors == []
    assert len(defects) == 1
    assert defects[0].defect_scale is None
    assert defects[0].defect_deduction is None
    assert groups == []
