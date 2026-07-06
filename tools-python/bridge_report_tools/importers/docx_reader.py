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
