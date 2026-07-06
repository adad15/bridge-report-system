from __future__ import annotations

from datetime import date
from pathlib import Path
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator

from bridge_report_tools.contracts.annual_inspection import (
    BridgeAnnualInspectionData,
    DataRole,
    FileRole,
    SourceType,
)


ImportMode = Literal["新桥初始化", "已有桥年度导入"]
Module04SourceType = Literal["软件导出Word", "正式Word"]
Module04FileRole = Literal["当前年度检测资料", "历史基线资料"]
Module04DataRole = Literal["当前年度", "历史基线"]


class WordImportModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class WordImportRequest(WordImportModel):
    docx_path: Path
    temporary_photo_output_dir: Path
    import_mode: ImportMode
    source_type: Module04SourceType
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

    @field_validator("docx_path")
    @classmethod
    def require_docx_path(cls, value: Path) -> Path:
        if value.suffix.lower() != ".docx":
            raise ValueError("docx_path must point to a .docx file")
        return value

    @field_validator("temporary_photo_output_dir")
    @classmethod
    def require_photo_output_dir(cls, value: Path) -> Path:
        if value.exists() and not value.is_dir():
            raise ValueError("temporary_photo_output_dir must be a directory")
        return value

    def contract_source_type(self) -> SourceType:
        return self.source_type

    def contract_file_role(self) -> FileRole:
        return self.file_role

    def contract_data_role(self) -> DataRole:
        return self.data_role


class WordImportResponse(WordImportModel):
    data: BridgeAnnualInspectionData
    temporary_photo_files: list[str]
