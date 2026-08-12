from __future__ import annotations

from datetime import datetime, timezone

from pydantic import ValidationError

from bridge_report_tools.contracts.annual_inspection import (
    BridgeAnnualInspectionData,
    BridgeCheck,
    ContractInfo,
    ImportContext,
    InspectionInfo,
    WarningItem,
)
from bridge_report_tools.importers.defect_tables import parse_defect_tables
from bridge_report_tools.importers.docx_reader import read_docx_blocks
from bridge_report_tools.importers.photo_extractor import extract_and_match_photos
from bridge_report_tools.importers.word_context import WordImportRequest, WordImportResponse
from bridge_report_tools.importers.word_errors import WordImportError
from bridge_report_tools.importers.word_rules import select_rule_set


PARSER_VERSION = "0.3.0"


def extract_bridge_name(paragraph_texts: list[str], selected_bridge_name: str) -> str | None:
    for text in paragraph_texts[:10]:
        if selected_bridge_name in text:
            return selected_bridge_name
    return None


def build_bridge_check(request: WordImportRequest, paragraph_texts: list[str]) -> BridgeCheck:
    extracted_bridge_name = extract_bridge_name(paragraph_texts, request.selected_bridge_name)
    warnings: list[WarningItem] = []
    match_status = "匹配" if extracted_bridge_name == request.selected_bridge_name else "待人工确认"
    if extracted_bridge_name is None:
        warnings.append(
            WarningItem(
                code="bridge_name_not_found",
                message="未在 Word 前部文本中识别到系统选择的桥梁名称，请人工确认。",
                severity="warning",
                target_candidate_id=None,
            )
        )

    return BridgeCheck(
        selected_bridge_system_number=request.selected_bridge_system_number,
        extracted_bridge_name=extracted_bridge_name,
        match_status=match_status,
        warnings=warnings,
    )


def parse_word_import(request: WordImportRequest) -> WordImportResponse:
    if not request.docx_path.exists():
        raise WordImportError(
            code="docx_open_failed",
            message=f"Word 文件不存在：{request.docx_path}",
        )
    if request.temporary_photo_output_dir.exists() and not request.temporary_photo_output_dir.is_dir():
        raise WordImportError(
            code="temporary_photo_output_unwritable",
            message=f"临时图片输出路径不是目录：{request.temporary_photo_output_dir}",
        )

    rule_set = select_rule_set(request.rule_profile)

    document = read_docx_blocks(request.docx_path)
    defects, defect_warnings, defect_errors = parse_defect_tables(document.tables, rule_set)
    photos, temporary_photo_files, photo_warnings = extract_and_match_photos(
        request.docx_path,
        document,
        defects,
        request.temporary_photo_output_dir,
        rule_set,
    )

    try:
        data = BridgeAnnualInspectionData(
            contract=ContractInfo(
                name="BridgeAnnualInspectionData",
                version="4.0",
                generated_at=datetime.now(timezone.utc),
                producer="python-tools",
                parser_name="word_importer",
                parser_version=PARSER_VERSION,
            ),
            import_context=ImportContext(
                source_type=request.contract_source_type(),
                file_role=request.contract_file_role(),
                archived_file_system_number=request.archived_file_system_number,
                import_record_system_number=request.import_record_system_number,
            ),
            bridge_check=build_bridge_check(request, document.paragraph_texts),
            inspection=InspectionInfo(
                inspection_year=request.inspection_year,
                inspection_date=request.inspection_date,
                report_number=request.report_number,
                project_name=request.project_name,
                data_role=request.contract_data_role(),
            ),
            defects=defects,
            photos=photos,
            comparison_candidates=[],
            report_text_candidates=[],
            warnings=defect_warnings + photo_warnings,
            errors=defect_errors,
        )
        validated = BridgeAnnualInspectionData.model_validate(data.model_dump())
    except ValidationError as exc:
        raise WordImportError(
            code="contract_validation_failed",
            message=str(exc),
        ) from exc

    return WordImportResponse(data=validated, temporary_photo_files=temporary_photo_files)
