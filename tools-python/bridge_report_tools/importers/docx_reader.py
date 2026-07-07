from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

from docx import Document
from docx.document import Document as DocumentObject
from docx.oxml.ns import qn
from docx.oxml.table import CT_Tbl
from docx.oxml.text.paragraph import CT_P
from docx.table import Table
from docx.text.paragraph import Paragraph


CUSTOM_XML_TAG = qn("w:customXml")


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


def iter_block_children(parent_element, parent: DocumentObject) -> Iterator[Paragraph | Table]:
    for child in parent_element.iterchildren():
        if isinstance(child, CT_P):
            yield Paragraph(child, parent)
        elif isinstance(child, CT_Tbl):
            yield Table(child, parent)
        elif child.tag == CUSTOM_XML_TAG:
            yield from iter_block_children(child, parent)


def iter_block_items(document: DocumentObject) -> Iterator[Paragraph | Table]:
    yield from iter_block_children(document.element.body, document)


def is_chapter_text(text: str) -> bool:
    return text.startswith("第二章") or text.startswith("第四章")


def table_to_rows(table: Table) -> list[list[str]]:
    return [[clean_cell_text(cell.text) for cell in row.cells] for row in table.rows]


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
        for nested_title, nested_table in nested_tables(block):
            tables.append(
                DocxTable(
                    index=len(tables),
                    title=nested_title,
                    chapter=current_chapter,
                    rows=table_to_rows(nested_table),
                )
            )

    return DocxBlocks(paragraph_texts=paragraph_texts, tables=tables)
