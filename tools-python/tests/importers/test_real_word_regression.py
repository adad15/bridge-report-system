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

    # 合同 1.2：表 2.x-1 详细位置、标度、扣分与构件评分来源值的精确断言。
    def defects_of(alias: str):
        return [defect for defect in response.data.defects if defect.component_alias == alias]

    def rating_of(alias: str):
        matches = [
            rating
            for rating in response.data.ratings.component_ratings
            if rating.component_ref.component_alias == alias
        ]
        assert len(matches) == 1, f"expected exactly one component rating for {alias}"
        return matches[0]

    board_1_1 = defects_of("1-1#板")
    assert [defect.defect_scale for defect in board_1_1] == [2]
    assert [defect.defect_deduction for defect in board_1_1] == [35.0]

    board_1_2 = defects_of("1-2#板")
    assert [defect.defect_scale for defect in board_1_2] == [2]
    assert [defect.defect_deduction for defect in board_1_2] == [35.0]

    board_2_1 = defects_of("2-1#板")
    assert sorted(defect.defect_deduction for defect in board_2_1) == [20.0, 35.0]

    assert rating_of("1-1#板").source_score == 65
    assert rating_of("1-2#板").source_score == 65
    assert rating_of("2-1#板").source_score == 55.81
    assert set(rating_of("2-1#板").deduction_defect_candidate_ids) == {
        defect.candidate_id for defect in board_2_1
    }

    # JTG/T H21-2011 4.1.1 复算：三块板均应与 Word 来源分一致并预填最终分。
    from bridge_report_tools.scoring.component_score import round2

    for alias, expected in [("1-1#板", 65.0), ("1-2#板", 65.0), ("2-1#板", 55.81)]:
        rating = rating_of(alias)
        assert rating.calculated_score is not None, alias
        assert round2(rating.calculated_score) == expected, alias
        assert rating.score_validation_status == "一致", alias
        assert rating.confirmed_score == expected, alias
        assert rating.score_resolution_reason is None, alias
    assert rating_of("2-1#板").calculation_details.ordered_deductions == [35.0, 20.0]

    # 第四章：上部承重构件与全桥的精确值。
    assert any(
        part.evaluation_part == "上部承重构件" and part.part_score == 86.62
        for part in response.data.ratings.evaluation_parts
    )
    assert response.data.ratings.overall.total_score == 85.61
    assert response.data.ratings.overall.overall_grade == "2类"
