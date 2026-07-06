from pathlib import Path
import zipfile

import pytest
from docx import Document
from pydantic import ValidationError

from bridge_report_tools.contracts.annual_inspection import DataRole, FileRole, SourceType
from bridge_report_tools.importers.defect_tables import parse_defect_tables
from bridge_report_tools.importers.docx_reader import DocxBlocks, DocxTable
from bridge_report_tools.importers.docx_reader import read_docx_blocks
from bridge_report_tools.importers.photo_extractor import extract_and_match_photos, find_photo_captions, media_members
from bridge_report_tools.importers.rating_tables import parse_rating_tables
from bridge_report_tools.importers.word_context import ImportMode, WordImportRequest
from bridge_report_tools.importers.word_errors import WordImportError
from tests.importers.docx_fixtures import (
    add_defect_table,
    add_photo,
    add_rating_table,
    create_docx_without_rating_table,
    create_sample_docx,
    write_png,
)


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


def test_parse_defect_tables_ignores_non_defect_table_with_disease_title() -> None:
    table = DocxTable(
        index=0,
        title="主要病害及技术状况评定表",
        chapter="第四章 全桥技术状况综合评定",
        rows=[
            ["层级", "结构部位", "类别编号", "评价部件", "评分", "权重", "等级", "构件评分"],
            ["全桥", "全桥", "", "全桥", "85.61", "", "2类", ""],
        ],
    )

    defects, warnings, errors = parse_defect_tables([table])

    assert defects == []
    assert warnings == []
    assert len(errors) == 1
    assert errors[0].code == "defect_tables_not_found"


def test_parse_defect_tables_reports_missing_defect_table(tmp_path: Path) -> None:
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = create_sample_docx(tmp_path / "sample.docx", image_path)
    document = read_docx_blocks(docx_path)

    defects, warnings, errors = parse_defect_tables(document.tables[1:])

    assert defects == []
    assert warnings == []
    assert len(errors) == 1
    assert errors[0].code == "defect_tables_not_found"


def test_parse_rating_tables_extracts_overall_structure_and_evaluation_parts(tmp_path: Path) -> None:
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = create_sample_docx(tmp_path / "sample.docx", image_path)
    document = read_docx_blocks(docx_path)

    ratings, warnings = parse_rating_tables(document.tables)

    assert warnings == []
    assert ratings.overall.total_score == 85.61
    assert ratings.overall.overall_grade == "2类"
    assert [item.structure_part for item in ratings.structure_parts] == ["上部结构", "下部结构", "桥面系"]
    assert ratings.structure_parts[2].grade == "3"
    assert len(ratings.evaluation_parts) == 1
    assert ratings.evaluation_parts[0].evaluation_part == "上部承重构件"
    assert ratings.evaluation_parts[0].part_score == 86.62
    assert ratings.evaluation_parts[0].score_rows[0].component_count == 3
    assert ratings.evaluation_parts[0].score_rows[0].component_score == 86.62
    assert not hasattr(ratings.evaluation_parts[0], "grade")


def test_parse_rating_tables_fails_when_fourth_chapter_table_missing(tmp_path: Path) -> None:
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = create_docx_without_rating_table(tmp_path / "sample.docx", image_path)
    document = read_docx_blocks(docx_path)

    with pytest.raises(WordImportError) as exc_info:
        parse_rating_tables(document.tables)

    assert exc_info.value.code == "rating_table_not_found"
    assert exc_info.value.message == "未识别到第四章总体技术状况评定表。"


def test_parse_rating_tables_ignores_non_fourth_chapter_rating_like_table() -> None:
    table = DocxTable(
        index=0,
        title="主要病害及技术状况评定表",
        chapter="第二章 结构病害检查",
        rows=[
            ["层级", "结构部位", "类别编号", "评价部件", "评分", "权重", "等级", "构件评分"],
            ["全桥", "全桥", "", "全桥", "85.61", "", "2类", ""],
        ],
    )

    with pytest.raises(WordImportError) as exc_info:
        parse_rating_tables([table])

    assert exc_info.value.code == "rating_table_not_found"


def test_parse_rating_tables_ignores_unrelated_fourth_chapter_table_without_rating_headers() -> None:
    table = DocxTable(
        index=0,
        title="技术状况文字说明表",
        chapter="第四章 全桥技术状况综合评定",
        rows=[
            ["层级", "说明"],
            ["全桥", "整体情况说明"],
        ],
    )

    with pytest.raises(WordImportError) as exc_info:
        parse_rating_tables([table])

    assert exc_info.value.code == "rating_table_not_found"
    assert exc_info.value.message == "未识别到第四章总体技术状况评定表。"


def test_parse_rating_tables_ignores_partial_fourth_chapter_rating_headers() -> None:
    table = DocxTable(
        index=0,
        title="技术状况文字说明表",
        chapter="第四章 全桥技术状况综合评定",
        rows=[
            ["层级", "评分", "等级"],
            ["全桥", "整体情况说明", "2类"],
        ],
    )

    with pytest.raises(WordImportError) as exc_info:
        parse_rating_tables([table])

    assert exc_info.value.code == "rating_table_not_found"


def test_parse_rating_tables_skips_malformed_fourth_chapter_rating_candidate() -> None:
    table = DocxTable(
        index=0,
        title="总体技术状况评定表",
        chapter="第四章 全桥技术状况综合评定",
        rows=[
            ["层级", "结构部位", "评分", "等级"],
            ["全桥", "全桥", "整体情况说明", "2类"],
        ],
    )

    with pytest.raises(WordImportError) as exc_info:
        parse_rating_tables([table])

    assert exc_info.value.code == "rating_table_not_found"


def test_parse_defect_tables_keeps_row_level_warnings() -> None:
    table = DocxTable(
        index=0,
        title="上部结构病害检查表",
        chapter="第二章 结构病害检查",
        rows=[
            ["构件", "位置", "病害", "数量", "尺寸", "照片编号"],
            ["主梁", "第二跨左幅梁底", "破损", "1处", "局部破损，约20cm×30cm", ""],
        ],
    )

    defects, warnings, errors = parse_defect_tables([table])

    assert warnings == []
    assert errors == []
    assert len(defects) == 1
    warning_codes = [warning.code for warning in defects[0].warnings]
    assert "measurement_parse_low_confidence" in warning_codes
    assert "photo_number_missing" in warning_codes


def test_extract_and_match_photos_links_caption_to_defect(tmp_path: Path) -> None:
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = create_sample_docx(tmp_path / "sample.docx", image_path)
    document = read_docx_blocks(docx_path)
    defects, _, _ = parse_defect_tables(document.tables)
    photo_output_dir = tmp_path / "out"

    photos, temporary_files, warnings = extract_and_match_photos(docx_path, document, defects, photo_output_dir)

    assert warnings == []
    assert temporary_files == ["photo_0001.png"]
    assert (photo_output_dir / "photo_0001.png").exists()
    assert len(photos) == 1
    assert photos[0].photo_number == "2.1-1"
    assert photos[0].linked_defect_candidate_id == "defect_0001"
    assert photos[0].match_status == "高置信候选"
    assert photos[0].extracted_file.temporary_file_name == "photo_0001.png"
    assert photos[0].extracted_file.original_caption == "照片2.1-1 主梁梁底裂缝"


def test_find_photo_captions_ignores_dates_without_caption_prefix() -> None:
    document = DocxBlocks(
        paragraph_texts=["检测日期 2026-05-18", "照片 2026-05-18", "照片2.1-1 主梁裂缝"],
        tables=[],
    )

    captions = find_photo_captions(document)

    assert captions == [("2.1-1", "照片2.1-1 主梁裂缝")]


def test_extract_and_match_photos_keeps_unreferenced_photo_warning(tmp_path: Path) -> None:
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = tmp_path / "sample.docx"
    document_obj = Document()
    add_defect_table(document_obj)
    add_photo(document_obj, image_path, "照片2.1-3 桥面铺装局部破损")
    add_rating_table(document_obj)
    document_obj.save(docx_path)
    document = read_docx_blocks(docx_path)
    defects, _, _ = parse_defect_tables(document.tables)

    photos, temporary_files, warnings = extract_and_match_photos(docx_path, document, defects, tmp_path / "out")

    assert warnings == []
    assert temporary_files == ["photo_0001.png"]
    assert photos[0].photo_number == "2.1-3"
    assert photos[0].linked_defect_candidate_id is None
    assert photos[0].match_status == "未关联"
    assert photos[0].warnings[0].code == "photo_not_referenced_by_defect"


def test_media_members_uses_document_body_relationship_order(tmp_path: Path) -> None:
    docx_path = tmp_path / "ordered.docx"
    with zipfile.ZipFile(docx_path, "w") as archive:
        archive.writestr(
            "word/document.xml",
            """
            <w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"
                xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"
                xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
                <w:body>
                    <w:p><w:r><w:drawing><a:blip r:embed="rId2" /></w:drawing></w:r></w:p>
                    <w:p><w:r><w:drawing><a:blip r:embed="rId1" /></w:drawing></w:r></w:p>
                </w:body>
            </w:document>
            """,
        )
        archive.writestr(
            "word/_rels/document.xml.rels",
            """
            <Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
                <Relationship Id="rId1"
                    Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image"
                    Target="media/image10.png" />
                <Relationship Id="rId2"
                    Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image"
                    Target="media/image2.png" />
            </Relationships>
            """,
        )
        archive.writestr("word/media/image10.png", b"image10")
        archive.writestr("word/media/image2.png", b"image2")
        archive.writestr("word/media/logo.png", b"logo")

    assert media_members(docx_path) == ["word/media/image2.png", "word/media/image10.png"]


def test_extract_and_match_photos_unmatched_defect_warning_is_idempotent(tmp_path: Path) -> None:
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = tmp_path / "sample.docx"
    document_obj = Document()
    add_defect_table(document_obj)
    add_photo(document_obj, image_path, "照片2.1-3 桥面铺装局部破损")
    add_rating_table(document_obj)
    document_obj.save(docx_path)
    document = read_docx_blocks(docx_path)
    defects, _, _ = parse_defect_tables(document.tables)

    extract_and_match_photos(docx_path, document, defects, tmp_path / "out")
    extract_and_match_photos(docx_path, document, defects, tmp_path / "out")

    warning_codes = [warning.code for warning in defects[0].warnings]
    assert warning_codes.count("photo_number_unmatched") == 1
