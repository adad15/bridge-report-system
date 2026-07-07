# Liaoning Trunk Word Rules Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor module 04 Word parsing rules into a selectable rule profile system and implement the first rule profile, `辽宁国省干线`.

**Architecture:** The Python Word importer keeps one stable orchestration flow, while rule-specific decisions live under `tools-python/bridge_report_tools/importers/word_rules/`. C++ or the frontend will choose the rule profile and pass it to Python as `rule_profile`; Python does not auto-detect templates. The `辽宁国省干线` rule extracts defects only from `表2.1-1`、`表2.2-1`、`表2.3-1`, disease photos only from `照片2.1-x`、`照片2.2-x`、`照片2.3-x`, and ratings only from `表4.1-1` / `表4.1-2`.

**Tech Stack:** Python 3.11+, FastAPI, Pydantic v2, python-docx, pytest, standard-library `dataclasses`, `re`, `zipfile`, `xml.etree.ElementTree`.

## Global Constraints

- First-version Word input remains `.docx` only.
- Python tool service does not write PostgreSQL.
- Python tool service outputs module 03 `BridgeAnnualInspectionData`.
- `rule_profile` is selected by the user workflow and passed in by C++; Python does not infer it from Word text.
- First implemented rule profile is exactly `辽宁国省干线`.
- Old generalized `第二章` / `第四章` hardcoded business rules are removed as rule logic.
- `5.1.1 桥梁外观检查结论` is not a defect source.
- `照片1-x` and `图1-x` are not disease photos.
- `附录1 桥梁技术状况评定表` and rating conclusion text do not replace `表4.1-2`.
- Real user Word samples in `test-inputs/` and generated files in `test-output/` are local test data and must not be staged.

---

## File Structure

Create:

- `tools-python/bridge_report_tools/importers/word_rules/__init__.py`
- `tools-python/bridge_report_tools/importers/word_rules/common.py`
- `tools-python/bridge_report_tools/importers/word_rules/rule_set.py`
- `tools-python/bridge_report_tools/importers/word_rules/liaoning_trunk.py`
- `tools-python/bridge_report_tools/importers/docx_diagnostics.py`
- `tools-python/tests/importers/test_word_rules.py`

Modify:

- `tools-python/bridge_report_tools/importers/word_context.py`
- `tools-python/bridge_report_tools/importers/docx_reader.py`
- `tools-python/bridge_report_tools/importers/defect_tables.py`
- `tools-python/bridge_report_tools/importers/photo_extractor.py`
- `tools-python/bridge_report_tools/importers/rating_tables.py`
- `tools-python/bridge_report_tools/importers/word_importer.py`
- `tools-python/tests/importers/docx_fixtures.py`
- `tools-python/tests/importers/test_word_importer.py`
- `README.md`

---

### Task 1: Add Rule Profile Request Field and Rule Set Skeleton

**Files:**
- Modify: `tools-python/bridge_report_tools/importers/word_context.py`
- Create: `tools-python/bridge_report_tools/importers/word_rules/__init__.py`
- Create: `tools-python/bridge_report_tools/importers/word_rules/rule_set.py`
- Create: `tools-python/tests/importers/test_word_rules.py`
- Modify: `tools-python/tests/importers/test_word_importer.py`

**Interfaces:**
- Produces: `RuleProfile = Literal["辽宁国省干线"]`
- Produces: `WordImportRequest.rule_profile: RuleProfile`
- Produces: `select_rule_set(rule_profile: str) -> WordRuleSet`
- Produces: `WordRuleSet.profile: str`
- Consumes: `WordImportError(code: str, message: str)`

- [ ] **Step 1: Write failing tests for rule profile validation and rule selection**

Append to `tools-python/tests/importers/test_word_rules.py`:

```python
from __future__ import annotations

import pytest

from bridge_report_tools.importers.word_errors import WordImportError
from bridge_report_tools.importers.word_rules import select_rule_set


def test_select_rule_set_returns_liaoning_trunk_rules() -> None:
    rule_set = select_rule_set("辽宁国省干线")

    assert rule_set.profile == "辽宁国省干线"


def test_select_rule_set_rejects_unknown_profile() -> None:
    with pytest.raises(WordImportError) as exc_info:
        select_rule_set("吉林国省干线")

    assert exc_info.value.code == "unsupported_rule_profile"
    assert exc_info.value.message == "不支持的 Word 解析规则：吉林国省干线"
```

Modify `valid_request()` in `tools-python/tests/importers/test_word_importer.py` so the constructed `WordImportRequest` includes:

```python
        rule_profile="辽宁国省干线",
```

Add this assertion to `test_word_import_request_accepts_module04_current_year_context`:

```python
    assert request.rule_profile == "辽宁国省干线"
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_rules.py tests/importers/test_word_importer.py::test_word_import_request_accepts_module04_current_year_context -q
```

Expected:

```text
FAILED tests/importers/test_word_rules.py::test_select_rule_set_returns_liaoning_trunk_rules
FAILED tests/importers/test_word_importer.py::test_word_import_request_accepts_module04_current_year_context
```

- [ ] **Step 3: Implement request field and rule set skeleton**

Modify `tools-python/bridge_report_tools/importers/word_context.py`:

```python
ImportMode = Literal["新桥初始化", "已有桥年度导入"]
RuleProfile = Literal["辽宁国省干线"]
Module04SourceType = Literal["软件导出Word", "正式Word"]
Module04FileRole = Literal["当前年度检测资料", "历史基线资料"]
Module04DataRole = Literal["当前年度", "历史基线"]
```

Add the field after `temporary_photo_output_dir`:

```python
    rule_profile: RuleProfile
```

Create `tools-python/bridge_report_tools/importers/word_rules/rule_set.py`:

```python
from __future__ import annotations

from dataclasses import dataclass

from bridge_report_tools.importers.word_errors import WordImportError


@dataclass(frozen=True)
class WordRuleSet:
    profile: str


def select_rule_set(rule_profile: str) -> WordRuleSet:
    if rule_profile == "辽宁国省干线":
        return WordRuleSet(profile="辽宁国省干线")
    raise WordImportError(
        code="unsupported_rule_profile",
        message=f"不支持的 Word 解析规则：{rule_profile}",
    )
```

Create `tools-python/bridge_report_tools/importers/word_rules/__init__.py`:

```python
from bridge_report_tools.importers.word_rules.rule_set import WordRuleSet, select_rule_set

__all__ = [
    "WordRuleSet",
    "select_rule_set",
]
```

- [ ] **Step 4: Run tests to verify pass**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_rules.py tests/importers/test_word_importer.py::test_word_import_request_accepts_module04_current_year_context -q
```

Expected:

```text
3 passed
```

- [ ] **Step 5: Commit**

```powershell
git add tools-python/bridge_report_tools/importers/word_context.py tools-python/bridge_report_tools/importers/word_rules tools-python/tests/importers/test_word_rules.py tools-python/tests/importers/test_word_importer.py
git commit -m "feat: add Word rule profile selection"
```

---

### Task 2: Implement Liaoning Trunk Rule Definitions

**Files:**
- Modify: `tools-python/bridge_report_tools/importers/word_rules/rule_set.py`
- Create: `tools-python/bridge_report_tools/importers/word_rules/common.py`
- Create: `tools-python/bridge_report_tools/importers/word_rules/liaoning_trunk.py`
- Modify: `tools-python/bridge_report_tools/importers/word_rules/__init__.py`
- Modify: `tools-python/tests/importers/test_word_rules.py`

**Interfaces:**
- Produces: `DefectTableRule(table_no, title_keywords, structure_part)`
- Produces: `RatingTableRule(table_no, title_keywords, table_kind)`
- Produces: `PhotoCaption(number, raw_text, is_defect_photo)`
- Produces: `WordRuleSet.match_defect_table_title(title: str | None) -> DefectTableRule | None`
- Produces: `WordRuleSet.match_rating_table_title(title: str | None) -> RatingTableRule | None`
- Produces: `WordRuleSet.parse_photo_caption(text: str) -> PhotoCaption | None`
- Consumes: string table titles and caption paragraph text.

- [ ] **Step 1: Write failing tests for title and caption rules**

Append to `tools-python/tests/importers/test_word_rules.py`:

```python
def test_liaoning_trunk_matches_defect_table_titles() -> None:
    rule_set = select_rule_set("辽宁国省干线")

    upper = rule_set.match_defect_table_title("表2.1-1  上部结构病害检查表")
    lower = rule_set.match_defect_table_title("表2.2-1 下部结构病害检查表")
    deck = rule_set.match_defect_table_title("表2.3-1  桥面系病害检查表")

    assert upper is not None
    assert upper.structure_part == "上部结构"
    assert lower is not None
    assert lower.structure_part == "下部结构"
    assert deck is not None
    assert deck.structure_part == "桥面系"


def test_liaoning_trunk_rejects_old_generic_defect_title() -> None:
    rule_set = select_rule_set("辽宁国省干线")

    assert rule_set.match_defect_table_title("上部结构病害检查表") is None


def test_liaoning_trunk_matches_rating_table_titles() -> None:
    rule_set = select_rule_set("辽宁国省干线")

    weight = rule_set.match_rating_table_title("表4.1-1桥梁部件权重计算表")
    overall = rule_set.match_rating_table_title("表4.1-2  总体技术状况评定表")

    assert weight is not None
    assert weight.table_kind == "weight"
    assert overall is not None
    assert overall.table_kind == "overall"


def test_liaoning_trunk_does_not_use_appendix_rating_title() -> None:
    rule_set = select_rule_set("辽宁国省干线")

    assert rule_set.match_rating_table_title("附录1 桥梁技术状况评定表") is None


def test_liaoning_trunk_classifies_disease_photo_captions() -> None:
    rule_set = select_rule_set("辽宁国省干线")

    caption = rule_set.parse_photo_caption("照片2.1-1 主梁梁底裂缝")

    assert caption is not None
    assert caption.number == "2.1-1"
    assert caption.is_defect_photo is True


def test_liaoning_trunk_classifies_overview_photo_as_non_disease() -> None:
    rule_set = select_rule_set("辽宁国省干线")

    caption = rule_set.parse_photo_caption("照片1-1 辽小线绕阳河二号桥桥面正面照")

    assert caption is not None
    assert caption.number == "1-1"
    assert caption.is_defect_photo is False
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_rules.py -q
```

Expected:

```text
FAILED tests/importers/test_word_rules.py::test_liaoning_trunk_matches_defect_table_titles
FAILED tests/importers/test_word_rules.py::test_liaoning_trunk_matches_rating_table_titles
FAILED tests/importers/test_word_rules.py::test_liaoning_trunk_classifies_disease_photo_captions
```

- [ ] **Step 3: Implement common text helpers**

Create `tools-python/bridge_report_tools/importers/word_rules/common.py`:

```python
from __future__ import annotations

import re


def normalize_rule_text(text: str | None) -> str:
    if text is None:
        return ""
    return re.sub(r"\s+", "", text.replace("\u3000", " ")).strip()


def contains_all(text: str | None, keywords: tuple[str, ...]) -> bool:
    normalized = normalize_rule_text(text)
    return all(normalize_rule_text(keyword) in normalized for keyword in keywords)
```

- [ ] **Step 4: Implement Liaoning trunk rules**

Create `tools-python/bridge_report_tools/importers/word_rules/liaoning_trunk.py`:

```python
from __future__ import annotations

import re

from bridge_report_tools.importers.word_rules.rule_set import (
    DefectTableRule,
    PhotoCaption,
    RatingTableRule,
    WordRuleSet,
)


DISEASE_PHOTO_PATTERN = re.compile(r"(?:照片)?\s*(?P<number>2\.(?:1|2|3)-\d+)")
ANY_CAPTION_PATTERN = re.compile(r"(?:照片|图)\s*(?P<number>\d+(?:\.\d+)?-\d+)")


def build_liaoning_trunk_rule_set() -> WordRuleSet:
    return WordRuleSet(
        profile="辽宁国省干线",
        defect_table_rules=(
            DefectTableRule(
                table_no="表2.1-1",
                title_keywords=("表2.1-1", "上部结构", "病害检查表"),
                structure_part="上部结构",
            ),
            DefectTableRule(
                table_no="表2.2-1",
                title_keywords=("表2.2-1", "下部结构", "病害检查表"),
                structure_part="下部结构",
            ),
            DefectTableRule(
                table_no="表2.3-1",
                title_keywords=("表2.3-1", "桥面系", "病害检查表"),
                structure_part="桥面系",
            ),
        ),
        rating_table_rules=(
            RatingTableRule(
                table_no="表4.1-1",
                title_keywords=("表4.1-1", "桥梁部件权重计算表"),
                table_kind="weight",
            ),
            RatingTableRule(
                table_no="表4.1-2",
                title_keywords=("表4.1-2", "总体技术状况评定表"),
                table_kind="overall",
            ),
        ),
        disease_photo_pattern=DISEASE_PHOTO_PATTERN,
        any_caption_pattern=ANY_CAPTION_PATTERN,
    )
```

- [ ] **Step 5: Expand the rule set dataclasses and matching methods**

Replace `tools-python/bridge_report_tools/importers/word_rules/rule_set.py` with:

```python
from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Literal

from bridge_report_tools.importers.word_errors import WordImportError
from bridge_report_tools.importers.word_rules.common import contains_all


@dataclass(frozen=True)
class DefectTableRule:
    table_no: str
    title_keywords: tuple[str, ...]
    structure_part: Literal["上部结构", "下部结构", "桥面系"]


@dataclass(frozen=True)
class RatingTableRule:
    table_no: str
    title_keywords: tuple[str, ...]
    table_kind: Literal["weight", "overall"]


@dataclass(frozen=True)
class PhotoCaption:
    number: str
    raw_text: str
    is_defect_photo: bool


@dataclass(frozen=True)
class WordRuleSet:
    profile: str
    defect_table_rules: tuple[DefectTableRule, ...]
    rating_table_rules: tuple[RatingTableRule, ...]
    disease_photo_pattern: re.Pattern[str]
    any_caption_pattern: re.Pattern[str]

    def match_defect_table_title(self, title: str | None) -> DefectTableRule | None:
        for rule in self.defect_table_rules:
            if contains_all(title, rule.title_keywords):
                return rule
        return None

    def match_rating_table_title(self, title: str | None) -> RatingTableRule | None:
        for rule in self.rating_table_rules:
            if contains_all(title, rule.title_keywords):
                return rule
        return None

    def parse_photo_caption(self, text: str) -> PhotoCaption | None:
        disease_match = self.disease_photo_pattern.search(text)
        if disease_match is not None:
            return PhotoCaption(
                number=disease_match.group("number"),
                raw_text=text,
                is_defect_photo=True,
            )
        caption_match = self.any_caption_pattern.search(text)
        if caption_match is None:
            return None
        return PhotoCaption(
            number=caption_match.group("number"),
            raw_text=text,
            is_defect_photo=False,
        )


def select_rule_set(rule_profile: str) -> WordRuleSet:
    if rule_profile == "辽宁国省干线":
        from bridge_report_tools.importers.word_rules.liaoning_trunk import (
            build_liaoning_trunk_rule_set,
        )

        return build_liaoning_trunk_rule_set()
    raise WordImportError(
        code="unsupported_rule_profile",
        message=f"不支持的 Word 解析规则：{rule_profile}",
    )
```

- [ ] **Step 6: Export new rule types**

Replace `tools-python/bridge_report_tools/importers/word_rules/__init__.py` with:

```python
from bridge_report_tools.importers.word_rules.rule_set import (
    DefectTableRule,
    PhotoCaption,
    RatingTableRule,
    WordRuleSet,
    select_rule_set,
)

__all__ = [
    "DefectTableRule",
    "PhotoCaption",
    "RatingTableRule",
    "WordRuleSet",
    "select_rule_set",
]
```

- [ ] **Step 7: Run tests to verify pass**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_rules.py -q
```

Expected:

```text
8 passed
```

- [ ] **Step 8: Commit**

```powershell
git add tools-python/bridge_report_tools/importers/word_rules tools-python/tests/importers/test_word_rules.py
git commit -m "feat: add Liaoning trunk Word rules"
```

---

### Task 3: Add DOCX Reading Diagnostics and Nested Table Support

**Files:**
- Modify: `tools-python/bridge_report_tools/importers/docx_reader.py`
- Create: `tools-python/bridge_report_tools/importers/docx_diagnostics.py`
- Modify: `tools-python/tests/importers/test_word_importer.py`

**Interfaces:**
- Produces: `find_text_locations(path: Path, needles: list[str]) -> dict[str, list[str]]`
- Produces: `read_docx_blocks()` includes nested tables as individual `DocxTable` entries.
- Consumes: `DocxTable(index, title, chapter, rows)`.

- [ ] **Step 1: Write failing tests for nested table reading and text diagnostics**

Append to `tools-python/tests/importers/test_word_importer.py`:

```python
def test_read_docx_blocks_keeps_nested_tables_as_tables(tmp_path: Path) -> None:
    docx_path = tmp_path / "nested.docx"
    document_obj = Document()
    outer = document_obj.add_table(rows=1, cols=1)
    cell = outer.rows[0].cells[0]
    cell.add_paragraph("表2.1-1  上部结构病害检查表")
    nested = cell.add_table(rows=2, cols=6)
    headers = ["构件", "位置", "病害", "数量", "尺寸", "照片编号"]
    values = ["主梁", "第二跨左幅梁底", "裂缝", "1处", "L=0.8m", "2.1-1"]
    for index, header in enumerate(headers):
        nested.rows[0].cells[index].text = header
        nested.rows[1].cells[index].text = values[index]
    document_obj.save(docx_path)

    document = read_docx_blocks(docx_path)

    assert any(table.title == "表2.1-1 上部结构病害检查表" for table in document.tables)
    assert any(table.rows[0] == headers for table in document.tables)


def test_docx_diagnostics_finds_text_in_document_xml(tmp_path: Path) -> None:
    from bridge_report_tools.importers.docx_diagnostics import find_text_locations

    docx_path = tmp_path / "diagnostic.docx"
    document_obj = Document()
    document_obj.add_paragraph("表4.1-2  总体技术状况评定表")
    document_obj.save(docx_path)

    locations = find_text_locations(docx_path, ["表4.1-2"])

    assert locations["表4.1-2"] == ["word/document.xml"]
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_read_docx_blocks_keeps_nested_tables_as_tables tests/importers/test_word_importer.py::test_docx_diagnostics_finds_text_in_document_xml -q
```

Expected:

```text
FAILED tests/importers/test_word_importer.py::test_read_docx_blocks_keeps_nested_tables_as_tables
FAILED tests/importers/test_word_importer.py::test_docx_diagnostics_finds_text_in_document_xml
```

- [ ] **Step 3: Implement nested table extraction**

Modify `tools-python/bridge_report_tools/importers/docx_reader.py`:

```python
from docx.table import _Cell
```

Add below `table_to_rows`:

```python
def nested_tables(table: Table) -> Iterator[tuple[str | None, Table]]:
    for row in table.rows:
        for cell in row.cells:
            last_nonempty_paragraph: str | None = None
            for child in cell._tc.iterchildren():
                if isinstance(child, CT_P):
                    paragraph = Paragraph(child, cell)
                    text = clean_cell_text(paragraph.text)
                    if text:
                        last_nonempty_paragraph = text
                    continue
                if isinstance(child, CT_Tbl):
                    nested = Table(child, cell)
                    yield last_nonempty_paragraph, nested
                    yield from nested_tables(nested)
```

In `read_docx_blocks`, immediately after appending a top-level table, append nested tables:

```python
        for nested_title, nested_table in nested_tables(block):
            tables.append(
                DocxTable(
                    index=len(tables),
                    title=nested_title,
                    chapter=current_chapter,
                    rows=table_to_rows(nested_table),
                )
            )
```

- [ ] **Step 4: Implement diagnostic text locator**

Create `tools-python/bridge_report_tools/importers/docx_diagnostics.py`:

```python
from __future__ import annotations

import zipfile
from pathlib import Path


TEXT_XML_MEMBERS = (
    "word/document.xml",
    "word/footnotes.xml",
    "word/endnotes.xml",
)


def find_text_locations(path: Path, needles: list[str]) -> dict[str, list[str]]:
    locations = {needle: [] for needle in needles}
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        for member in TEXT_XML_MEMBERS:
            if member not in names:
                continue
            xml_text = archive.read(member).decode("utf-8", errors="ignore")
            for needle in needles:
                if needle in xml_text:
                    locations[needle].append(member)
    return locations
```

- [ ] **Step 5: Run targeted tests**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_read_docx_blocks_keeps_nested_tables_as_tables tests/importers/test_word_importer.py::test_docx_diagnostics_finds_text_in_document_xml -q
```

Expected:

```text
2 passed
```

- [ ] **Step 6: Run real sample diagnostic without staging sample files**

Run this command only when the local sample exists:

```powershell
cd tools-python
uv run python -c "from pathlib import Path; from bridge_report_tools.importers.docx_diagnostics import find_text_locations; p=Path(r'D:\vs2022 code\bridge-report-system\test-inputs\word-import\绕阳河二号桥报告b8e246e8-cd4d-4202-8d4d-49c56dd34389.docx'); print(find_text_locations(p, ['表2.1-1','表2.2-1','表2.3-1','表4.1-1','表4.1-2']))"
```

Expected if text exists in searchable Word XML:

```text
{'表2.1-1': ['word/document.xml'], '表2.2-1': ['word/document.xml'], '表2.3-1': ['word/document.xml'], '表4.1-1': ['word/document.xml'], '表4.1-2': ['word/document.xml']}
```

Expected if the tables are embedded as images or non-text objects:

```text
{'表2.1-1': [], '表2.2-1': [], '表2.3-1': [], '表4.1-1': [], '表4.1-2': []}
```

Record the output in the implementation notes for the next task. Do not commit `test-inputs/` or `test-output/`.

- [ ] **Step 7: Commit**

```powershell
git add tools-python/bridge_report_tools/importers/docx_reader.py tools-python/bridge_report_tools/importers/docx_diagnostics.py tools-python/tests/importers/test_word_importer.py
git commit -m "feat: add docx reading diagnostics"
```

---

### Task 4: Refactor Defect Table Parsing to Use Liaoning Rules

**Files:**
- Modify: `tools-python/bridge_report_tools/importers/defect_tables.py`
- Modify: `tools-python/tests/importers/docx_fixtures.py`
- Modify: `tools-python/tests/importers/test_word_importer.py`

**Interfaces:**
- Consumes: `WordRuleSet.match_defect_table_title(title)`
- Produces: `parse_defect_tables(tables: list[DocxTable], rule_set: WordRuleSet)`
- Produces: warnings for missing single Liaoning defect tables.

- [ ] **Step 1: Update fixtures to use Liaoning table titles**

Modify `add_defect_table()` in `tools-python/tests/importers/docx_fixtures.py`:

```python
def add_defect_table(document: Document) -> None:
    document.add_heading("桥梁外观检查", level=1)
    document.add_paragraph("表2.1-1  上部结构病害检查表")
    table = document.add_table(rows=2, cols=6)
    headers = ["构件", "位置", "病害", "数量", "尺寸", "照片编号"]
    values = ["主梁", "第二跨左幅梁底", "裂缝", "1处", "L=0.8m，W=0.12mm", "2.1-1"]
    for index, header in enumerate(headers):
        table.rows[0].cells[index].text = header
        table.rows[1].cells[index].text = values[index]
```

- [ ] **Step 2: Write failing parser tests using rule set**

Modify imports in `tools-python/tests/importers/test_word_importer.py`:

```python
from bridge_report_tools.importers.word_rules import select_rule_set
```

In every call to `parse_defect_tables(document.tables)`, pass the rule set:

```python
    rule_set = select_rule_set("辽宁国省干线")
    defects, warnings, errors = parse_defect_tables(document.tables, rule_set)
```

Add a focused test:

```python
def test_parse_defect_tables_uses_liaoning_table_numbers() -> None:
    rule_set = select_rule_set("辽宁国省干线")
    table = DocxTable(
        index=0,
        title="表2.2-1  下部结构病害检查表",
        chapter=None,
        rows=[
            ["构件", "位置", "病害", "数量", "尺寸", "照片编号"],
            ["桥台", "0#台左侧翼墙", "勾缝砂浆脱落", "1处", "L=1m", "2.2-1"],
        ],
    )

    defects, warnings, errors = parse_defect_tables([table], rule_set)

    assert warnings
    assert {warning.code for warning in warnings} == {"liaoning_trunk_defect_table_missing"}
    assert errors == []
    assert len(defects) == 1
    assert defects[0].structure_part == "下部结构"
    assert defects[0].photo_numbers == ["2.2-1"]
```

Update the old test `test_parse_defect_tables_ignores_non_defect_table_with_disease_title` so the non-defect table title remains rejected:

```python
    rule_set = select_rule_set("辽宁国省干线")
    defects, warnings, errors = parse_defect_tables([table], rule_set)
```

- [ ] **Step 3: Run tests to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_parse_defect_tables_extracts_defect_candidate tests/importers/test_word_importer.py::test_parse_defect_tables_uses_liaoning_table_numbers -q
```

Expected:

```text
FAILED tests/importers/test_word_importer.py::test_parse_defect_tables_extracts_defect_candidate - TypeError: parse_defect_tables() takes 1 positional argument but 2 were given
```

- [ ] **Step 4: Implement rule-based defect table matching**

Replace `is_defect_table()` in `tools-python/bridge_report_tools/importers/defect_tables.py` with:

```python
from bridge_report_tools.importers.word_rules import DefectTableRule, WordRuleSet
```

```python
def match_defect_table(table: DocxTable, rule_set: WordRuleSet) -> DefectTableRule | None:
    if not table.rows:
        return None
    rule = rule_set.match_defect_table_title(table.title)
    if rule is None:
        return None
    header = table.rows[0]
    header_text = "|".join(header)
    if "病害" not in header_text:
        return None
    if "照片" not in header_text:
        return None
    if not any(keyword in header_text for keyword in ["构件", "部件", "部位", "位置"]):
        return None
    return rule
```

Change function signature:

```python
def parse_defect_tables(
    tables: list[DocxTable],
    rule_set: WordRuleSet,
) -> tuple[list[DefectCandidate], list[WarningItem], list[WarningItem]]:
```

Inside the loop, replace the old table check:

```python
        table_rule = match_defect_table(table, rule_set)
        if table_rule is None:
            continue
        found_table_numbers.add(table_rule.table_no)
        defect_table_found = True
```

Set:

```python
        structure_part = table_rule.structure_part
```

Before the loop, add:

```python
    found_table_numbers: set[str] = set()
```

After the loop and before the all-missing error block:

```python
    if defect_table_found:
        for table_rule in rule_set.defect_table_rules:
            if table_rule.table_no in found_table_numbers:
                continue
            warnings.append(
                WarningItem(
                    code="liaoning_trunk_defect_table_missing",
                    message=f"未识别到{table_rule.table_no}{table_rule.structure_part}病害检查表，请人工确认。",
                    severity="warning",
                    target_candidate_id=None,
                )
            )
```

Update the all-missing error message:

```python
                message="未识别到辽宁国省干线表2.1-1、表2.2-1、表2.3-1病害检查表，本次导入没有生成病害候选。",
```

- [ ] **Step 5: Run defect parser tests**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_parse_defect_tables_extracts_defect_candidate tests/importers/test_word_importer.py::test_parse_defect_tables_uses_liaoning_table_numbers tests/importers/test_word_importer.py::test_parse_defect_tables_ignores_non_defect_table_with_disease_title -q
```

Expected:

```text
3 passed
```

- [ ] **Step 6: Commit**

```powershell
git add tools-python/bridge_report_tools/importers/defect_tables.py tools-python/tests/importers/docx_fixtures.py tools-python/tests/importers/test_word_importer.py
git commit -m "feat: parse defects with Liaoning trunk rules"
```

---

### Task 5: Refactor Photo Extraction to Use Liaoning Disease Photo Rules

**Files:**
- Modify: `tools-python/bridge_report_tools/importers/photo_extractor.py`
- Modify: `tools-python/tests/importers/test_word_importer.py`

**Interfaces:**
- Consumes: `WordRuleSet.parse_photo_caption(text) -> PhotoCaption | None`
- Produces: `find_photo_captions(document: DocxBlocks, rule_set: WordRuleSet) -> list[PhotoCaption]`
- Produces: `extract_and_match_photos(docx_path, document, defects, output_dir, rule_set)`.

- [ ] **Step 1: Write failing tests for disease-photo filtering**

Modify `test_find_photo_captions_ignores_dates_without_caption_prefix`:

```python
def test_find_photo_captions_ignores_dates_without_caption_prefix() -> None:
    rule_set = select_rule_set("辽宁国省干线")
    document = DocxBlocks(
        paragraph_texts=[
            "检测日期 2026-05-18",
            "照片 2026-05-18",
            "照片1-1 桥梁正面照",
            "照片2.1-1 主梁裂缝",
        ],
        tables=[],
    )

    captions = find_photo_captions(document, rule_set)

    assert [(caption.number, caption.is_defect_photo) for caption in captions] == [
        ("1-1", False),
        ("2.1-1", True),
    ]
```

Add:

```python
def test_extract_and_match_photos_skips_overview_photos_in_candidates(tmp_path: Path) -> None:
    rule_set = select_rule_set("辽宁国省干线")
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = tmp_path / "sample.docx"
    document_obj = Document()
    add_defect_table(document_obj)
    add_photo(document_obj, image_path, "照片1-1 桥梁正面照")
    add_photo(document_obj, image_path, "照片2.1-1 主梁梁底裂缝")
    add_rating_table(document_obj)
    document_obj.save(docx_path)
    document = read_docx_blocks(docx_path)
    defects, _, _ = parse_defect_tables(document.tables, rule_set)

    photos, temporary_files, warnings = extract_and_match_photos(
        docx_path,
        document,
        defects,
        tmp_path / "out",
        rule_set,
    )

    assert warnings == []
    assert temporary_files == ["photo_0001.png", "photo_0002.png"]
    assert len(photos) == 1
    assert photos[0].photo_number == "2.1-1"
    assert photos[0].extracted_file.temporary_file_name == "photo_0002.png"
```

Update all existing calls to `find_photo_captions` and `extract_and_match_photos` in tests to pass `rule_set`.

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_find_photo_captions_ignores_dates_without_caption_prefix tests/importers/test_word_importer.py::test_extract_and_match_photos_skips_overview_photos_in_candidates -q
```

Expected:

```text
FAILED tests/importers/test_word_importer.py::test_find_photo_captions_ignores_dates_without_caption_prefix - TypeError
```

- [ ] **Step 3: Implement rule-based caption extraction**

Modify imports in `tools-python/bridge_report_tools/importers/photo_extractor.py`:

```python
from bridge_report_tools.importers.word_rules import PhotoCaption, WordRuleSet
```

Replace `find_photo_captions`:

```python
def find_photo_captions(document: DocxBlocks, rule_set: WordRuleSet) -> list[PhotoCaption]:
    captions: list[PhotoCaption] = []
    for text in document.paragraph_texts:
        caption = rule_set.parse_photo_caption(text)
        if caption is not None:
            captions.append(caption)
    return captions
```

Update signature:

```python
def extract_and_match_photos(
    docx_path: Path,
    document: DocxBlocks,
    defects: list[DefectCandidate],
    output_dir: Path,
    rule_set: WordRuleSet,
) -> tuple[list[PhotoCandidate], list[str], list[WarningItem]]:
```

Inside `extract_and_match_photos`, replace caption variables:

```python
    captions = find_photo_captions(document, rule_set)
```

Replace the loop body start:

```python
    for index, temporary_file in enumerate(temporary_files, start=1):
        caption: PhotoCaption | None = captions[index - 1] if index <= len(captions) else None
        if caption is None or not caption.is_defect_photo:
            continue

        caption_number = caption.number
        caption_text = caption.raw_text
```

Keep candidate construction using `caption_number` and `caption_text`.

- [ ] **Step 4: Run photo tests**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_find_photo_captions_ignores_dates_without_caption_prefix tests/importers/test_word_importer.py::test_extract_and_match_photos_links_caption_to_defect tests/importers/test_word_importer.py::test_extract_and_match_photos_skips_overview_photos_in_candidates tests/importers/test_word_importer.py::test_extract_and_match_photos_keeps_unreferenced_photo_warning tests/importers/test_word_importer.py::test_extract_and_match_photos_unmatched_defect_warning_is_idempotent -q
```

Expected:

```text
5 passed
```

- [ ] **Step 5: Commit**

```powershell
git add tools-python/bridge_report_tools/importers/photo_extractor.py tools-python/tests/importers/test_word_importer.py
git commit -m "feat: filter photos with Liaoning trunk rules"
```

---

### Task 6: Refactor Rating Parsing to Use Liaoning Tables 4.1-1 and 4.1-2

**Files:**
- Modify: `tools-python/bridge_report_tools/importers/rating_tables.py`
- Modify: `tools-python/tests/importers/docx_fixtures.py`
- Modify: `tools-python/tests/importers/test_word_importer.py`

**Interfaces:**
- Consumes: `WordRuleSet.match_rating_table_title(title)`
- Produces: `parse_rating_tables(tables: list[DocxTable], rule_set: WordRuleSet)`
- Produces: warning `liaoning_trunk_rating_weight_table_missing` when `表4.1-1` is absent.

- [ ] **Step 1: Update rating fixture title**

Modify `add_rating_table()` in `tools-python/tests/importers/docx_fixtures.py`:

```python
def add_rating_table(document: Document) -> None:
    document.add_heading("全桥技术状况综合评定", level=1)
    document.add_paragraph("表4.1-2  总体技术状况评定表")
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
```

Add helper:

```python
def add_weight_table(document: Document) -> None:
    document.add_paragraph("表4.1-1 桥梁部件权重计算表")
    table = document.add_table(rows=2, cols=3)
    rows = [
        ["结构部位", "评价部件", "权重"],
        ["上部结构", "上部承重构件", "0.70"],
    ]
    for row_index, row in enumerate(rows):
        for cell_index, value in enumerate(row):
            table.rows[row_index].cells[cell_index].text = value
```

- [ ] **Step 2: Update rating tests to pass rule set and add Liaoning-specific cases**

In every call to `parse_rating_tables(document.tables)`, pass `rule_set`.

In `test_parse_rating_tables_extracts_overall_structure_and_evaluation_parts`, replace:

```python
    assert warnings == []
```

with:

```python
    assert [warning.code for warning in warnings] == ["liaoning_trunk_rating_weight_table_missing"]
```

Add:

```python
def test_parse_rating_tables_warns_when_liaoning_weight_table_missing(tmp_path: Path) -> None:
    rule_set = select_rule_set("辽宁国省干线")
    image_path = tmp_path / "photo.png"
    write_png(image_path)
    docx_path = create_sample_docx(tmp_path / "sample.docx", image_path)
    document = read_docx_blocks(docx_path)

    ratings, warnings = parse_rating_tables(document.tables, rule_set)

    assert ratings.overall.total_score == 85.61
    assert [warning.code for warning in warnings] == ["liaoning_trunk_rating_weight_table_missing"]


def test_parse_rating_tables_does_not_use_appendix_rating_table() -> None:
    rule_set = select_rule_set("辽宁国省干线")
    table = DocxTable(
        index=0,
        title="附录1 桥梁技术状况评定表",
        chapter=None,
        rows=[
            ["桥梁总体技术状况评分Dr", "85.61", "总体技术状况等级", "2类"],
        ],
    )

    with pytest.raises(WordImportError) as exc_info:
        parse_rating_tables([table], rule_set)

    assert exc_info.value.code == "rating_table_not_found"
```

- [ ] **Step 3: Run tests to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_parse_rating_tables_extracts_overall_structure_and_evaluation_parts tests/importers/test_word_importer.py::test_parse_rating_tables_warns_when_liaoning_weight_table_missing tests/importers/test_word_importer.py::test_parse_rating_tables_does_not_use_appendix_rating_table -q
```

Expected:

```text
FAILED tests/importers/test_word_importer.py::test_parse_rating_tables_extracts_overall_structure_and_evaluation_parts - TypeError: parse_rating_tables() takes 1 positional argument but 2 were given
```

- [ ] **Step 4: Implement rule-based rating table matching**

Modify imports in `tools-python/bridge_report_tools/importers/rating_tables.py`:

```python
from bridge_report_tools.importers.word_rules import WordRuleSet
```

Replace `is_rating_table`:

```python
def is_overall_rating_table(table: DocxTable, rule_set: WordRuleSet) -> bool:
    table_rule = rule_set.match_rating_table_title(table.title)
    if table_rule is None or table_rule.table_kind != "overall":
        return False

    header = table.rows[0] if table.rows else []
    return (
        has_header(header, "层级")
        and has_header(header, "结构部位")
        and has_header(header, "类别编号")
        and has_header(header, "评价部件")
        and has_standalone_score_header(header)
        and has_header(header, "权重")
        and has_header(header, "等级")
        and has_header(header, "构件评分")
    )
```

Add:

```python
def has_weight_table(tables: list[DocxTable], rule_set: WordRuleSet) -> bool:
    return any(
        (table_rule := rule_set.match_rating_table_title(table.title)) is not None
        and table_rule.table_kind == "weight"
        for table in tables
    )
```

Change signature:

```python
def parse_rating_tables(tables: list[DocxTable], rule_set: WordRuleSet) -> tuple[Ratings, list[WarningItem]]:
```

At the start:

```python
    warnings: list[WarningItem] = []
    if not has_weight_table(tables, rule_set):
        warnings.append(
            WarningItem(
                code="liaoning_trunk_rating_weight_table_missing",
                message="未识别到表4.1-1桥梁部件权重计算表，请人工确认评分权重。",
                severity="warning",
                target_candidate_id=None,
            )
        )
```

Replace table predicate:

```python
        if not table.rows or not is_overall_rating_table(table, rule_set):
            continue
```

Update final error message:

```python
        message="未识别到辽宁国省干线表4.1-2总体技术状况评定表。",
```

- [ ] **Step 5: Run rating tests**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_parse_rating_tables_extracts_overall_structure_and_evaluation_parts tests/importers/test_word_importer.py::test_parse_rating_tables_warns_when_liaoning_weight_table_missing tests/importers/test_word_importer.py::test_parse_rating_tables_does_not_use_appendix_rating_table tests/importers/test_word_importer.py::test_parse_rating_tables_fails_when_fourth_chapter_table_missing -q
```

Expected:

```text
4 passed
```

- [ ] **Step 6: Commit**

```powershell
git add tools-python/bridge_report_tools/importers/rating_tables.py tools-python/tests/importers/docx_fixtures.py tools-python/tests/importers/test_word_importer.py
git commit -m "feat: parse ratings with Liaoning trunk rules"
```

---

### Task 7: Wire Rule Set Through Main Word Import Flow and API Tests

**Files:**
- Modify: `tools-python/bridge_report_tools/importers/word_importer.py`
- Modify: `tools-python/tests/importers/test_word_importer.py`

**Interfaces:**
- Consumes: `WordImportRequest.rule_profile`
- Consumes: `select_rule_set(request.rule_profile)`
- Produces: endpoint JSON requires `"rule_profile": "辽宁国省干线"`.

- [ ] **Step 1: Write failing main-flow and endpoint assertions**

In `test_parse_word_import_outputs_contract_data_and_photo_files`, add:

```python
    assert request.rule_profile == "辽宁国省干线"
```

In `test_parse_word_endpoint_returns_contract_data`, after `payload = response.json()`, add:

```python
    assert payload["data"]["contract"]["parser_name"] == "word_importer"
```

All endpoint requests already use `request.model_dump(mode="json")`, so adding `rule_profile` to `valid_request` is sufficient.

- [ ] **Step 2: Run main-flow tests to verify failure**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_parse_word_import_outputs_contract_data_and_photo_files tests/importers/test_word_importer.py::test_parse_word_endpoint_returns_contract_data -q
```

Expected before wiring:

```text
FAILED tests/importers/test_word_importer.py::test_parse_word_import_outputs_contract_data_and_photo_files - TypeError
```

- [ ] **Step 3: Pass rule set through the importer**

Modify imports in `tools-python/bridge_report_tools/importers/word_importer.py`:

```python
from bridge_report_tools.importers.word_rules import select_rule_set
```

In `parse_word_import`, after path validation and before reading/parsing:

```python
    rule_set = select_rule_set(request.rule_profile)
```

Replace parser calls:

```python
    defects, defect_warnings, defect_errors = parse_defect_tables(document.tables, rule_set)
    ratings, rating_warnings = parse_rating_tables(document.tables, rule_set)
    photos, temporary_photo_files, photo_warnings = extract_and_match_photos(
        request.docx_path,
        document,
        defects,
        request.temporary_photo_output_dir,
        rule_set,
    )
```

- [ ] **Step 4: Run main-flow tests**

Run:

```powershell
cd tools-python
uv run pytest tests/importers/test_word_importer.py::test_parse_word_import_outputs_contract_data_and_photo_files tests/importers/test_word_importer.py::test_parse_word_endpoint_returns_contract_data tests/importers/test_word_importer.py::test_parse_word_endpoint_maps_import_error_to_bad_request -q
```

Expected:

```text
3 passed
```

- [ ] **Step 5: Run all importer tests**

Run:

```powershell
cd tools-python
uv run pytest tests/importers -q
```

Expected:

```text
all importer tests passed
```

- [ ] **Step 6: Commit**

```powershell
git add tools-python/bridge_report_tools/importers/word_importer.py tools-python/tests/importers/test_word_importer.py
git commit -m "feat: wire Word importer rule profiles"
```

---

### Task 8: Update Documentation and Run Full Verification

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/specs/modules/04-word-importer-prototype.md`

**Interfaces:**
- Consumes: final API request format.
- Produces: documentation showing `rule_profile`.

- [ ] **Step 1: Update README module 04 request notes**

In `README.md`, under “Module 04 Word Importer Prototype”, add:

```markdown
Current rule profile support:

- `辽宁国省干线`

The parse request must include:

```json
{
  "rule_profile": "辽宁国省干线"
}
```

The rule profile is selected by the user workflow and passed by C++; Python does not auto-detect report templates.
```
```

- [ ] **Step 2: Update module 04 spec with rule-profile amendment**

Append to `docs/superpowers/specs/modules/04-word-importer-prototype.md` change log:

```markdown
| 2026-07-07 | 新增规则模块化和辽宁国省干线规则方向 | 用户确认规则由前端点选，Python 按 `rule_profile` 解析；辽宁国省干线只抽表2.1-1/2.2-1/2.3-1、照片2.x-x、表4.1-1/4.1-2 | Word 导入、规则模块 |
```

Add a short note near the request structure:

```markdown
`rule_profile` 由 C++ 根据用户选择传入。第一版支持 `辽宁国省干线`，Python 不自动判断模板类型。
```

- [ ] **Step 3: Run full Python tests**

Run:

```powershell
cd tools-python
uv run pytest -q
```

Expected:

```text
all tests passed
```

- [ ] **Step 4: Check git status excludes local samples**

Run:

```powershell
git status --short
```

Expected:

```text
 M README.md
 M docs/superpowers/specs/modules/04-word-importer-prototype.md
 M tools-python/bridge_report_tools/importers/word_importer.py
 M tools-python/bridge_report_tools/importers/defect_tables.py
 M tools-python/bridge_report_tools/importers/photo_extractor.py
 M tools-python/bridge_report_tools/importers/rating_tables.py
?? test-inputs/
?? test-output/
```

Confirm `test-inputs/` and `test-output/` are not staged.

- [ ] **Step 5: Commit docs and final verification changes**

```powershell
git add README.md docs/superpowers/specs/modules/04-word-importer-prototype.md
git commit -m "docs: document Liaoning trunk rule profile"
```

- [ ] **Step 6: Final branch verification**

Run:

```powershell
cd tools-python
uv run pytest -q
```

Expected:

```text
all tests passed
```

Run:

```powershell
git status --short
```

Expected:

```text
?? test-inputs/
?? test-output/
```

Only local sample directories remain untracked.

---

## Self-Review

Spec coverage:

- User-selected rule profile is covered by Tasks 1 and 7.
- `辽宁国省干线` rule module is covered by Task 2.
- Defect sources `表2.1-1`、`表2.2-1`、`表2.3-1` are covered by Task 4.
- Disease photo rules `照片2.1-x`、`照片2.2-x`、`照片2.3-x` and exclusion of `照片1-x` are covered by Task 5.
- Rating sources `表4.1-1` and `表4.1-2` are covered by Task 6.
- Rejection of `附录1` /正文评分 as replacement is covered by Task 6.
- Reading-layer diagnostics are covered by Task 3.
- Documentation updates are covered by Task 8.

Placeholder scan:

- The plan contains no `TBD`, `TODO`, or unassigned implementation placeholders.
- Every code-changing task includes concrete snippets and a verification command.

Type consistency:

- `select_rule_set(rule_profile: str) -> WordRuleSet` is introduced in Task 1 and expanded in Task 2.
- Parser signatures consistently use `rule_set: WordRuleSet` from Tasks 4 through 7.
- Photo caption handling consistently uses `PhotoCaption`.
