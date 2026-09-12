"""来源库导入的请求模型与编排。

产出与 Word 那条路完全相同的 `WordImportResponse`——契约就是两条路的解耦层，
后端只认这个形状，不关心数据是从 docx 来的还是从离线库来的。
"""

from __future__ import annotations

from datetime import date, datetime, timezone
from pathlib import Path

from pydantic import BaseModel, ConfigDict, Field, ValidationError

from bridge_report_tools.contracts.annual_inspection import (
    BridgeAnnualInspectionData,
    BridgeCheck,
    ContractInfo,
    ImportContext,
    InspectionInfo,
)
from bridge_report_tools.importers.source_db.defects import build_defect_candidates
from bridge_report_tools.importers.source_db.photos import build_photo_candidates
from bridge_report_tools.importers.source_db.reader import (
    SourceDatabaseError,
    load_component_tree,
    load_defects,
    load_group_codes,
    load_indicator_codes,
    load_photos,
    load_task,
    open_source_db,
)
from bridge_report_tools.importers.word_context import (
    Module04DataRole,
    Module04FileRole,
    WordImportResponse,
)

PARSER_VERSION = "0.1.0"


class SourceImportRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    source_db_path: Path
    task_id: str = Field(min_length=1)
    temporary_photo_output_dir: Path
    import_mode: str
    file_role: Module04FileRole
    data_role: Module04DataRole
    selected_bridge_system_number: str = Field(min_length=1)
    selected_bridge_name: str = Field(min_length=1)
    inspection_year: int = Field(ge=1900, le=2200)
    inspection_date: date
    report_number: str = Field(min_length=1)
    project_name: str = Field(min_length=1)
    archived_file_system_number: str = Field(min_length=1)
    import_record_system_number: str = Field(min_length=1)


def parse_source_import(request: SourceImportRequest) -> WordImportResponse:
    with open_source_db(request.source_db_path) as db:
        task = load_task(db, request.task_id)
        tree = load_component_tree(db, request.task_id)
        defects, links = build_defect_candidates(
            load_defects(db, request.task_id),
            tree,
            load_group_codes(db),
            load_indicator_codes(db),
        )
        photos, temporary_photo_files = build_photo_candidates(
            db, load_photos(db, request.task_id), defects, links, tree,
            request.temporary_photo_output_dir)

    try:
        data = BridgeAnnualInspectionData(
            contract=ContractInfo(
                name="BridgeAnnualInspectionData",
                version="5.0",
                generated_at=datetime.now(timezone.utc),
                producer="python-tools",
                parser_name="source_db_importer",
                parser_version=PARSER_VERSION,
            ),
            import_context=ImportContext(
                # 契约枚举里已有「接口同步」，不需要为这条路改契约。
                source_type="接口同步",
                file_role=request.file_role,
                archived_file_system_number=request.archived_file_system_number,
                import_record_system_number=request.import_record_system_number,
            ),
            bridge_check=BridgeCheck(
                selected_bridge_system_number=request.selected_bridge_system_number,
                extracted_bridge_name=task.name or None,
                match_status="匹配" if task.name == request.selected_bridge_name else "待人工确认",
                warnings=[],
            ),
            inspection=InspectionInfo(
                inspection_year=request.inspection_year,
                inspection_date=request.inspection_date,
                report_number=request.report_number,
                project_name=request.project_name,
                data_role=request.data_role,
            ),
            defects=defects,
            photos=photos,
            # 来源库里没有跨年度对比与报告正文；契约允许空数组，不阻塞导入。
            comparison_candidates=[],
            report_text_candidates=[],
            warnings=[],
            errors=[],
        )
        validated = BridgeAnnualInspectionData.model_validate(data.model_dump())
    except ValidationError as exc:
        raise SourceDatabaseError("contract_validation_failed", str(exc)) from exc

    return WordImportResponse(data=validated, temporary_photo_files=temporary_photo_files)
