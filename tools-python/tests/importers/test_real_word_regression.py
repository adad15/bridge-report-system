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
    assert 15 == 1 + len(response.data.ratings.structure_parts) + len(response.data.ratings.evaluation_parts)
    assert 10 == sum(1 for defect in response.data.defects if defect.quantity_text)
    assert any(defect.component_alias == "2-1#板" for defect in response.data.defects)
