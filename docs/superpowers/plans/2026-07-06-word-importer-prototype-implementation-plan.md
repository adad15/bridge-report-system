# Word Importer Prototype Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Python `.docx` importer prototype that extracts second-chapter defect tables, defect photos, and fourth-chapter ratings into the existing `BridgeAnnualInspectionData` contract.

**Architecture:** The Python FastAPI service exposes one thin `/imports/word/parse` endpoint. Parsing lives in focused importer modules: request context validation, `.docx` document reading, defect table parsing, photo extraction and matching, rating table parsing, measurement parsing, and final contract assembly. C++ remains responsible for upload, archive records, database writes, and revision/version decisions.

**Tech Stack:** Python 3.11, FastAPI, Pydantic v2, python-docx, pytest, httpx, standard-library `zipfile`, `pathlib`, `re`, and module 03 `BridgeAnnualInspectionData` Pydantic models.

---

## Scope Boundaries

- [ ] Implement only `.docx`; reject `.doc`, PDF, images, and unsupported paths.
- [ ] Implement two import modes: `新桥初始化` and `已有桥年度导入`.
- [ ] Support `source_type` values used by module 04: `正式Word` and `软件导出Word`.
- [ ] Support `data_role` values produced by module 04: `历史基线` and `当前年度`.
- [ ] Do not output `data_role = 修订版`; revision handling remains C++/database work.
- [ ] Do not parse formal report body text into `report_text_candidates`; keep it `[]`.
- [ ] Do not generate `comparison_candidates`; keep it `[]`.
- [ ] Do not write PostgreSQL or archive metadata.
- [ ] Extract images to the C++-provided temporary directory only; keep `archive_relative_path = null`.

## File Structure

Create these files:

- `tools-python/bridge_report_tools/importers/__init__.py`
- `tools-python/bridge_report_tools/importers/word_context.py`
- `tools-python/bridge_report_tools/importers/word_errors.py`
- `tools-python/bridge_report_tools/importers/measurements.py`
- `tools-python/bridge_report_tools/importers/docx_reader.py`
- `tools-python/bridge_report_tools/importers/defect_tables.py`
- `tools-python/bridge_report_tools/importers/photo_extractor.py`
- `tools-python/bridge_report_tools/importers/rating_tables.py`
- `tools-python/bridge_report_tools/importers/word_importer.py`
- `tools-python/tests/importers/__init__.py`
- `tools-python/tests/importers/docx_fixtures.py`
- `tools-python/tests/importers/test_measurements.py`
- `tools-python/tests/importers/test_word_importer.py`

Modify these files:

- `tools-python/pyproject.toml`
- `tools-python/uv.lock`
- `tools-python/bridge_report_tools/main.py`
- `README.md`

## Task 1: Add Word Import Dependency And Request Models

**Files:**
- Modify: `tools-python/pyproject.toml`
- Modify: `tools-python/uv.lock`
- Create: `tools-python/bridge_report_tools/importers/__init__.py`
- Create: `tools-python/bridge_report_tools/importers/word_errors.py`
- Create: `tools-python/bridge_report_tools/importers/word_context.py`
- Test: `tools-python/tests/importers/test_word_importer.py`

- [ ] **Step 1: Add request-model tests**

Create `tools-python/tests/importers/__init__.py` as an empty package marker.

Create `tools-python/tests/importers/test_word_importer.py` with this initial content:

```python
from pathlib import Path

import pytest
from pydantic import ValidationError

from bridge_report_tools.contracts.annual_inspection import DataRole, FileRole, SourceType
from bridge_report_tools.importers.word_context import ImportMode, WordImportRequest


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
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py -q
```

Expected: FAIL with import error for `bridge_report_tools.importers`.

- [ ] **Step 3: Add dependency**

Modify `tools-python/pyproject.toml` dependencies to include `python-docx`:

```toml
dependencies = [
  "fastapi>=0.115.0",
  "pydantic>=2.8.0",
  "python-docx>=1.1.2",
  "uvicorn[standard]>=0.30.0"
]
```

Run:

```powershell
cd tools-python
uv lock
```

Expected: `uv.lock` updates and includes `python-docx`.

- [ ] **Step 4: Add importer package**

Create `tools-python/bridge_report_tools/importers/__init__.py`:

```python
from bridge_report_tools.importers.word_context import WordImportRequest, WordImportResponse
from bridge_report_tools.importers.word_importer import parse_word_import

__all__ = [
    "WordImportRequest",
    "WordImportResponse",
    "parse_word_import",
]
```

Create `tools-python/bridge_report_tools/importers/word_errors.py`:

```python
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class WordImportError(Exception):
    code: str
    message: str

    def __str__(self) -> str:
        return f"{self.code}: {self.message}"
```

Create `tools-python/bridge_report_tools/importers/word_context.py`:

```python
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
```

Create a temporary stub `tools-python/bridge_report_tools/importers/word_importer.py` so imports resolve until later tasks replace it:

```python
from __future__ import annotations

from bridge_report_tools.importers.word_context import WordImportRequest, WordImportResponse
from bridge_report_tools.importers.word_errors import WordImportError


def parse_word_import(request: WordImportRequest) -> WordImportResponse:
    raise WordImportError(
        code="word_importer_not_implemented",
        message="Word importer orchestration has not been implemented yet.",
    )
```

- [ ] **Step 5: Run tests to verify pass**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py -q
```

Expected: `5 passed`.

- [ ] **Step 6: Commit**

Run:

```powershell
git add tools-python/pyproject.toml tools-python/uv.lock tools-python/bridge_report_tools/importers/__init__.py tools-python/bridge_report_tools/importers/word_context.py tools-python/bridge_report_tools/importers/word_errors.py tools-python/bridge_report_tools/importers/word_importer.py tools-python/tests/importers/__init__.py tools-python/tests/importers/test_word_importer.py
git commit -m "feat(python): add word import request models"
```

## Task 2: Add Measurement Parsing

**Files:**
- Create: `tools-python/bridge_report_tools/importers/measurements.py`
- Modify: `tools-python/tests/importers/test_measurements.py`

- [ ] **Step 1: Write failing measurement tests**

Create `tools-python/tests/importers/test_measurements.py`:

```python
from bridge_report_tools.importers.measurements import parse_measurements


def test_parse_length_and_width_measurements() -> None:
    measurements, warnings = parse_measurements("L=0.8m，W=0.12mm", "defect_0001")

    assert warnings == []
    assert [item.dimension_type for item in measurements] == ["长度", "宽度"]
    assert measurements[0].value == 0.8
    assert measurements[0].unit == "m"
    assert measurements[0].source_text == "L=0.8m"
    assert measurements[1].value == 0.12
    assert measurements[1].unit == "mm"
    assert measurements[1].source_text == "W=0.12mm"


def test_parse_area_spacing_and_count_measurements() -> None:
    measurements, warnings = parse_measurements("S=0.3m2，D=0.15m，3处", "defect_0002")

    assert warnings == []
    assert [item.dimension_type for item in measurements] == ["面积", "间距", "数量"]
    assert measurements[0].unit == "m2"
    assert measurements[1].unit == "m"
    assert measurements[2].unit == "处"
    assert measurements[2].value == 3


def test_low_confidence_measurement_keeps_warning() -> None:
    measurements, warnings = parse_measurements("局部破损，约20cm×30cm", "defect_0003")

    assert measurements == []
    assert warnings == [
        {
            "code": "measurement_parse_low_confidence",
            "message": "尺寸表达未能稳定结构化，请人工确认。",
            "severity": "warning",
            "target_candidate_id": "defect_0003",
        }
    ]
```

- [ ] **Step 2: Run test to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_measurements.py -q
```

Expected: FAIL with missing module or missing `parse_measurements`.

- [ ] **Step 3: Implement measurement parser**

Create `tools-python/bridge_report_tools/importers/measurements.py`:

```python
from __future__ import annotations

import re

from bridge_report_tools.contracts.annual_inspection import Measurement, WarningItem


DIMENSION_LABELS = {
    "L": "长度",
    "W": "宽度",
    "S": "面积",
    "A": "面积",
    "D": "间距",
}

DIMENSION_PATTERN = re.compile(
    r"(?P<label>[LWSAD])\s*[=:：]\s*(?P<value>\d+(?:\.\d+)?)\s*(?P<unit>m2|m²|㎡|mm|cm|m)",
    re.IGNORECASE,
)
COUNT_PATTERN = re.compile(r"(?P<value>\d+(?:\.\d+)?)\s*(?P<unit>处|条|个|块)")


def normalize_unit(unit: str) -> str:
    if unit in {"m²", "㎡"}:
        return "m2"
    return unit


def parse_measurements(measurement_text: str | None, candidate_id: str) -> tuple[list[Measurement], list[WarningItem]]:
    if not measurement_text:
        return [], []

    measurements: list[Measurement] = []
    for match in DIMENSION_PATTERN.finditer(measurement_text):
        label = match.group("label").upper()
        source_text = match.group(0).replace("：", "=")
        measurements.append(
            Measurement(
                dimension_type=DIMENSION_LABELS[label],
                value=float(match.group("value")),
                unit=normalize_unit(match.group("unit")),
                source_text=source_text,
            )
        )

    for match in COUNT_PATTERN.finditer(measurement_text):
        source_text = match.group(0)
        measurements.append(
            Measurement(
                dimension_type="数量",
                value=float(match.group("value")),
                unit=match.group("unit"),
                source_text=source_text,
            )
        )

    if measurements:
        return measurements, []

    return [], [
        WarningItem(
            code="measurement_parse_low_confidence",
            message="尺寸表达未能稳定结构化，请人工确认。",
            severity="warning",
            target_candidate_id=candidate_id,
        )
    ]
```

- [ ] **Step 4: Run tests to verify pass**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_measurements.py -q
```

Expected: `3 passed`.

- [ ] **Step 5: Commit**

Run:

```powershell
git add tools-python/bridge_report_tools/importers/measurements.py tools-python/tests/importers/test_measurements.py
git commit -m "feat(python): parse defect measurement text"
```

## Task 3: Add Dynamic `.docx` Test Fixtures

**Files:**
- Create: `tools-python/tests/importers/docx_fixtures.py`
- Modify: `tools-python/tests/importers/test_word_importer.py`

- [ ] **Step 1: Add fixture helper**

Create `tools-python/tests/importers/docx_fixtures.py`:

```python
from __future__ import annotations

import base64
from pathlib import Path

from docx import Document
from docx.shared import Inches


PNG_1X1 = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/p9sAAAAASUVORK5CYII="
)


def write_png(path: Path) -> None:
    path.write_bytes(PNG_1X1)


def add_defect_table(document: Document) -> None:
    document.add_heading("第二章 结构病害检查", level=1)
    document.add_paragraph("上部结构病害检查表")
    table = document.add_table(rows=2, cols=6)
    headers = ["构件", "位置", "病害", "数量", "尺寸", "照片编号"]
    values = ["主梁", "第二跨左幅梁底", "裂缝", "1处", "L=0.8m，W=0.12mm", "2.1-1"]
    for index, header in enumerate(headers):
        table.rows[0].cells[index].text = header
        table.rows[1].cells[index].text = values[index]


def add_photo(document: Document, image_path: Path, caption: str = "照片2.1-1 主梁梁底裂缝") -> None:
    document.add_picture(str(image_path), width=Inches(1))
    document.add_paragraph(caption)


def add_rating_table(document: Document) -> None:
    document.add_heading("第四章 全桥技术状况综合评定", level=1)
    document.add_paragraph("总体技术状况评定表")
    table = document.add_table(rows=6, cols=8)
    headers = ["层级", "结构部位", "类别编号", "评价部件", "评分", "权重", "等级", "构件评分"]
    rows = [
        ["全桥", "全桥", "", "全桥", "85.61", "", "2类", ""],
        ["结构分部", "上部结构", "", "上部结构", "87.45", "0.4", "2", ""],
        ["结构分部", "下部结构", "", "下部结构", "86.61", "0.4", "2", ""],
        ["结构分部", "桥面系", "", "桥面系", "79.93", "0.2", "3", ""],
        ["评价部件", "上部结构", "1", "上部承重构件", "86.62", "", "", "3:86.62"],
    ]
    for index, header in enumerate(headers):
        table.rows[0].cells[index].text = header
    for row_index, row in enumerate(rows, start=1):
        for cell_index, value in enumerate(row):
            table.rows[row_index].cells[cell_index].text = value


def create_sample_docx(path: Path, image_path: Path | None = None) -> Path:
    document = Document()
    document.add_paragraph("绕阳河二号桥 定期检测报告")
    add_defect_table(document)
    if image_path is not None:
        add_photo(document, image_path)
    add_rating_table(document)
    document.save(path)
    return path


def create_docx_without_rating_table(path: Path, image_path: Path | None = None) -> Path:
    document = Document()
    document.add_paragraph("绕阳河二号桥 定期检测报告")
    add_defect_table(document)
    if image_path is not None:
        add_photo(document, image_path)
    document.save(path)
    return path
```

- [ ] **Step 2: Add smoke test for generated fixture**

Append to `tools-python/tests/importers/test_word_importer.py`:

```python
from docx import Document

from tests.importers.docx_fixtures import create_sample_docx, write_png


def test_dynamic_docx_fixture_contains_expected_tables(tmp_path: Path) -> None:
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = create_sample_docx(tmp_path / "sample.docx", image_path)

    document = Document(str(docx_path))

    assert len(document.tables) == 2
    assert document.tables[0].rows[0].cells[0].text == "构件"
    assert document.tables[1].rows[0].cells[0].text == "层级"
```

- [ ] **Step 3: Run test to verify pass**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_dynamic_docx_fixture_contains_expected_tables -q
```

Expected: `1 passed`.

- [ ] **Step 4: Commit**

Run:

```powershell
git add tools-python/tests/importers/docx_fixtures.py tools-python/tests/importers/test_word_importer.py
git commit -m "test(python): add dynamic docx importer fixtures"
```

## Task 4: Read `.docx` Paragraphs And Tables In Order

**Files:**
- Create: `tools-python/bridge_report_tools/importers/docx_reader.py`
- Modify: `tools-python/tests/importers/test_word_importer.py`

- [ ] **Step 1: Add failing reader test**

Append to `tools-python/tests/importers/test_word_importer.py`:

```python
from bridge_report_tools.importers.docx_reader import read_docx_blocks


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
```

- [ ] **Step 2: Run test to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_read_docx_blocks_keeps_table_titles -q
```

Expected: FAIL with missing `docx_reader`.

- [ ] **Step 3: Implement reader**

Create `tools-python/bridge_report_tools/importers/docx_reader.py`:

```python
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

from docx import Document
from docx.document import Document as DocumentObject
from docx.oxml.table import CT_Tbl
from docx.oxml.text.paragraph import CT_P
from docx.table import Table
from docx.text.paragraph import Paragraph


@dataclass(frozen=True)
class DocxTable:
    index: int
    title: str | None
    chapter: str | None
    rows: list[list[str]]


@dataclass(frozen=True)
class DocxBlocks:
    paragraph_texts: list[str]
    tables: list[DocxTable]


def clean_cell_text(text: str) -> str:
    return " ".join(text.replace("\n", " ").split())


def iter_block_items(document: DocumentObject) -> Iterator[Paragraph | Table]:
    body = document.element.body
    for child in body.iterchildren():
        if isinstance(child, CT_P):
            yield Paragraph(child, document)
        elif isinstance(child, CT_Tbl):
            yield Table(child, document)


def is_chapter_text(text: str) -> bool:
    return text.startswith("第二章") or text.startswith("第四章")


def table_to_rows(table: Table) -> list[list[str]]:
    return [[clean_cell_text(cell.text) for cell in row.cells] for row in table.rows]


def read_docx_blocks(path: Path) -> DocxBlocks:
    document = Document(str(path))
    paragraph_texts: list[str] = []
    tables: list[DocxTable] = []
    last_nonempty_paragraph: str | None = None
    current_chapter: str | None = None

    for block in iter_block_items(document):
        if isinstance(block, Paragraph):
            text = clean_cell_text(block.text)
            if not text:
                continue
            paragraph_texts.append(text)
            if is_chapter_text(text):
                current_chapter = text
            last_nonempty_paragraph = text
            continue

        rows = table_to_rows(block)
        tables.append(
            DocxTable(
                index=len(tables),
                title=last_nonempty_paragraph,
                chapter=current_chapter,
                rows=rows,
            )
        )

    return DocxBlocks(paragraph_texts=paragraph_texts, tables=tables)
```

- [ ] **Step 4: Run test to verify pass**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_read_docx_blocks_keeps_table_titles -q
```

Expected: `1 passed`.

- [ ] **Step 5: Commit**

Run:

```powershell
git add tools-python/bridge_report_tools/importers/docx_reader.py tools-python/tests/importers/test_word_importer.py
git commit -m "feat(python): read docx paragraphs and tables"
```

## Task 5: Parse Defect Tables

**Files:**
- Create: `tools-python/bridge_report_tools/importers/defect_tables.py`
- Modify: `tools-python/tests/importers/test_word_importer.py`

- [ ] **Step 1: Add failing defect-table test**

Append to `tools-python/tests/importers/test_word_importer.py`:

```python
from bridge_report_tools.importers.defect_tables import parse_defect_tables


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
```

- [ ] **Step 2: Run test to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_parse_defect_tables_extracts_defect_candidate -q
```

Expected: FAIL with missing `defect_tables`.

- [ ] **Step 3: Implement defect parser**

Create `tools-python/bridge_report_tools/importers/defect_tables.py`:

```python
from __future__ import annotations

import re

from bridge_report_tools.contracts.annual_inspection import DefectCandidate, SourceRef, WarningItem
from bridge_report_tools.importers.docx_reader import DocxTable
from bridge_report_tools.importers.measurements import parse_measurements


PHOTO_NUMBER_PATTERN = re.compile(r"(?:照片)?(?P<number>\d+(?:\.\d+)?-\d+)")


def infer_structure_part(table: DocxTable) -> str:
    text = " ".join(value or "" for value in [table.title, table.chapter])
    if "上部结构" in text:
        return "上部结构"
    if "下部结构" in text:
        return "下部结构"
    if "桥面系" in text:
        return "桥面系"
    return "其他"


def is_defect_table(table: DocxTable) -> bool:
    header = table.rows[0] if table.rows else []
    header_text = "|".join(header)
    title = table.title or ""
    return (
        "病害" in title
        or ("病害" in header_text and "照片" in header_text and ("构件" in header_text or "部位" in header_text))
    )


def header_index(headers: list[str], keywords: list[str]) -> int | None:
    for index, header in enumerate(headers):
        if any(keyword in header for keyword in keywords):
            return index
    return None


def get_cell(row: list[str], index: int | None) -> str:
    if index is None or index >= len(row):
        return ""
    return row[index].strip()


def parse_photo_numbers(text: str) -> list[str]:
    return [match.group("number") for match in PHOTO_NUMBER_PATTERN.finditer(text)]


def parse_defect_tables(tables: list[DocxTable]) -> tuple[list[DefectCandidate], list[WarningItem], list[WarningItem]]:
    defects: list[DefectCandidate] = []
    warnings: list[WarningItem] = []
    errors: list[WarningItem] = []
    defect_table_found = False

    for table in tables:
        if not table.rows or not is_defect_table(table):
            continue
        defect_table_found = True
        headers = table.rows[0]
        component_index = header_index(headers, ["构件", "部件"])
        location_index = header_index(headers, ["位置", "部位"])
        type_index = header_index(headers, ["病害"])
        quantity_index = header_index(headers, ["数量"])
        measurement_index = header_index(headers, ["尺寸"])
        photo_index = header_index(headers, ["照片"])
        structure_part = infer_structure_part(table)

        for row_index, row in enumerate(table.rows[1:], start=1):
            if not any(row):
                continue
            candidate_id = f"defect_{len(defects) + 1:04d}"
            measurement_text = get_cell(row, measurement_index) or None
            measurements, measurement_warnings = parse_measurements(measurement_text, candidate_id)
            photo_numbers = parse_photo_numbers(get_cell(row, photo_index))
            row_warnings = list(measurement_warnings)
            if not photo_numbers:
                row_warnings.append(
                    WarningItem(
                        code="photo_number_missing",
                        message="病害行缺少照片编号，请人工确认。",
                        severity="warning",
                        target_candidate_id=candidate_id,
                    )
                )

            location = get_cell(row, location_index)
            defect_type = get_cell(row, type_index)
            defects.append(
                DefectCandidate(
                    candidate_id=candidate_id,
                    structure_part=structure_part,
                    component_name=get_cell(row, component_index) or "未识别构件",
                    component_alias=None,
                    defect_type=defect_type or "未识别病害",
                    defect_location=location or "未识别位置",
                    defect_description=f"{location}{defect_type}".strip() or None,
                    quantity_text=get_cell(row, quantity_index) or None,
                    measurement_text=measurement_text,
                    measurements=measurements,
                    photo_numbers=photo_numbers,
                    severity=None,
                    remark=None,
                    source_ref=SourceRef(
                        chapter=table.chapter,
                        table_title=table.title,
                        table_index=table.index,
                        row_index=row_index,
                        raw_row_text=" | ".join(row),
                    ),
                    confidence=0.92 if structure_part != "其他" else 0.75,
                    review_status="待确认",
                    review_note=None,
                    warnings=row_warnings,
                )
            )

    if not defect_table_found:
        errors.append(
            WarningItem(
                code="defect_tables_not_found",
                message="未识别到第二章结构病害检查表，本次导入没有生成病害候选。",
                severity="error",
                target_candidate_id=None,
            )
        )

    return defects, warnings, errors
```

- [ ] **Step 4: Run test to verify pass**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_parse_defect_tables_extracts_defect_candidate -q
```

Expected: `1 passed`.

- [ ] **Step 5: Commit**

Run:

```powershell
git add tools-python/bridge_report_tools/importers/defect_tables.py tools-python/tests/importers/test_word_importer.py
git commit -m "feat(python): parse word defect tables"
```

## Task 6: Extract And Match Photos

**Files:**
- Create: `tools-python/bridge_report_tools/importers/photo_extractor.py`
- Modify: `tools-python/tests/importers/test_word_importer.py`

- [ ] **Step 1: Add failing photo tests**

Append to `tools-python/tests/importers/test_word_importer.py`:

```python
from bridge_report_tools.importers.photo_extractor import extract_and_match_photos


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
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_extract_and_match_photos_links_caption_to_defect tests/importers/test_word_importer.py::test_extract_and_match_photos_keeps_unreferenced_photo_warning -q
```

Expected: FAIL with missing `photo_extractor`.

- [ ] **Step 3: Implement photo extractor**

Create `tools-python/bridge_report_tools/importers/photo_extractor.py`:

```python
from __future__ import annotations

import re
import zipfile
from pathlib import Path

from bridge_report_tools.contracts.annual_inspection import DefectCandidate, ExtractedPhotoFile, PhotoCandidate, SourceRef, WarningItem
from bridge_report_tools.importers.docx_reader import DocxBlocks


CAPTION_PATTERN = re.compile(r"(?:照片|图)?\s*(?P<number>\d+(?:\.\d+)?-\d+)")


def find_photo_captions(document: DocxBlocks) -> list[tuple[str, str]]:
    captions: list[tuple[str, str]] = []
    for text in document.paragraph_texts:
        match = CAPTION_PATTERN.search(text)
        if match:
            captions.append((match.group("number"), text))
    return captions


def media_members(docx_path: Path) -> list[str]:
    with zipfile.ZipFile(docx_path) as archive:
        return sorted(
            name
            for name in archive.namelist()
            if name.startswith("word/media/") and not name.endswith("/")
        )


def extract_media(docx_path: Path, output_dir: Path) -> list[str]:
    output_dir.mkdir(parents=True, exist_ok=True)
    written_files: list[str] = []
    members = media_members(docx_path)
    with zipfile.ZipFile(docx_path) as archive:
        for index, member in enumerate(members, start=1):
            extension = Path(member).suffix.lower() or ".bin"
            file_name = f"photo_{index:04d}{extension}"
            (output_dir / file_name).write_bytes(archive.read(member))
            written_files.append(file_name)
    return written_files


def defect_by_photo_number(defects: list[DefectCandidate]) -> dict[str, str]:
    mapping: dict[str, str] = {}
    for defect in defects:
        for photo_number in defect.photo_numbers:
            if photo_number not in mapping:
                mapping[photo_number] = defect.candidate_id
    return mapping


def extract_and_match_photos(
    docx_path: Path,
    document: DocxBlocks,
    defects: list[DefectCandidate],
    output_dir: Path,
) -> tuple[list[PhotoCandidate], list[str], list[WarningItem]]:
    temporary_files = extract_media(docx_path, output_dir)
    captions = find_photo_captions(document)
    defect_mapping = defect_by_photo_number(defects)
    photos: list[PhotoCandidate] = []

    for index, temporary_file in enumerate(temporary_files, start=1):
        caption_number: str | None = None
        caption_text: str | None = None
        if index <= len(captions):
            caption_number, caption_text = captions[index - 1]
        else:
            caption_number = f"unmatched-{index:04d}"
            caption_text = None

        candidate_id = f"photo_{index:04d}"
        linked_defect_id = defect_mapping.get(caption_number)
        warnings: list[WarningItem] = []
        match_status = "高置信候选" if linked_defect_id else "未关联"
        confidence = 0.95 if linked_defect_id else 0.6
        if not linked_defect_id:
            warnings.append(
                WarningItem(
                    code="photo_not_referenced_by_defect",
                    message=f"Word 图片区存在照片编号 {caption_number}，但病害表未引用。",
                    severity="warning",
                    target_candidate_id=candidate_id,
                )
            )

        photos.append(
            PhotoCandidate(
                candidate_id=candidate_id,
                photo_number=caption_number,
                linked_defect_candidate_id=linked_defect_id,
                extracted_file=ExtractedPhotoFile(
                    temporary_file_name=temporary_file,
                    original_caption=caption_text,
                    archive_relative_path=None,
                ),
                match_status=match_status,
                source_ref=SourceRef(
                    chapter="第二章",
                    photo_area_caption=caption_text,
                ),
                confidence=confidence,
                review_status="待确认",
                warnings=warnings,
            )
        )

    matched_numbers = {photo.photo_number for photo in photos}
    for defect in defects:
        for photo_number in defect.photo_numbers:
            if photo_number not in matched_numbers:
                defect.warnings.append(
                    WarningItem(
                        code="photo_number_unmatched",
                        message=f"病害行引用照片编号 {photo_number}，但未在 Word 图片区找到对应图片。",
                        severity="warning",
                        target_candidate_id=defect.candidate_id,
                    )
                )

    return photos, temporary_files, []
```

- [ ] **Step 4: Run tests to verify pass**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_extract_and_match_photos_links_caption_to_defect tests/importers/test_word_importer.py::test_extract_and_match_photos_keeps_unreferenced_photo_warning -q
```

Expected: `2 passed`.

- [ ] **Step 5: Commit**

Run:

```powershell
git add tools-python/bridge_report_tools/importers/photo_extractor.py tools-python/tests/importers/test_word_importer.py
git commit -m "feat(python): extract and match word photos"
```

## Task 7: Parse Fourth-Chapter Ratings

**Files:**
- Create: `tools-python/bridge_report_tools/importers/rating_tables.py`
- Modify: `tools-python/tests/importers/test_word_importer.py`

- [ ] **Step 1: Add failing rating tests**

Append to `tools-python/tests/importers/test_word_importer.py`:

```python
from bridge_report_tools.importers.rating_tables import parse_rating_tables
from bridge_report_tools.importers.word_errors import WordImportError


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
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_parse_rating_tables_extracts_overall_structure_and_evaluation_parts tests/importers/test_word_importer.py::test_parse_rating_tables_fails_when_fourth_chapter_table_missing -q
```

Expected: FAIL with missing `rating_tables`.

- [ ] **Step 3: Implement rating parser**

Create `tools-python/bridge_report_tools/importers/rating_tables.py`:

```python
from __future__ import annotations

from bridge_report_tools.contracts.annual_inspection import (
    EvaluationPartRating,
    EvaluationScoreRow,
    OverallRating,
    Ratings,
    SourceRef,
    StructurePartRating,
    WarningItem,
)
from bridge_report_tools.importers.docx_reader import DocxTable
from bridge_report_tools.importers.word_errors import WordImportError


def is_rating_table(table: DocxTable) -> bool:
    if not table.rows:
        return False
    text = " ".join([table.title or "", table.chapter or "", " ".join(table.rows[0])])
    return "评定" in text or "评分" in text or "技术状况" in text


def header_index(headers: list[str], keyword: str) -> int | None:
    for index, header in enumerate(headers):
        if keyword in header:
            return index
    return None


def cell(row: list[str], index: int | None) -> str:
    if index is None or index >= len(row):
        return ""
    return row[index].strip()


def parse_float(text: str) -> float:
    return float(text.strip())


def parse_int(text: str) -> int:
    return int(float(text.strip()))


def parse_score_rows(text: str) -> list[EvaluationScoreRow]:
    if not text:
        return []
    rows: list[EvaluationScoreRow] = []
    for item in text.split(";"):
        if ":" not in item:
            continue
        count_text, score_text = item.split(":", 1)
        rows.append(
            EvaluationScoreRow(
                component_count=parse_int(count_text),
                component_score=parse_float(score_text),
            )
        )
    return rows


def parse_rating_tables(tables: list[DocxTable]) -> tuple[Ratings, list[WarningItem]]:
    for table in tables:
        if not table.rows or not is_rating_table(table):
            continue
        headers = table.rows[0]
        level_i = header_index(headers, "层级")
        structure_i = header_index(headers, "结构部位")
        category_i = header_index(headers, "类别编号")
        part_i = header_index(headers, "评价部件")
        score_i = header_index(headers, "评分")
        weight_i = header_index(headers, "权重")
        grade_i = header_index(headers, "等级")
        component_score_i = header_index(headers, "构件评分")

        overall: OverallRating | None = None
        structure_parts: list[StructurePartRating] = []
        evaluation_parts: list[EvaluationPartRating] = []

        for row_index, row in enumerate(table.rows[1:], start=1):
            level = cell(row, level_i)
            source_ref = SourceRef(
                chapter=table.chapter,
                table_title=table.title,
                table_index=table.index,
                row_index=row_index,
                raw_row_text=" | ".join(row),
            )
            if level == "全桥":
                overall = OverallRating(
                    total_score=parse_float(cell(row, score_i)),
                    overall_grade=cell(row, grade_i),
                    source_ref=source_ref,
                    confidence=0.95,
                    review_status="待确认",
                )
            elif level == "结构分部":
                structure_parts.append(
                    StructurePartRating(
                        structure_part=cell(row, structure_i),
                        structure_score=parse_float(cell(row, score_i)),
                        weight=parse_float(cell(row, weight_i)),
                        grade=cell(row, grade_i),
                        source_ref=source_ref,
                        confidence=0.95,
                        review_status="待确认",
                    )
                )
            elif level == "评价部件":
                evaluation_parts.append(
                    EvaluationPartRating(
                        structure_part=cell(row, structure_i),
                        category_no=parse_int(cell(row, category_i)),
                        evaluation_part=cell(row, part_i),
                        part_score=parse_float(cell(row, score_i)),
                        score_rows=parse_score_rows(cell(row, component_score_i)),
                        source_ref=source_ref,
                        confidence=0.95,
                        review_status="待确认",
                    )
                )

        if overall is None:
            raise WordImportError(
                code="rating_table_not_found",
                message="未识别到第四章总体技术状况评定表。",
            )

        return Ratings(
            overall=overall,
            structure_parts=structure_parts,
            evaluation_parts=evaluation_parts,
            warnings=[],
        ), []

    raise WordImportError(
        code="rating_table_not_found",
        message="未识别到第四章总体技术状况评定表。",
    )
```

- [ ] **Step 4: Run tests to verify pass**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_parse_rating_tables_extracts_overall_structure_and_evaluation_parts tests/importers/test_word_importer.py::test_parse_rating_tables_fails_when_fourth_chapter_table_missing -q
```

Expected: `2 passed`.

- [ ] **Step 5: Commit**

Run:

```powershell
git add tools-python/bridge_report_tools/importers/rating_tables.py tools-python/tests/importers/test_word_importer.py
git commit -m "feat(python): parse word rating tables"
```

## Task 8: Assemble `BridgeAnnualInspectionData`

**Files:**
- Modify: `tools-python/bridge_report_tools/importers/word_importer.py`
- Modify: `tools-python/tests/importers/test_word_importer.py`

- [ ] **Step 1: Add failing end-to-end importer tests**

Append to `tools-python/tests/importers/test_word_importer.py`:

```python
from bridge_report_tools.importers.word_importer import parse_word_import


def test_parse_word_import_outputs_contract_data_and_photo_files(tmp_path: Path) -> None:
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = create_sample_docx(tmp_path / "sample.docx", image_path)
    request = valid_request(tmp_path).model_copy(
        update={
            "docx_path": docx_path,
            "temporary_photo_output_dir": tmp_path / "out",
        }
    )

    response = parse_word_import(request)

    assert response.temporary_photo_files == ["photo_0001.png"]
    data = response.data
    assert data.contract.name == "BridgeAnnualInspectionData"
    assert data.contract.parser_name == "word_importer"
    assert data.import_context.source_type == "软件导出Word"
    assert data.import_context.file_role == "当前年度检测资料"
    assert data.bridge_check.selected_bridge_system_number == "QL-000001"
    assert data.bridge_check.extracted_bridge_name == "绕阳河二号桥"
    assert data.bridge_check.match_status == "匹配"
    assert data.inspection.inspection_year == 2026
    assert data.inspection.report_number == "Q202605001-JZ-024"
    assert len(data.defects) == 1
    assert data.defects[0].candidate_id == "defect_0001"
    assert len(data.photos) == 1
    assert data.photos[0].linked_defect_candidate_id == "defect_0001"
    assert data.ratings.overall.total_score == 85.61
    assert data.comparison_candidates == []
    assert data.report_text_candidates == []
    assert data.errors == []


def test_parse_word_import_keeps_defect_table_missing_as_contract_error(tmp_path: Path) -> None:
    document_obj = Document()
    document_obj.add_paragraph("绕阳河二号桥 定期检测报告")
    add_rating_table(document_obj)
    docx_path = tmp_path / "sample.docx"
    document_obj.save(docx_path)
    request = valid_request(tmp_path).model_copy(
        update={
            "docx_path": docx_path,
            "temporary_photo_output_dir": tmp_path / "out",
        }
    )

    response = parse_word_import(request)

    assert response.data.defects == []
    assert response.data.photos == []
    assert response.data.errors[0].code == "defect_tables_not_found"
    assert response.data.ratings.overall.total_score == 85.61


def test_parse_word_import_fails_when_rating_table_missing(tmp_path: Path) -> None:
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = create_docx_without_rating_table(tmp_path / "sample.docx", image_path)
    request = valid_request(tmp_path).model_copy(
        update={
            "docx_path": docx_path,
            "temporary_photo_output_dir": tmp_path / "out",
        }
    )

    with pytest.raises(WordImportError) as exc_info:
        parse_word_import(request)

    assert exc_info.value.code == "rating_table_not_found"
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_parse_word_import_outputs_contract_data_and_photo_files tests/importers/test_word_importer.py::test_parse_word_import_keeps_defect_table_missing_as_contract_error tests/importers/test_word_importer.py::test_parse_word_import_fails_when_rating_table_missing -q
```

Expected: FAIL because `parse_word_import` still raises `word_importer_not_implemented`.

- [ ] **Step 3: Implement importer orchestration**

Replace `tools-python/bridge_report_tools/importers/word_importer.py` with:

```python
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
from bridge_report_tools.importers.rating_tables import parse_rating_tables
from bridge_report_tools.importers.word_context import WordImportRequest, WordImportResponse
from bridge_report_tools.importers.word_errors import WordImportError


PARSER_VERSION = "0.1.0"


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

    document = read_docx_blocks(request.docx_path)
    defects, defect_warnings, defect_errors = parse_defect_tables(document.tables)
    ratings, rating_warnings = parse_rating_tables(document.tables)
    photos, temporary_photo_files, photo_warnings = extract_and_match_photos(
        request.docx_path,
        document,
        defects,
        request.temporary_photo_output_dir,
    )

    top_level_warnings = defect_warnings + rating_warnings + photo_warnings
    top_level_errors = defect_errors

    data = BridgeAnnualInspectionData(
        contract=ContractInfo(
            name="BridgeAnnualInspectionData",
            version="1.0",
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
        ratings=ratings,
        comparison_candidates=[],
        report_text_candidates=[],
        warnings=top_level_warnings,
        errors=top_level_errors,
    )

    try:
        validated = BridgeAnnualInspectionData.model_validate(data.model_dump())
    except ValidationError as exc:
        raise WordImportError(
            code="contract_validation_failed",
            message=str(exc),
        ) from exc

    return WordImportResponse(data=validated, temporary_photo_files=temporary_photo_files)
```

- [ ] **Step 4: Run end-to-end importer tests**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_parse_word_import_outputs_contract_data_and_photo_files tests/importers/test_word_importer.py::test_parse_word_import_keeps_defect_table_missing_as_contract_error tests/importers/test_word_importer.py::test_parse_word_import_fails_when_rating_table_missing -q
```

Expected: `3 passed`.

- [ ] **Step 5: Run all importer tests**

Run:

```powershell
cd tools-python
uv run pytest tests/importers -q
```

Expected: all importer tests pass.

- [ ] **Step 6: Commit**

Run:

```powershell
git add tools-python/bridge_report_tools/importers/word_importer.py tools-python/tests/importers/test_word_importer.py
git commit -m "feat(python): assemble word import contract data"
```

## Task 9: Add FastAPI Endpoint

**Files:**
- Modify: `tools-python/bridge_report_tools/main.py`
- Modify: `tools-python/tests/test_health.py`
- Modify: `tools-python/tests/importers/test_word_importer.py`

- [ ] **Step 1: Add failing API tests**

Append to `tools-python/tests/importers/test_word_importer.py`:

```python
import asyncio

from httpx import ASGITransport, AsyncClient

from bridge_report_tools.main import app


def test_parse_word_endpoint_returns_contract_data(tmp_path: Path) -> None:
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = create_sample_docx(tmp_path / "sample.docx", image_path)
    request = valid_request(tmp_path).model_copy(
        update={
            "docx_path": docx_path,
            "temporary_photo_output_dir": tmp_path / "out",
        }
    )

    async def post_parse():
        transport = ASGITransport(app=app)
        async with AsyncClient(transport=transport, base_url="http://testserver") as client:
            return await client.post(
                "/imports/word/parse",
                json=request.model_dump(mode="json"),
            )

    response = asyncio.run(post_parse())

    assert response.status_code == 200
    payload = response.json()
    assert payload["data"]["contract"]["name"] == "BridgeAnnualInspectionData"
    assert payload["data"]["defects"][0]["candidate_id"] == "defect_0001"
    assert payload["temporary_photo_files"] == ["photo_0001.png"]


def test_parse_word_endpoint_maps_import_error_to_bad_request(tmp_path: Path) -> None:
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = create_docx_without_rating_table(tmp_path / "sample.docx", image_path)
    request = valid_request(tmp_path).model_copy(
        update={
            "docx_path": docx_path,
            "temporary_photo_output_dir": tmp_path / "out",
        }
    )

    async def post_parse():
        transport = ASGITransport(app=app)
        async with AsyncClient(transport=transport, base_url="http://testserver") as client:
            return await client.post(
                "/imports/word/parse",
                json=request.model_dump(mode="json"),
            )

    response = asyncio.run(post_parse())

    assert response.status_code == 400
    assert response.json() == {
        "detail": {
            "code": "rating_table_not_found",
            "message": "未识别到第四章总体技术状况评定表。",
        }
    }
```

- [ ] **Step 2: Run API tests to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_parse_word_endpoint_returns_contract_data tests/importers/test_word_importer.py::test_parse_word_endpoint_maps_import_error_to_bad_request -q
```

Expected: FAIL with `404 Not Found` for `/imports/word/parse`.

- [ ] **Step 3: Implement endpoint**

Modify `tools-python/bridge_report_tools/main.py`:

```python
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

from bridge_report_tools import __version__
from bridge_report_tools.config import get_settings
from bridge_report_tools.importers.word_context import WordImportRequest, WordImportResponse
from bridge_report_tools.importers.word_errors import WordImportError
from bridge_report_tools.importers.word_importer import parse_word_import


class HealthResponse(BaseModel):
    status: str
    service: str
    version: str
    host: str
    port: int


app = FastAPI(
    title="Bridge Report Python Tools",
    version=__version__,
)


@app.get("/health", response_model=HealthResponse)
def health() -> HealthResponse:
    settings = get_settings()
    return HealthResponse(
        status="ok",
        service="bridge-report-python-tools",
        version=__version__,
        host=settings.host,
        port=settings.port,
    )


@app.post("/imports/word/parse", response_model=WordImportResponse)
def parse_word(request: WordImportRequest) -> WordImportResponse:
    try:
        return parse_word_import(request)
    except WordImportError as exc:
        raise HTTPException(
            status_code=400,
            detail={
                "code": exc.code,
                "message": exc.message,
            },
        ) from exc
```

- [ ] **Step 4: Run API tests**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_parse_word_endpoint_returns_contract_data tests/importers/test_word_importer.py::test_parse_word_endpoint_maps_import_error_to_bad_request -q
```

Expected: `2 passed`.

- [ ] **Step 5: Run health test to ensure no regression**

Run:

```powershell
cd tools-python
uv run pytest tests/test_health.py -q
```

Expected: `1 passed`.

- [ ] **Step 6: Commit**

Run:

```powershell
git add tools-python/bridge_report_tools/main.py tools-python/tests/importers/test_word_importer.py
git commit -m "feat(python): expose word import parse endpoint"
```

## Task 10: Document Module 04 Runtime Usage

**Files:**
- Modify: `README.md`
- Test: `README.md`

- [ ] **Step 1: Update README**

Add this section after the Module 03 section in `README.md`:

````markdown
## Module 04 Word Importer Prototype

Module 04 adds the Python `.docx` importer prototype.

The C++ backend remains responsible for browser upload, file archive records,
import records, database writes, and revision/version decisions. The Python
tool service reads an already archived `.docx` path and writes extracted images
to a temporary directory provided by C++.

Endpoint:

```text
POST http://127.0.0.1:18081/imports/word/parse
```

The endpoint returns a module 03 `BridgeAnnualInspectionData` candidate JSON and
a list of temporary image file names. First-version extraction is limited to:

- second-chapter defect tables
- defect photos matched by photo number
- fourth-chapter condition rating tables

It does not parse formal report body text, generate comparison candidates, write
PostgreSQL, or decide same-year revision behavior.
````

- [ ] **Step 2: Verify docs references**

Run:

```powershell
rg -n "Module 04|/imports/word/parse|BridgeAnnualInspectionData|revision" README.md docs/superpowers/specs/modules/04-word-importer-prototype.md
```

Expected output includes matches in both `README.md` and `docs/superpowers/specs/modules/04-word-importer-prototype.md`.

- [ ] **Step 3: Commit**

Run:

```powershell
git add README.md
git commit -m "docs: document word importer prototype usage"
```

## Task 11: Full Verification

**Files:**
- Verify only; no file edits expected.

- [ ] **Step 1: Run all Python tests**

Run:

```powershell
cd tools-python
uv run pytest -q
```

Expected: all tests pass, including existing health and module 03 contract tests.

- [ ] **Step 2: Verify contract samples still pass**

Run:

```powershell
cd tools-python
uv run pytest tests/test_annual_inspection_contract.py -q
```

Expected: all module 03 contract tests pass.

- [ ] **Step 3: Check worktree**

Run:

```powershell
git status --short
```

Expected: no unstaged implementation files remain after commits.

- [ ] **Step 4: Inspect commit sequence**

Run:

```powershell
git log --oneline -8
```

Expected recent commits include:

```text
docs: document word importer prototype usage
feat(python): expose word import parse endpoint
feat(python): assemble word import contract data
feat(python): parse word rating tables
feat(python): extract and match word photos
feat(python): parse word defect tables
feat(python): read docx paragraphs and tables
test(python): add dynamic docx importer fixtures
```

## Self-Review Checklist

- [ ] The plan implements only `.docx`.
- [ ] The plan supports new-bridge baseline imports from `正式Word`.
- [ ] The plan supports existing-bridge annual imports from `软件导出Word`.
- [ ] The plan does not implement `data_role = 修订版`.
- [ ] The plan keeps `comparison_candidates=[]`.
- [ ] The plan keeps `report_text_candidates=[]`.
- [ ] The plan makes complete fourth-chapter rating-table absence a parse error.
- [ ] The plan allows missing second-chapter defect tables to return legal contract data with top-level `errors[]`.
- [ ] The plan writes object-level photo and measurement warnings.
- [ ] The plan validates final output through `BridgeAnnualInspectionData`.
- [ ] The plan keeps the FastAPI route thin and parser logic in focused modules.

## Commit Sequence

Use these commits in order:

1. `feat(python): add word import request models`
2. `feat(python): parse defect measurement text`
3. `test(python): add dynamic docx importer fixtures`
4. `feat(python): read docx paragraphs and tables`
5. `feat(python): parse word defect tables`
6. `feat(python): extract and match word photos`
7. `feat(python): parse word rating tables`
8. `feat(python): assemble word import contract data`
9. `feat(python): expose word import parse endpoint`
10. `docs: document word importer prototype usage`

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-06-word-importer-prototype-implementation-plan.md`. Two execution options:

**1. Subagent-Driven (recommended)** - dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** - execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
