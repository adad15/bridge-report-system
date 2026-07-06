from pathlib import Path

import pytest
from docx import Document
from pydantic import ValidationError

from bridge_report_tools.contracts.annual_inspection import DataRole, FileRole, SourceType
from bridge_report_tools.importers.defect_tables import parse_defect_tables
from bridge_report_tools.importers.docx_reader import read_docx_blocks
from bridge_report_tools.importers.word_context import ImportMode, WordImportRequest
from tests.importers.docx_fixtures import create_sample_docx, write_png


def valid_request(tmp_path: Path) -> WordImportRequest:
    docx_path = tmp_path / "sample.docx"
    docx_path.write_bytes(b"not-a-real-docx-yet")
    photo_dir = tmp_path / "photos"
    photo_dir.mkdir()
    return WordImportRequest(
        docx_path=docx_path,
        temporary_photo_output_dir=photo_dir,
        import_mode="已有桥年度导入",
        source_type="软件导出Word",
        file_role="当前年度检测资料",
        data_role="当前年度",
        selected_bridge_system_number="QL-000001",
        selected_bridge_name="绕阳河二号桥",
        inspection_year=2026,
        inspection_date="2026-05-18",
        report_number="Q202605001-JZ-024",
        project_name="绕阳河二号桥2026年度定期检测",
        archived_file_system_number="GDWJ-000001",
        import_record_system_number="DRJL-000001",
    )


def test_word_import_request_accepts_module04_current_year_context(tmp_path: Path) -> None:
    request = valid_request(tmp_path)

    assert request.import_mode == "已有桥年度导入"
    assert request.source_type == "软件导出Word"
    assert request.file_role == "当前年度检测资料"
    assert request.data_role == "当前年度"
    assert request.docx_path.suffix == ".docx"


def test_word_import_request_accepts_new_bridge_baseline_context(tmp_path: Path) -> None:
    request = valid_request(tmp_path).model_copy(
        update={
            "import_mode": "新桥初始化",
            "source_type": "正式Word",
            "file_role": "历史基线资料",
            "data_role": "历史基线",
            "inspection_year": 2025,
        }
    )

    assert request.import_mode == "新桥初始化"
    assert request.source_type == "正式Word"
    assert request.file_role == "历史基线资料"
    assert request.data_role == "历史基线"
    assert request.inspection_year == 2025


def test_word_import_request_rejects_non_docx(tmp_path: Path) -> None:
    doc_path = tmp_path / "sample.doc"
    doc_path.write_text("old word format", encoding="utf-8")
    photo_dir = tmp_path / "photos"
    photo_dir.mkdir()

    with pytest.raises(ValidationError) as exc_info:
        WordImportRequest(
            docx_path=doc_path,
            temporary_photo_output_dir=photo_dir,
            import_mode="已有桥年度导入",
            source_type="软件导出Word",
            file_role="当前年度检测资料",
            data_role="当前年度",
            selected_bridge_system_number="QL-000001",
            selected_bridge_name="绕阳河二号桥",
            inspection_year=2026,
            inspection_date="2026-05-18",
            report_number="Q202605001-JZ-024",
            project_name="绕阳河二号桥2026年度定期检测",
            archived_file_system_number="GDWJ-000001",
            import_record_system_number="DRJL-000001",
        )

    assert "docx_path" in str(exc_info.value)


def test_word_import_request_rejects_revision_role(tmp_path: Path) -> None:
    request = valid_request(tmp_path)
    with pytest.raises(ValidationError) as exc_info:
        WordImportRequest(
            docx_path=request.docx_path,
            temporary_photo_output_dir=request.temporary_photo_output_dir,
            import_mode=request.import_mode,
            source_type=request.source_type,
            file_role=request.file_role,
            data_role="修订版",
            selected_bridge_system_number=request.selected_bridge_system_number,
            selected_bridge_name=request.selected_bridge_name,
            inspection_year=request.inspection_year,
            inspection_date=request.inspection_date,
            report_number=request.report_number,
            project_name=request.project_name,
            archived_file_system_number=request.archived_file_system_number,
            import_record_system_number=request.import_record_system_number,
        )

    assert "data_role" in str(exc_info.value)


def test_type_aliases_match_contract_literals() -> None:
    source_type: SourceType = "软件导出Word"
    file_role: FileRole = "当前年度检测资料"
    data_role: DataRole = "当前年度"
    import_mode: ImportMode = "已有桥年度导入"

    assert source_type == "软件导出Word"
    assert file_role == "当前年度检测资料"
    assert data_role == "当前年度"
    assert import_mode == "已有桥年度导入"


def test_dynamic_docx_fixture_contains_expected_tables(tmp_path: Path) -> None:
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = create_sample_docx(tmp_path / "sample.docx", image_path)

    document = Document(str(docx_path))

    assert len(document.tables) == 2
    assert document.tables[0].rows[0].cells[0].text == "构件"
    assert document.tables[1].rows[0].cells[0].text == "层级"


def test_read_docx_blocks_keeps_table_titles(tmp_path: Path) -> None:
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = create_sample_docx(tmp_path / "sample.docx", image_path)

    document = read_docx_blocks(docx_path)

    assert document.paragraph_texts[0] == "绕阳河二号桥 定期检测报告"
    assert document.tables[0].title == "上部结构病害检查表"
    assert document.tables[0].chapter == "第二章 结构病害检查"
    assert document.tables[0].rows[0] == ["构件", "位置", "病害", "数量", "尺寸", "照片编号"]
    assert document.tables[1].title == "总体技术状况评定表"
    assert document.tables[1].chapter == "第四章 全桥技术状况综合评定"


def test_parse_defect_tables_extracts_defect_candidate(tmp_path: Path) -> None:
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = create_sample_docx(tmp_path / "sample.docx", image_path)
    document = read_docx_blocks(docx_path)

    defects, warnings, errors = parse_defect_tables(document.tables)

    assert warnings == []
    assert errors == []
    assert len(defects) == 1
    defect = defects[0]
    assert defect.candidate_id == "defect_0001"
    assert defect.structure_part == "上部结构"
    assert defect.component_name == "主梁"
    assert defect.defect_location == "第二跨左幅梁底"
    assert defect.defect_type == "裂缝"
    assert defect.quantity_text == "1处"
    assert defect.measurement_text == "L=0.8m，W=0.12mm"
    assert [item.dimension_type for item in defect.measurements] == ["长度", "宽度"]
    assert defect.photo_numbers == ["2.1-1"]
    assert defect.source_ref.table_title == "上部结构病害检查表"
