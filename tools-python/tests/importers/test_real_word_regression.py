from __future__ import annotations

import os
from pathlib import Path

import pytest

from bridge_report_tools.importers.word_context import WordImportRequest
from bridge_report_tools.importers.word_importer import parse_word_import


def test_liaoning_real_word_regression(tmp_path: Path) -> None:
    configured_path = os.getenv("BRIDGE_REPORT_REAL_WORD_PATH")
    if not configured_path:
        pytest.skip("BRIDGE_REPORT_REAL_WORD_PATH is not configured")

    docx_path = Path(configured_path)
    if not docx_path.is_file():
        pytest.fail(f"real Word fixture does not exist: {docx_path}")

    request = WordImportRequest(
        docx_path=docx_path,
        temporary_photo_output_dir=tmp_path / "photos",
        rule_profile="辽宁国省干线",
        import_mode="已有桥年度导入",
        source_type="软件导出Word",
        file_role="当前年度检测资料",
        data_role="当前年度",
        selected_bridge_system_number="QL-REAL-WORD",
        selected_bridge_name="绕阳河二号桥",
        inspection_year=2026,
        inspection_date="2026-07-07",
        report_number="REAL-WORD-REGRESSION",
        project_name="绕阳河二号桥真实Word回归",
        archived_file_system_number="GDWJ-REAL-WORD",
        import_record_system_number="DRJL-REAL-WORD",
    )

    response = parse_word_import(request)

    assert len(response.data.defects) == 25
    assert len(response.data.photos) == 31
    assert len(response.temporary_photo_files) == 36
    assert 31 == sum(1 for photo in response.data.photos if photo.extracted_file.temporary_file_name)
    assert 10 == sum(1 for defect in response.data.defects if defect.quantity_text)
    assert any(defect.component_number == "2-1#板" for defect in response.data.defects)

    # 合同 4.0：Word 只提供病害事实证据，不再接收报告中的扣分和评分。
    wire_data = response.data.model_dump(mode="json")
    assert wire_data["contract"]["version"] == "4.0"
    assert "ratings" not in wire_data
    assert all("defect_deduction" not in defect for defect in wire_data["defects"])
    assert all("component_alias" not in defect for defect in wire_data["defects"])
    assert all("structure_part" not in defect for defect in wire_data["defects"])

    def defects_of(component_number: str):
        return [
            defect
            for defect in response.data.defects
            if defect.component_number == component_number
        ]

    board_1_1 = defects_of("1-1#板")
    assert [defect.defect_scale for defect in board_1_1] == [2]

    board_1_2 = defects_of("1-2#板")
    assert [defect.defect_scale for defect in board_1_2] == [2]

    board_2_1 = defects_of("2-1#板")
    assert len(board_2_1) == 2
    assert all(defect.component_name == "上部承重构件" for defect in board_2_1)
    assert all(defect.bridge_component_id is None for defect in response.data.defects)
    assert all(defect.standard_component_category_id is None for defect in response.data.defects)
    assert all(defect.resolved_structure_part is None for defect in response.data.defects)

    measurements = [
        measurement
        for defect in response.data.defects
        for measurement in defect.measurements
    ]
    assert measurements
    assert all(measurement.source_text for measurement in measurements)
    assert all(
        measurement.value is not None
        and measurement.minimum_value is None
        and measurement.maximum_value is None
        for measurement in measurements
        if measurement.value_type == "single"
    )
