# Bridge Annual Inspection Data Contract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现模块 03 的 `BridgeAnnualInspectionData` JSON 契约落地物：共享 JSON 样例、Python Pydantic 模型与 JSON Schema 导出、C++ JsonCpp 校验器、前端 TypeScript 类型与运行时校验，并用测试保证三端接受同一份结构。

**Architecture:** Python 契约模型是最严格表达和 JSON Schema 来源；`contracts/` 与 `samples/contracts/` 是跨端共享资产；C++ 与前端使用轻量运行时校验保护保存、校对和入库入口。本模块只处理候选 JSON 契约，不实现 Word 解析、页面、数据库迁移、事实入库或病害对比算法。

**Tech Stack:** Python 3.11 + Pydantic v2 + pytest/uv；C++20 + JsonCpp + GoogleTest + CMake/vcpkg manifest；TypeScript 5 + Vitest + Vite；JSON Schema Draft 2020-12。

---

## Scope Boundaries

- [ ] 保持本模块只落地 `BridgeAnnualInspectionData` 契约、校验器、样例和文档。
- [ ] 不修改 PostgreSQL schema。模块 2 的 `import_records.parsed_result_json` 已能承载候选 JSON。
- [ ] 不实现 Word/docx 表格解析。
- [ ] 不实现前端人工校对页面。
- [ ] 不实现第 N 年与第 N-1 年病害对比算法。
- [ ] 不把模板正文、自然语言结论段或正式报告正文写成事实来源。
- [ ] 不提交当前已存在但不属于本计划的未跟踪文件：`docs/superpowers/diagrams/`、`docs/superpowers/specs/module03/`、`scripts/dev/generate-module03-diagrams.py`。

## File Changes

Create these files:

- [ ] `samples/contracts/bridge_annual_inspection_data.valid.json`
- [ ] `samples/contracts/bridge_annual_inspection_data.invalid-evaluation-part-grade.json`
- [ ] `samples/contracts/bridge_annual_inspection_data.with-comparison.json`
- [ ] `contracts/bridge_annual_inspection_data.schema.json`
- [ ] `contracts/README.md`
- [ ] `tools-python/bridge_report_tools/contracts/__init__.py`
- [ ] `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- [ ] `tools-python/bridge_report_tools/contracts/export_schema.py`
- [ ] `tools-python/tests/test_annual_inspection_contract.py`
- [ ] `backend-cpp/include/bridge_report/contracts/AnnualInspectionContract.hpp`
- [ ] `backend-cpp/src/contracts/AnnualInspectionContract.cpp`
- [ ] `backend-cpp/tests/test_annual_inspection_contract.cpp`
- [ ] `frontend/src/contracts/annualInspection.ts`
- [ ] `frontend/src/contracts/annualInspection.test.ts`

Modify these files:

- [ ] `tools-python/pyproject.toml`
- [ ] `backend-cpp/CMakeLists.txt`
- [ ] `README.md`

## Step 0: Preflight

- [ ] Confirm the branch is the module 03 branch.

```powershell
git branch --show-current
```

Expected output:

```text
feature/03-bridge-annual-inspection-data-contract
```

- [ ] Check worktree state before edits.

```powershell
git status --short
```

Expected known untracked entries:

```text
?? docs/superpowers/diagrams/
?? docs/superpowers/plans/2026-07-03-bridge-annual-inspection-data-contract-implementation-plan.md
?? docs/superpowers/specs/module03/
?? scripts/dev/generate-module03-diagrams.py
```

If the plan file has already been committed, it will not appear in this output. If additional tracked modifications appear, inspect them before editing and do not overwrite user work.

## Step 1: Add Shared JSON Fixtures

The fixtures are the cross-language contract examples. Python, C++ and TypeScript tests must load or mirror these structures.

### 1.1 Create Valid Fixture

- [ ] Create `samples/contracts/bridge_annual_inspection_data.valid.json`.

Use this exact content:

```json
{
  "contract": {
    "name": "BridgeAnnualInspectionData",
    "version": "1.0",
    "generated_at": "2026-07-03T10:30:00+08:00",
    "producer": "python-tools",
    "parser_name": "word_table_importer",
    "parser_version": "0.1.0"
  },
  "import_context": {
    "source_type": "软件导出Word",
    "file_role": "当前年度检测资料",
    "archived_file_system_number": "GDWJ-000001",
    "import_record_system_number": "DRJL-000001"
  },
  "bridge_check": {
    "selected_bridge_system_number": "QL-000001",
    "extracted_bridge_name": "绕阳河二号桥",
    "match_status": "匹配",
    "warnings": []
  },
  "inspection": {
    "inspection_year": 2026,
    "inspection_date": "2026-05-12",
    "report_number": "Q202605001-JZ-024",
    "project_name": "绕阳河二号桥定期检测",
    "data_role": "当前年度"
  },
  "defects": [
    {
      "candidate_id": "defect_0001",
      "structure_part": "上部结构",
      "component_name": "主梁",
      "component_alias": "1#孔主梁",
      "defect_type": "裂缝",
      "defect_location": "梁底",
      "defect_description": "梁底存在横向裂缝",
      "quantity_text": "1处",
      "measurement_text": "L=0.8m，W=0.12mm",
      "measurements": [
        {
          "dimension_type": "长度",
          "value": 0.8,
          "unit": "m",
          "source_text": "L=0.8m"
        },
        {
          "dimension_type": "宽度",
          "value": 0.12,
          "unit": "mm",
          "source_text": "W=0.12mm"
        }
      ],
      "photo_numbers": [
        "2.1-1"
      ],
      "severity": null,
      "remark": null,
      "source_ref": {
        "chapter": "第二章",
        "table_title": "上部结构病害检查表",
        "table_index": 3,
        "row_index": 5,
        "column_name": "病害描述",
        "raw_row_text": "主梁 | 梁底 | 裂缝 | L=0.8m，W=0.12mm | 2.1-1"
      },
      "confidence": 0.92,
      "review_status": "待确认",
      "review_note": null,
      "warnings": []
    }
  ],
  "photos": [
    {
      "candidate_id": "photo_0001",
      "photo_number": "2.1-1",
      "linked_defect_candidate_id": "defect_0001",
      "extracted_file": {
        "temporary_file_name": "photo_0001.jpg",
        "original_caption": "照片2.1-1 主梁梁底裂缝",
        "archive_relative_path": null
      },
      "match_status": "高置信候选",
      "source_ref": {
        "chapter": "第二章",
        "table_title": "上部结构病害检查表",
        "row_index": 5,
        "photo_area_caption": "照片2.1-1 主梁梁底裂缝"
      },
      "confidence": 0.95,
      "review_status": "待确认",
      "warnings": []
    }
  ],
  "ratings": {
    "overall": {
      "total_score": 85.61,
      "overall_grade": "2类",
      "source_ref": {
        "chapter": "第四章",
        "table_title": "总体技术状况评定表"
      },
      "confidence": 0.95,
      "review_status": "待确认"
    },
    "structure_parts": [
      {
        "structure_part": "上部结构",
        "structure_score": 87.45,
        "weight": 0.4,
        "grade": "2",
        "source_ref": {
          "chapter": "第四章",
          "table_title": "总体技术状况评定表",
          "row_index": 1
        },
        "confidence": 0.95,
        "review_status": "待确认"
      },
      {
        "structure_part": "下部结构",
        "structure_score": 86.61,
        "weight": 0.4,
        "grade": "2",
        "source_ref": {
          "chapter": "第四章",
          "table_title": "总体技术状况评定表",
          "row_index": 4
        },
        "confidence": 0.95,
        "review_status": "待确认"
      },
      {
        "structure_part": "桥面系",
        "structure_score": 79.93,
        "weight": 0.2,
        "grade": "3",
        "source_ref": {
          "chapter": "第四章",
          "table_title": "总体技术状况评定表",
          "row_index": 9
        },
        "confidence": 0.95,
        "review_status": "待确认"
      }
    ],
    "evaluation_parts": [
      {
        "structure_part": "上部结构",
        "category_no": 1,
        "evaluation_part": "上部承重构件",
        "part_score": 86.62,
        "score_rows": [
          {
            "component_count": 1,
            "component_score": 55.81
          },
          {
            "component_count": 2,
            "component_score": 65
          },
          {
            "component_count": 13,
            "component_score": 100
          }
        ],
        "source_ref": {
          "chapter": "第四章",
          "table_title": "总体技术状况评定表",
          "row_index": 1
        },
        "confidence": 0.95,
        "review_status": "待确认"
      },
      {
        "structure_part": "上部结构",
        "category_no": 2,
        "evaluation_part": "上部一般构件",
        "part_score": 82.29,
        "score_rows": [
          {
            "component_count": 8,
            "component_score": 75
          },
          {
            "component_count": 6,
            "component_score": 100
          }
        ],
        "source_ref": {
          "chapter": "第四章",
          "table_title": "总体技术状况评定表",
          "row_index": 2
        },
        "confidence": 0.95,
        "review_status": "待确认"
      },
      {
        "structure_part": "上部结构",
        "category_no": 3,
        "evaluation_part": "支座",
        "part_score": 100,
        "score_rows": [
          {
            "component_count": 64,
            "component_score": 100
          }
        ],
        "source_ref": {
          "chapter": "第四章",
          "table_title": "总体技术状况评定表",
          "row_index": 3
        },
        "confidence": 0.95,
        "review_status": "待确认"
      }
    ],
    "warnings": []
  },
  "comparison_candidates": [],
  "report_text_candidates": [],
  "warnings": [],
  "errors": []
}
```

### 1.2 Create Invalid Fixture For Evaluation Part Grade

- [ ] Create `samples/contracts/bridge_annual_inspection_data.invalid-evaluation-part-grade.json`.
- [ ] Copy the valid fixture content.
- [ ] Add a `grade` field inside the first `ratings.evaluation_parts[0]` object:

```json
"grade": "2"
```

This fixture must be rejected because评价部件只有评分，等级只存在于 `ratings.structure_parts[]` 和 `ratings.overall`。

### 1.3 Create Fixture With Comparison Candidate

- [ ] Create `samples/contracts/bridge_annual_inspection_data.with-comparison.json`.
- [ ] Copy the valid fixture content.
- [ ] Replace `comparison_candidates` with:

```json
"comparison_candidates": [
  {
    "candidate_id": "comparison_0001",
    "previous_defect_observation_system_number": "BHGC-000123",
    "current_defect_observation_system_number": "BHGC-000456",
    "comparison_type": "原病害发展",
    "change_summary": "主梁梁底裂缝宽度由 0.10mm 发展至 0.12mm。",
    "match_basis": {
      "same_component": true,
      "same_defect_type": true,
      "location_similarity": 0.82,
      "measurement_change_detected": true,
      "photo_number_related": false
    },
    "confidence": 0.86,
    "confirmation_status": "待确认",
    "review_note": null,
    "warnings": []
  }
]
```

### 1.4 Verify Fixture JSON Syntax

- [ ] Run:

```powershell
python -m json.tool samples/contracts/bridge_annual_inspection_data.valid.json $env:TEMP\bridge_contract_valid.pretty.json
python -m json.tool samples/contracts/bridge_annual_inspection_data.invalid-evaluation-part-grade.json $env:TEMP\bridge_contract_invalid.pretty.json
python -m json.tool samples/contracts/bridge_annual_inspection_data.with-comparison.json $env:TEMP\bridge_contract_with_comparison.pretty.json
```

Expected output: no console output and exit code `0` for all three commands.

- [ ] Commit:

```powershell
git add samples/contracts/bridge_annual_inspection_data.valid.json samples/contracts/bridge_annual_inspection_data.invalid-evaluation-part-grade.json samples/contracts/bridge_annual_inspection_data.with-comparison.json
git commit -m "test: add module 03 contract fixtures"
```

## Step 2: Add Python Pydantic Contract Models

Python is the strictest contract implementation. It validates fixtures and exports JSON Schema.

### 2.1 Add Pydantic Dependency

- [ ] Modify `tools-python/pyproject.toml`.

Change dependencies to include explicit Pydantic v2:

```toml
dependencies = [
  "fastapi>=0.115.0",
  "pydantic>=2.8.0",
  "uvicorn[standard]>=0.30.0"
]
```

### 2.2 Write Failing Python Tests

- [ ] Create `tools-python/tests/test_annual_inspection_contract.py`.

Use this exact test skeleton:

```python
import json
from pathlib import Path

import pytest
from pydantic import ValidationError

from bridge_report_tools.contracts.annual_inspection import (
    BridgeAnnualInspectionData,
    export_bridge_annual_inspection_schema,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
SAMPLES_DIR = REPO_ROOT / "samples" / "contracts"


def load_sample(name: str) -> dict:
    return json.loads((SAMPLES_DIR / name).read_text(encoding="utf-8"))


def test_accepts_valid_bridge_annual_inspection_data() -> None:
    data = BridgeAnnualInspectionData.model_validate(
        load_sample("bridge_annual_inspection_data.valid.json")
    )

    assert data.contract.name == "BridgeAnnualInspectionData"
    assert data.inspection.inspection_year == 2026
    assert data.defects[0].candidate_id == "defect_0001"
    assert data.defects[0].measurements[1].unit == "mm"
    assert data.ratings.overall.total_score == 85.61
    assert data.ratings.structure_parts[2].grade == "3"
    assert data.ratings.evaluation_parts[0].evaluation_part == "上部承重构件"
    assert data.comparison_candidates == []


def test_accepts_comparison_candidate_after_annual_facts_are_confirmed() -> None:
    data = BridgeAnnualInspectionData.model_validate(
        load_sample("bridge_annual_inspection_data.with-comparison.json")
    )

    comparison = data.comparison_candidates[0]

    assert comparison.comparison_type == "原病害发展"
    assert comparison.match_basis.same_component is True
    assert comparison.match_basis.location_similarity == 0.82
    assert comparison.confirmation_status == "待确认"


def test_rejects_grade_on_evaluation_part() -> None:
    with pytest.raises(ValidationError) as error:
        BridgeAnnualInspectionData.model_validate(
            load_sample("bridge_annual_inspection_data.invalid-evaluation-part-grade.json")
        )

    assert "ratings.evaluation_parts.0.grade" in str(error.value)


def test_rejects_confidence_outside_zero_to_one() -> None:
    sample = load_sample("bridge_annual_inspection_data.valid.json")
    sample["defects"][0]["confidence"] = 1.01

    with pytest.raises(ValidationError) as error:
        BridgeAnnualInspectionData.model_validate(sample)

    assert "less than or equal to 1" in str(error.value)


def test_exports_json_schema_with_contract_title(tmp_path: Path) -> None:
    schema_path = tmp_path / "bridge_annual_inspection_data.schema.json"

    export_bridge_annual_inspection_schema(schema_path)
    schema = json.loads(schema_path.read_text(encoding="utf-8"))

    assert schema["$schema"] == "https://json-schema.org/draft/2020-12/schema"
    assert schema["title"] == "BridgeAnnualInspectionData"
    assert "ratings" in schema["properties"]
```

- [ ] Run only the new test file to confirm it fails because the module does not exist yet.

```powershell
cd tools-python
uv run pytest tests/test_annual_inspection_contract.py -q
```

Expected failing output includes:

```text
ModuleNotFoundError: No module named 'bridge_report_tools.contracts'
```

### 2.3 Implement Python Contract Package

- [ ] Create `tools-python/bridge_report_tools/contracts/__init__.py`.

Use:

```python
from bridge_report_tools.contracts.annual_inspection import (
    BridgeAnnualInspectionData,
    export_bridge_annual_inspection_schema,
)

__all__ = [
    "BridgeAnnualInspectionData",
    "export_bridge_annual_inspection_schema",
]
```

- [ ] Create `tools-python/bridge_report_tools/contracts/annual_inspection.py`.

Use this model structure:

```python
from __future__ import annotations

import json
from pathlib import Path
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field


ReviewStatus = Literal["待确认", "已确认", "已修改", "已忽略"]
ComparisonConfirmationStatus = Literal["待确认", "已确认", "已修改", "已拒绝"]
Severity = Literal["info", "warning", "error"]
StructurePart = Literal["全桥", "上部结构", "下部结构", "桥面系", "其他"]
SourceType = Literal["软件导出Word", "正式Word", "Excel病害表", "图片包", "接口同步", "JSON导入"]
FileRole = Literal["当前年度检测资料", "历史正式报告", "历史基线资料", "修订资料"]
DataRole = Literal["当前年度", "历史基线", "修订版"]
BridgeMatchStatus = Literal["匹配", "不匹配", "待人工确认"]
PhotoMatchStatus = Literal["高置信候选", "待校对", "已确认", "未关联", "已忽略"]
ComparisonType = Literal[
    "原病害无明显变化",
    "原病害发展",
    "原病害减轻",
    "原病害修复",
    "新增病害",
    "原病害未见",
    "无法判断",
]


class ContractModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class SourceRef(ContractModel):
    chapter: str | None = None
    table_title: str | None = None
    table_index: int | None = Field(default=None, ge=0)
    row_index: int | None = Field(default=None, ge=0)
    column_name: str | None = None
    raw_row_text: str | None = None
    photo_area_caption: str | None = None
    file_role: str | None = None
    paragraph_index: int | None = Field(default=None, ge=0)


class WarningItem(ContractModel):
    code: str
    message: str
    severity: Severity
    target_candidate_id: str | None = None


class ContractInfo(ContractModel):
    name: Literal["BridgeAnnualInspectionData"]
    version: Literal["1.0"]
    generated_at: str | None = None
    producer: str
    parser_name: str
    parser_version: str


class ImportContext(ContractModel):
    source_type: SourceType
    file_role: FileRole
    archived_file_system_number: str
    import_record_system_number: str


class BridgeCheck(ContractModel):
    selected_bridge_system_number: str
    extracted_bridge_name: str | None = None
    match_status: BridgeMatchStatus
    warnings: list[WarningItem] = Field(default_factory=list)


class InspectionInfo(ContractModel):
    inspection_year: int = Field(ge=1900, le=2200)
    inspection_date: str | None = None
    report_number: str | None = None
    project_name: str | None = None
    data_role: DataRole


class Measurement(ContractModel):
    dimension_type: str
    value: float
    unit: str
    source_text: str


class DefectCandidate(ContractModel):
    candidate_id: str
    structure_part: StructurePart
    component_name: str
    component_alias: str | None = None
    defect_type: str
    defect_location: str | None = None
    defect_description: str | None = None
    quantity_text: str | None = None
    measurement_text: str | None = None
    measurements: list[Measurement] = Field(default_factory=list)
    photo_numbers: list[str] = Field(default_factory=list)
    severity: str | None = None
    remark: str | None = None
    source_ref: SourceRef
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus
    review_note: str | None = None
    warnings: list[WarningItem] = Field(default_factory=list)


class ExtractedPhotoFile(ContractModel):
    temporary_file_name: str | None = None
    original_caption: str | None = None
    archive_relative_path: str | None = None


class PhotoCandidate(ContractModel):
    candidate_id: str
    photo_number: str
    linked_defect_candidate_id: str | None = None
    extracted_file: ExtractedPhotoFile
    match_status: PhotoMatchStatus
    source_ref: SourceRef | None = None
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus
    warnings: list[WarningItem] = Field(default_factory=list)


class OverallRating(ContractModel):
    total_score: float = Field(ge=0, le=100)
    overall_grade: str
    source_ref: SourceRef | None = None
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus


class StructurePartRating(ContractModel):
    structure_part: Literal["上部结构", "下部结构", "桥面系"]
    structure_score: float = Field(ge=0, le=100)
    weight: float = Field(ge=0, le=1)
    grade: str
    source_ref: SourceRef | None = None
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus


class EvaluationScoreRow(ContractModel):
    component_count: int = Field(ge=0)
    component_score: float = Field(ge=0, le=100)


class EvaluationPartRating(ContractModel):
    structure_part: Literal["上部结构", "下部结构", "桥面系"]
    category_no: int = Field(ge=1)
    evaluation_part: str
    part_score: float = Field(ge=0, le=100)
    score_rows: list[EvaluationScoreRow] = Field(default_factory=list)
    source_ref: SourceRef | None = None
    confidence: float = Field(ge=0, le=1)
    review_status: ReviewStatus


class Ratings(ContractModel):
    overall: OverallRating
    structure_parts: list[StructurePartRating]
    evaluation_parts: list[EvaluationPartRating]
    warnings: list[WarningItem] = Field(default_factory=list)


class ComparisonMatchBasis(ContractModel):
    same_component: bool
    same_defect_type: bool
    location_similarity: float = Field(ge=0, le=1)
    measurement_change_detected: bool
    photo_number_related: bool


class ComparisonCandidate(ContractModel):
    candidate_id: str
    previous_defect_observation_system_number: str | None = None
    current_defect_observation_system_number: str | None = None
    comparison_type: ComparisonType
    change_summary: str | None = None
    match_basis: ComparisonMatchBasis | None = None
    confidence: float = Field(ge=0, le=1)
    confirmation_status: ComparisonConfirmationStatus
    review_note: str | None = None
    warnings: list[WarningItem] = Field(default_factory=list)


class ReportTextCandidate(ContractModel):
    candidate_id: str
    section_key: str
    section_title: str
    text: str
    usage: str
    source_ref: SourceRef
    review_status: ReviewStatus


class BridgeAnnualInspectionData(ContractModel):
    contract: ContractInfo
    import_context: ImportContext
    bridge_check: BridgeCheck
    inspection: InspectionInfo
    defects: list[DefectCandidate]
    photos: list[PhotoCandidate]
    ratings: Ratings
    comparison_candidates: list[ComparisonCandidate] = Field(default_factory=list)
    report_text_candidates: list[ReportTextCandidate] = Field(default_factory=list)
    warnings: list[WarningItem] = Field(default_factory=list)
    errors: list[WarningItem] = Field(default_factory=list)


def export_bridge_annual_inspection_schema(path: Path) -> None:
    schema = BridgeAnnualInspectionData.model_json_schema()
    schema["$schema"] = "https://json-schema.org/draft/2020-12/schema"
    path.write_text(
        json.dumps(schema, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
```

This model intentionally does not define `grade` on `EvaluationPartRating`. Because `extra="forbid"` is enabled, the invalid fixture must fail.

### 2.4 Add Schema Export Module

- [ ] Create `tools-python/bridge_report_tools/contracts/export_schema.py`.

Use:

```python
from pathlib import Path

from bridge_report_tools.contracts.annual_inspection import export_bridge_annual_inspection_schema


def main() -> None:
    repo_root = Path(__file__).resolve().parents[3]
    schema_path = repo_root / "contracts" / "bridge_annual_inspection_data.schema.json"
    schema_path.parent.mkdir(parents=True, exist_ok=True)
    export_bridge_annual_inspection_schema(schema_path)


if __name__ == "__main__":
    main()
```

### 2.5 Verify Python Contract

- [ ] Run:

```powershell
cd tools-python
uv run pytest tests/test_annual_inspection_contract.py -q
```

Expected output:

```text
5 passed
```

- [ ] Run all Python tests:

```powershell
cd tools-python
uv run pytest -q
```

Expected output:

```text
6 passed
```

- [ ] Commit:

```powershell
git add tools-python/pyproject.toml tools-python/uv.lock tools-python/bridge_report_tools/contracts tools-python/tests/test_annual_inspection_contract.py
git commit -m "feat(python): add annual inspection contract model"
```

## Step 3: Generate And Check In JSON Schema

The checked-in schema is used by humans and future code generation. Pydantic remains the source for regenerating it.

### 3.1 Generate Schema

- [ ] Run:

```powershell
cd tools-python
uv run python -m bridge_report_tools.contracts.export_schema
```

Expected result:

```text
contracts/bridge_annual_inspection_data.schema.json exists at repository root.
```

- [ ] Validate generated schema JSON syntax:

```powershell
cd ..
python -m json.tool contracts/bridge_annual_inspection_data.schema.json $env:TEMP\bridge_contract_schema.pretty.json
```

Expected output: no console output and exit code `0`.

### 3.2 Add Contracts README

- [ ] Create `contracts/README.md`.

Use:

```markdown
# Contracts

This directory contains checked-in JSON contract artifacts shared by the C++ backend, Python tools, and React frontend.

## BridgeAnnualInspectionData

- Schema: `bridge_annual_inspection_data.schema.json`
- Source model: `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- Valid sample: `samples/contracts/bridge_annual_inspection_data.valid.json`

The object is candidate data for `import_records.parsed_result_json`. It is not formal bridge fact data until the user confirms it and the C++ backend writes the corresponding database tables.

Regenerate the schema from the repository root with:

```powershell
cd tools-python
uv run python -m bridge_report_tools.contracts.export_schema
```
```

### 3.3 Verify Schema Contains Ratings Contract

- [ ] Run:

```powershell
rg -n "\"evaluation_parts\"|\"structure_parts\"|\"overall_grade\"" contracts/bridge_annual_inspection_data.schema.json
```

Expected output includes one match for each of:

```text
"evaluation_parts"
"structure_parts"
"overall_grade"
```

- [ ] Commit:

```powershell
git add contracts/bridge_annual_inspection_data.schema.json contracts/README.md
git commit -m "feat(contract): add module 03 json schema"
```

## Step 4: Add C++ Runtime Contract Validator

C++ needs a small guard before persisting or accepting edited `parsed_result_json`. It does not need to duplicate every Pydantic rule, but it must reject malformed top-level objects and the important module 03 invariant: `ratings.evaluation_parts[]` cannot contain `grade`.

### 4.1 Write Failing C++ Tests

- [ ] Create `backend-cpp/tests/test_annual_inspection_contract.cpp`.

Use this test structure:

```cpp
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <json/json.h>
#include <gtest/gtest.h>

#include "bridge_report/contracts/AnnualInspectionContract.hpp"

#ifndef BRIDGE_REPORT_REPOSITORY_ROOT
#define BRIDGE_REPORT_REPOSITORY_ROOT "."
#endif

namespace {

Json::Value load_sample(const std::string& file_name) {
    const auto path = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT)
        / "samples"
        / "contracts"
        / file_name;

    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot open sample: " + path.string());
    }

    Json::CharReaderBuilder builder;
    Json::Value value;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &value, &errors)) {
        throw std::runtime_error(errors);
    }

    return value;
}

}  // namespace

TEST(AnnualInspectionContractTest, AcceptsValidContractFixture) {
    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(
        load_sample("bridge_annual_inspection_data.valid.json")
    );

    EXPECT_TRUE(result.ok()) << result.summary();
}

TEST(AnnualInspectionContractTest, AcceptsComparisonCandidateFixture) {
    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(
        load_sample("bridge_annual_inspection_data.with-comparison.json")
    );

    EXPECT_TRUE(result.ok()) << result.summary();
}

TEST(AnnualInspectionContractTest, RejectsGradeOnEvaluationPart) {
    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(
        load_sample("bridge_annual_inspection_data.invalid-evaluation-part-grade.json")
    );

    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.summary().find("ratings.evaluation_parts[0].grade"), std::string::npos);
}

TEST(AnnualInspectionContractTest, RejectsConfidenceOutsideZeroToOne) {
    auto sample = load_sample("bridge_annual_inspection_data.valid.json");
    sample["defects"][0]["confidence"] = 1.01;

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(sample);

    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.summary().find("defects[0].confidence"), std::string::npos);
}
```

- [ ] Run configure/build to confirm the test fails because the header does not exist.

```powershell
cd backend-cpp
cmake --preset vs2022-x64-debug
cmake --build --preset vs2022-x64-debug
```

Expected failing output includes:

```text
AnnualInspectionContract.hpp
No such file or directory
```

### 4.2 Implement C++ Header

- [ ] Create `backend-cpp/include/bridge_report/contracts/AnnualInspectionContract.hpp`.

Use:

```cpp
#pragma once

#include <string>
#include <vector>

#include <json/json.h>

namespace bridge_report::contracts {

struct ContractValidationIssue {
    std::string path;
    std::string message;
};

class ContractValidationResult {
public:
    void add_issue(std::string path, std::string message);
    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] const std::vector<ContractValidationIssue>& issues() const noexcept;
    [[nodiscard]] std::string summary() const;

private:
    std::vector<ContractValidationIssue> issues_;
};

ContractValidationResult validate_bridge_annual_inspection_data(const Json::Value& root);

}  // namespace bridge_report::contracts
```

### 4.3 Implement C++ Validator

- [ ] Create `backend-cpp/src/contracts/AnnualInspectionContract.cpp`.

Use this implementation shape:

```cpp
#include "bridge_report/contracts/AnnualInspectionContract.hpp"

#include <sstream>
#include <string>

namespace bridge_report::contracts {

void ContractValidationResult::add_issue(std::string path, std::string message) {
    issues_.push_back({std::move(path), std::move(message)});
}

bool ContractValidationResult::ok() const noexcept {
    return issues_.empty();
}

const std::vector<ContractValidationIssue>& ContractValidationResult::issues() const noexcept {
    return issues_;
}

std::string ContractValidationResult::summary() const {
    std::ostringstream out;
    for (const auto& issue : issues_) {
        out << issue.path << ": " << issue.message << '\n';
    }
    return out.str();
}

namespace {

bool is_confidence_valid(const Json::Value& value) {
    if (!value.isNumeric()) {
        return false;
    }
    const auto number = value.asDouble();
    return number >= 0.0 && number <= 1.0;
}

void require_object(
    ContractValidationResult& result,
    const Json::Value& value,
    const std::string& path
) {
    if (!value.isObject()) {
        result.add_issue(path, "must be an object");
    }
}

void require_array(
    ContractValidationResult& result,
    const Json::Value& value,
    const std::string& path
) {
    if (!value.isArray()) {
        result.add_issue(path, "must be an array");
    }
}

void require_confidence(
    ContractValidationResult& result,
    const Json::Value& value,
    const std::string& path
) {
    if (!value.isMember("confidence")) {
        result.add_issue(path + ".confidence", "is required");
        return;
    }
    if (!is_confidence_valid(value["confidence"])) {
        result.add_issue(path + ".confidence", "must be a number from 0 to 1");
    }
}

void validate_items_with_confidence(
    ContractValidationResult& result,
    const Json::Value& array,
    const std::string& path
) {
    if (!array.isArray()) {
        return;
    }
    for (Json::ArrayIndex index = 0; index < array.size(); ++index) {
        const auto item_path = path + "[" + std::to_string(index) + "]";
        require_object(result, array[index], item_path);
        if (array[index].isObject()) {
            require_confidence(result, array[index], item_path);
        }
    }
}

void validate_evaluation_parts(ContractValidationResult& result, const Json::Value& ratings) {
    const auto& evaluation_parts = ratings["evaluation_parts"];
    require_array(result, evaluation_parts, "ratings.evaluation_parts");
    if (!evaluation_parts.isArray()) {
        return;
    }

    for (Json::ArrayIndex index = 0; index < evaluation_parts.size(); ++index) {
        const auto item_path = "ratings.evaluation_parts[" + std::to_string(index) + "]";
        const auto& item = evaluation_parts[index];
        require_object(result, item, item_path);
        if (!item.isObject()) {
            continue;
        }
        require_confidence(result, item, item_path);
        if (item.isMember("grade")) {
            result.add_issue(item_path + ".grade", "evaluation parts must not include grade");
        }
    }
}

}  // namespace

ContractValidationResult validate_bridge_annual_inspection_data(const Json::Value& root) {
    ContractValidationResult result;

    require_object(result, root, "$");
    if (!root.isObject()) {
        return result;
    }

    for (const auto* member : {
             "contract",
             "import_context",
             "bridge_check",
             "inspection",
             "ratings",
         }) {
        if (!root.isMember(member)) {
            result.add_issue(member, "is required");
        } else {
            require_object(result, root[member], member);
        }
    }

    for (const auto* member : {
             "defects",
             "photos",
             "comparison_candidates",
             "report_text_candidates",
             "warnings",
             "errors",
         }) {
        if (!root.isMember(member)) {
            result.add_issue(member, "is required");
        } else {
            require_array(result, root[member], member);
        }
    }

    if (root.isMember("contract") && root["contract"].isObject()) {
        if (root["contract"]["name"].asString() != "BridgeAnnualInspectionData") {
            result.add_issue("contract.name", "must be BridgeAnnualInspectionData");
        }
        if (root["contract"]["version"].asString() != "1.0") {
            result.add_issue("contract.version", "must be 1.0");
        }
    }

    validate_items_with_confidence(result, root["defects"], "defects");
    validate_items_with_confidence(result, root["photos"], "photos");
    validate_items_with_confidence(result, root["comparison_candidates"], "comparison_candidates");

    if (root.isMember("ratings") && root["ratings"].isObject()) {
        const auto& ratings = root["ratings"];
        if (ratings.isMember("overall") && ratings["overall"].isObject()) {
            require_confidence(result, ratings["overall"], "ratings.overall");
        } else {
            result.add_issue("ratings.overall", "is required");
        }
        validate_items_with_confidence(result, ratings["structure_parts"], "ratings.structure_parts");
        validate_evaluation_parts(result, ratings);
    }

    return result;
}

}  // namespace bridge_report::contracts
```

### 4.4 Wire C++ Build

- [ ] Modify `backend-cpp/CMakeLists.txt`.

Add the new source to `bridge_report_backend_core`:

```cmake
    src/contracts/AnnualInspectionContract.cpp
```

Add the new test source to `bridge_report_backend_tests`:

```cmake
    tests/test_annual_inspection_contract.cpp
```

Add repository-root compile definition after the test target is declared:

```cmake
target_compile_definitions(bridge_report_backend_tests
    PRIVATE
        BRIDGE_REPORT_REPOSITORY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
```

### 4.5 Verify C++ Contract

- [ ] Run:

```powershell
cd backend-cpp
cmake --preset vs2022-x64-debug
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug
```

Expected test summary:

```text
100% tests passed, 0 tests failed out of 15
```

- [ ] Commit:

```powershell
git add backend-cpp/CMakeLists.txt backend-cpp/include/bridge_report/contracts/AnnualInspectionContract.hpp backend-cpp/src/contracts/AnnualInspectionContract.cpp backend-cpp/tests/test_annual_inspection_contract.cpp
git commit -m "feat(cpp): validate annual inspection contract json"
```

## Step 5: Add Frontend TypeScript Types And Runtime Guard

The frontend needs structural types for the review workspace and a small guard before rendering imported JSON.

### 5.1 Write Failing Frontend Tests

- [ ] Create `frontend/src/contracts/annualInspection.test.ts`.

Use:

```typescript
import { describe, expect, it } from "vitest";

import {
  BridgeAnnualInspectionData,
  isBridgeAnnualInspectionData,
} from "./annualInspection";

const validData: BridgeAnnualInspectionData = {
  contract: {
    name: "BridgeAnnualInspectionData",
    version: "1.0",
    producer: "python-tools",
    parser_name: "word_table_importer",
    parser_version: "0.1.0",
  },
  import_context: {
    source_type: "软件导出Word",
    file_role: "当前年度检测资料",
    archived_file_system_number: "GDWJ-000001",
    import_record_system_number: "DRJL-000001",
  },
  bridge_check: {
    selected_bridge_system_number: "QL-000001",
    extracted_bridge_name: "绕阳河二号桥",
    match_status: "匹配",
    warnings: [],
  },
  inspection: {
    inspection_year: 2026,
    inspection_date: "2026-05-12",
    report_number: "Q202605001-JZ-024",
    project_name: "绕阳河二号桥定期检测",
    data_role: "当前年度",
  },
  defects: [
    {
      candidate_id: "defect_0001",
      structure_part: "上部结构",
      component_name: "主梁",
      component_alias: "1#孔主梁",
      defect_type: "裂缝",
      defect_location: "梁底",
      defect_description: "梁底存在横向裂缝",
      quantity_text: "1处",
      measurement_text: "L=0.8m，W=0.12mm",
      measurements: [
        {
          dimension_type: "长度",
          value: 0.8,
          unit: "m",
          source_text: "L=0.8m",
        },
      ],
      photo_numbers: ["2.1-1"],
      severity: null,
      remark: null,
      source_ref: {
        chapter: "第二章",
        table_title: "上部结构病害检查表",
        row_index: 5,
      },
      confidence: 0.92,
      review_status: "待确认",
      review_note: null,
      warnings: [],
    },
  ],
  photos: [],
  ratings: {
    overall: {
      total_score: 85.61,
      overall_grade: "2类",
      confidence: 0.95,
      review_status: "待确认",
    },
    structure_parts: [
      {
        structure_part: "上部结构",
        structure_score: 87.45,
        weight: 0.4,
        grade: "2",
        confidence: 0.95,
        review_status: "待确认",
      },
    ],
    evaluation_parts: [
      {
        structure_part: "上部结构",
        category_no: 1,
        evaluation_part: "上部承重构件",
        part_score: 86.62,
        score_rows: [
          {
            component_count: 1,
            component_score: 55.81,
          },
        ],
        confidence: 0.95,
        review_status: "待确认",
      },
    ],
    warnings: [],
  },
  comparison_candidates: [],
  report_text_candidates: [],
  warnings: [],
  errors: [],
};

describe("isBridgeAnnualInspectionData", () => {
  it("accepts valid annual inspection data", () => {
    expect(isBridgeAnnualInspectionData(validData)).toBe(true);
  });

  it("rejects an evaluation part with grade", () => {
    const invalid = structuredClone(validData) as unknown as {
      ratings: { evaluation_parts: Array<Record<string, unknown>> };
    };
    invalid.ratings.evaluation_parts[0].grade = "2";

    expect(isBridgeAnnualInspectionData(invalid)).toBe(false);
  });

  it("rejects confidence outside zero to one", () => {
    const invalid = structuredClone(validData);
    invalid.defects[0].confidence = 1.01;

    expect(isBridgeAnnualInspectionData(invalid)).toBe(false);
  });
});
```

- [ ] Run this test to confirm it fails because the module does not exist.

```powershell
cd frontend
npm test -- --run src/contracts/annualInspection.test.ts
```

Expected failing output includes:

```text
Failed to resolve import "./annualInspection"
```

### 5.2 Implement TypeScript Contract File

- [ ] Create `frontend/src/contracts/annualInspection.ts`.

Use:

```typescript
export type ReviewStatus = "待确认" | "已确认" | "已修改" | "已忽略";
export type ComparisonConfirmationStatus = "待确认" | "已确认" | "已修改" | "已拒绝";
export type Severity = "info" | "warning" | "error";
export type StructurePart = "全桥" | "上部结构" | "下部结构" | "桥面系" | "其他";

export interface SourceRef {
  chapter?: string | null;
  table_title?: string | null;
  table_index?: number | null;
  row_index?: number | null;
  column_name?: string | null;
  raw_row_text?: string | null;
  photo_area_caption?: string | null;
  file_role?: string | null;
  paragraph_index?: number | null;
}

export interface WarningItem {
  code: string;
  message: string;
  severity: Severity;
  target_candidate_id?: string | null;
}

export interface ContractInfo {
  name: "BridgeAnnualInspectionData";
  version: "1.0";
  generated_at?: string | null;
  producer: string;
  parser_name: string;
  parser_version: string;
}

export interface ImportContext {
  source_type: "软件导出Word" | "正式Word" | "Excel病害表" | "图片包" | "接口同步" | "JSON导入";
  file_role: "当前年度检测资料" | "历史正式报告" | "历史基线资料" | "修订资料";
  archived_file_system_number: string;
  import_record_system_number: string;
}

export interface BridgeCheck {
  selected_bridge_system_number: string;
  extracted_bridge_name?: string | null;
  match_status: "匹配" | "不匹配" | "待人工确认";
  warnings: WarningItem[];
}

export interface InspectionInfo {
  inspection_year: number;
  inspection_date?: string | null;
  report_number?: string | null;
  project_name?: string | null;
  data_role: "当前年度" | "历史基线" | "修订版";
}

export interface Measurement {
  dimension_type: string;
  value: number;
  unit: string;
  source_text: string;
}

export interface DefectCandidate {
  candidate_id: string;
  structure_part: StructurePart;
  component_name: string;
  component_alias?: string | null;
  defect_type: string;
  defect_location?: string | null;
  defect_description?: string | null;
  quantity_text?: string | null;
  measurement_text?: string | null;
  measurements: Measurement[];
  photo_numbers: string[];
  severity?: string | null;
  remark?: string | null;
  source_ref: SourceRef;
  confidence: number;
  review_status: ReviewStatus;
  review_note?: string | null;
  warnings: WarningItem[];
}

export interface ExtractedPhotoFile {
  temporary_file_name?: string | null;
  original_caption?: string | null;
  archive_relative_path?: string | null;
}

export interface PhotoCandidate {
  candidate_id: string;
  photo_number: string;
  linked_defect_candidate_id?: string | null;
  extracted_file: ExtractedPhotoFile;
  match_status: "高置信候选" | "待校对" | "已确认" | "未关联" | "已忽略";
  source_ref?: SourceRef | null;
  confidence: number;
  review_status: ReviewStatus;
  warnings: WarningItem[];
}

export interface OverallRating {
  total_score: number;
  overall_grade: string;
  source_ref?: SourceRef | null;
  confidence: number;
  review_status: ReviewStatus;
}

export interface StructurePartRating {
  structure_part: "上部结构" | "下部结构" | "桥面系";
  structure_score: number;
  weight: number;
  grade: string;
  source_ref?: SourceRef | null;
  confidence: number;
  review_status: ReviewStatus;
}

export interface EvaluationScoreRow {
  component_count: number;
  component_score: number;
}

export interface EvaluationPartRating {
  structure_part: "上部结构" | "下部结构" | "桥面系";
  category_no: number;
  evaluation_part: string;
  part_score: number;
  score_rows: EvaluationScoreRow[];
  source_ref?: SourceRef | null;
  confidence: number;
  review_status: ReviewStatus;
}

export interface Ratings {
  overall: OverallRating;
  structure_parts: StructurePartRating[];
  evaluation_parts: EvaluationPartRating[];
  warnings: WarningItem[];
}

export interface ComparisonMatchBasis {
  same_component: boolean;
  same_defect_type: boolean;
  location_similarity: number;
  measurement_change_detected: boolean;
  photo_number_related: boolean;
}

export interface ComparisonCandidate {
  candidate_id: string;
  previous_defect_observation_system_number?: string | null;
  current_defect_observation_system_number?: string | null;
  comparison_type:
    | "原病害无明显变化"
    | "原病害发展"
    | "原病害减轻"
    | "原病害修复"
    | "新增病害"
    | "原病害未见"
    | "无法判断";
  change_summary?: string | null;
  match_basis?: ComparisonMatchBasis | null;
  confidence: number;
  confirmation_status: ComparisonConfirmationStatus;
  review_note?: string | null;
  warnings: WarningItem[];
}

export interface ReportTextCandidate {
  candidate_id: string;
  section_key: string;
  section_title: string;
  text: string;
  usage: string;
  source_ref: SourceRef;
  review_status: ReviewStatus;
}

export interface BridgeAnnualInspectionData {
  contract: ContractInfo;
  import_context: ImportContext;
  bridge_check: BridgeCheck;
  inspection: InspectionInfo;
  defects: DefectCandidate[];
  photos: PhotoCandidate[];
  ratings: Ratings;
  comparison_candidates: ComparisonCandidate[];
  report_text_candidates: ReportTextCandidate[];
  warnings: WarningItem[];
  errors: WarningItem[];
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isNumberFromZeroToOne(value: unknown): value is number {
  return typeof value === "number" && Number.isFinite(value) && value >= 0 && value <= 1;
}

function hasValidConfidence(value: unknown): boolean {
  return isRecord(value) && isNumberFromZeroToOne(value.confidence);
}

export function isBridgeAnnualInspectionData(value: unknown): value is BridgeAnnualInspectionData {
  if (!isRecord(value)) {
    return false;
  }

  if (!isRecord(value.contract)) {
    return false;
  }
  if (value.contract.name !== "BridgeAnnualInspectionData" || value.contract.version !== "1.0") {
    return false;
  }

  if (
    !isRecord(value.import_context) ||
    !isRecord(value.bridge_check) ||
    !isRecord(value.inspection) ||
    !isRecord(value.ratings)
  ) {
    return false;
  }

  if (
    !Array.isArray(value.defects) ||
    !Array.isArray(value.photos) ||
    !Array.isArray(value.comparison_candidates) ||
    !Array.isArray(value.report_text_candidates) ||
    !Array.isArray(value.warnings) ||
    !Array.isArray(value.errors)
  ) {
    return false;
  }

  const ratings = value.ratings;
  if (
    !isRecord(ratings.overall) ||
    !hasValidConfidence(ratings.overall) ||
    !Array.isArray(ratings.structure_parts) ||
    !Array.isArray(ratings.evaluation_parts) ||
    !Array.isArray(ratings.warnings)
  ) {
    return false;
  }

  const confidenceItems = [
    ...value.defects,
    ...value.photos,
    ...value.comparison_candidates,
    ...ratings.structure_parts,
    ...ratings.evaluation_parts,
  ];

  if (!confidenceItems.every(hasValidConfidence)) {
    return false;
  }

  return ratings.evaluation_parts.every(
    (part) => isRecord(part) && !Object.prototype.hasOwnProperty.call(part, "grade"),
  );
}
```

### 5.3 Verify Frontend Contract

- [ ] Run:

```powershell
cd frontend
npm test -- --run src/contracts/annualInspection.test.ts
```

Expected output includes:

```text
3 passed
```

- [ ] Run all frontend tests:

```powershell
cd frontend
npm test -- --run
```

Expected output includes:

```text
4 passed
```

- [ ] Run frontend build:

```powershell
cd frontend
npm run build
```

Expected output includes:

```text
✓ built in
```

- [ ] Commit:

```powershell
git add frontend/src/contracts/annualInspection.ts frontend/src/contracts/annualInspection.test.ts
git commit -m "feat(frontend): add annual inspection contract types"
```

## Step 6: Document Module 03 Contract Assets

### 6.1 Update README

- [ ] Modify `README.md`.
- [ ] Add a short module 03 section near the existing project/module overview:

```markdown
## Module 03: Bridge Annual Inspection Data Contract

Module 03 defines the shared `BridgeAnnualInspectionData` candidate JSON used between the Python Word-import tools, C++ backend, and React review workspace.

Artifacts:

- JSON Schema: `contracts/bridge_annual_inspection_data.schema.json`
- Shared samples: `samples/contracts/`
- Python model: `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- C++ validator: `backend-cpp/include/bridge_report/contracts/AnnualInspectionContract.hpp`
- Frontend types and guard: `frontend/src/contracts/annualInspection.ts`

The contract represents candidate data stored in `import_records.parsed_result_json`. Confirmed bridge facts still live in PostgreSQL.
```

### 6.2 Verify Documentation References

- [ ] Run:

```powershell
rg -n "BridgeAnnualInspectionData|bridge_annual_inspection_data.schema.json|annualInspection" README.md contracts docs/superpowers/specs/modules/03-bridge-annual-inspection-data-contract.md
```

Expected output includes matches in:

```text
README.md
contracts/README.md
docs/superpowers/specs/modules/03-bridge-annual-inspection-data-contract.md
```

- [ ] Commit:

```powershell
git add README.md
git commit -m "docs: document module 03 contract artifacts"
```

## Step 7: Full Verification

Run these checks after all implementation commits.

### 7.1 Python

- [ ] Run:

```powershell
cd tools-python
uv run pytest -q
```

Expected output:

```text
6 passed
```

### 7.2 C++

- [ ] Run:

```powershell
cd backend-cpp
cmake --preset vs2022-x64-debug
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug
```

Expected test summary:

```text
100% tests passed, 0 tests failed out of 15
```

### 7.3 Frontend

- [ ] Run:

```powershell
cd frontend
npm test -- --run
npm run build
```

Expected test output includes:

```text
4 passed
```

Expected build output includes:

```text
✓ built in
```

### 7.4 Worktree

- [ ] Run:

```powershell
git status --short
```

Expected output after commits:

```text
?? docs/superpowers/diagrams/
?? docs/superpowers/specs/module03/
?? scripts/dev/generate-module03-diagrams.py
```

The two untracked entries above are outside this implementation plan and must remain unstaged.

## Step 8: Final Review Checklist

- [ ] `BridgeAnnualInspectionData` accepts the valid sample.
- [ ] The invalid evaluation-part `grade` sample is rejected by Python, C++ and TypeScript guard.
- [ ] Confidence outside `[0, 1]` is rejected by Python, C++ and TypeScript guard.
- [ ] `ratings.structure_parts[]` keeps `grade`.
- [ ] `ratings.evaluation_parts[]` has no `grade`.
- [ ] `comparison_candidates[]` accepts the reserved post-confirmation comparison candidate shape.
- [ ] `report_text_candidates[]` exists and defaults to an empty array in samples.
- [ ] No database migration is added.
- [ ] No Word parser code is added.
- [ ] No UI page is added.
- [ ] JSON Schema is generated from Python model and checked in.
- [ ] README points to the schema, samples and three language implementations.

## Commit Sequence

Use these commits in order:

1. `test: add module 03 contract fixtures`
2. `feat(python): add annual inspection contract model`
3. `feat(contract): add module 03 json schema`
4. `feat(cpp): validate annual inspection contract json`
5. `feat(frontend): add annual inspection contract types`
6. `docs: document module 03 contract artifacts`

After verification succeeds, push the branch with:

```powershell
git push -u origin feature/03-bridge-annual-inspection-data-contract
```

Expected output includes:

```text
branch 'feature/03-bridge-annual-inspection-data-contract' set up to track
```
