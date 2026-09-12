"""改 docx 的底层手术：跨 run 替换文本、在锚点位置插入段落和表格。

与 docx_scan 分开：那一层只负责"看见什么"，这一层只负责"怎么改"，都不认识报告
业务。装配规则在 docx_builder 里。

这里所有插入都是"插到锚点段落之前，最后删掉锚点"，因为锚点可能在正文里，也可能在
某个表格单元格里（设计允许模板把锚点摆在任何正文位置）。python-docx 的
add_paragraph/add_table 只会往 body 末尾追加，所以先造好再把 XML 元素搬到位——
这样能沿用它的样式处理，不用自己拼段落 XML。
"""

from __future__ import annotations

from docx.document import Document as DocumentObject
from docx.enum.table import WD_ALIGN_VERTICAL
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.oxml.table import CT_Tbl
from docx.oxml.text.paragraph import CT_P
from docx.section import Section
from docx.shared import Emu, Length
from docx.table import Table
from docx.text.paragraph import Paragraph
from docx.text.run import Run

from bridge_report_tools.reports.docx_scan import (
    PLACEHOLDER_PATTERN,
    RunSpan,
    run_text_map,
    span_for,
    visible_runs,
)


TC_TAG = qn("w:tc")
TBL_TAG = qn("w:tbl")
P_TAG = qn("w:p")
SECT_PR_XPATH = "./w:pPr/w:sectPr"


def replace_span(runs: list[Run], span: RunSpan, text: str) -> None:
    """把 run 区间内的文字换成 text，样式沿用区间首个 run。

    Word 会把 `{{report_no}}` 拆进任意多个 run（拼写检查、语言标记、修订痕迹都会拆），
    所以不能按段落文本整体重写——那会把整段的字体、加粗和上下标全抹平。
    """
    start = runs[span.start_run]
    if span.start_run == span.end_run:
        start.text = start.text[: span.start_offset] + text + start.text[span.end_offset :]
        return
    start.text = start.text[: span.start_offset] + text
    for run in runs[span.start_run + 1 : span.end_run]:
        run.text = ""
    end = runs[span.end_run]
    end.text = end.text[span.end_offset :]


def replace_placeholders(paragraph: Paragraph, values: dict[str, str]) -> list[str]:
    """替换段落里所有已知的 {{占位符}}，返回没找到取值的占位符名。

    从后往前替换：先动后面的区间，前面区间的 run 下标和偏移都还是有效的。反过来做
    要在每次替换后重新扫描，多一次遍历不说，替换值里若恰好含 `{{` 还会死循环。
    """
    runs = visible_runs(paragraph)
    if not runs:
        return []
    text, starts = run_text_map(runs)
    if "{{" not in text:
        return []

    missing: list[str] = []
    for match in reversed(list(PLACEHOLDER_PATTERN.finditer(text))):
        name = match.group("name")
        if name not in values:
            missing.append(name)
            continue
        replace_span(runs, span_for(starts, match.start(), match.end()), values[name])
    missing.reverse()
    return missing


class BlockInserter:
    """在某个锚点段落的位置装配内容。

    用法：造完全部内容后调用 finish() 删掉锚点段落。锚点没被 finish 掉就等于
    `[[REPORT:...]]` 留在了交付文件里——最终校验（设计 §20.2）会拦，但那时已经
    白跑了一次 Word 域更新，所以这里的调用方必须自己保证配对。
    """

    def __init__(self, document: DocumentObject, anchor: Paragraph) -> None:
        self._document = document
        self.anchor = anchor
        self._anchor = anchor._p

    def paragraph(self, text: str = "", style: str | None = None) -> Paragraph:
        created = self._document.add_paragraph(text, style=style)
        self._anchor.addprevious(created._p)
        return created

    def table(self, rows: int, cols: int, style: str) -> Table:
        # 两张表格中间没有段落时，Word 会把它们合成一张——实测把 6 张表读回来只剩
        # 5 张，一个 2 列的照片表被并进了前一张。列数不同的两张表并起来就是废版面，
        # 所以相邻时先垫一个空段落隔开。
        previous = self._anchor.getprevious()
        if previous is not None and previous.tag == TBL_TAG:
            self._anchor.addprevious(OxmlElement("w:p"))
        created = self._document.add_table(rows, cols, style=style)
        self._anchor.addprevious(created._tbl)
        return created

    def finish(self) -> None:
        parent = self._anchor.getparent()
        parent.remove(self._anchor)
        # 表格单元格必须以段落结尾，否则 Word 打不开文件。锚点是单元格里最后一个
        # 段落、而我们又刚在它前面插了张表时，就会撞上这条。
        if parent.tag == TC_TAG and not parent.findall(P_TAG):
            parent.append(OxmlElement("w:p"))


def section_of(document: DocumentObject, paragraph: Paragraph) -> Section:
    """段落所属的分节。

    Word 的规矩是"段落属于它后面第一个分节符所结束的那一节"。必须按这个来找，
    不能图省事拿 document.sections[-1]——本模板的最后一节是横向的附录节，照着它
    算版心宽度会把正文里的照片表排到 29.7cm 宽。
    """
    body = document.element.body
    node = paragraph._p
    while node.getparent() is not None and node.getparent() is not body:
        node = node.getparent()

    index = 0
    for child in body.iterchildren():
        if child is node:
            break
        if child.tag == P_TAG and child.xpath(SECT_PR_XPATH):
            index += 1
    sections = document.sections
    return sections[min(index, len(sections) - 1)]


def content_width(section: Section) -> Length:
    """版心宽度：纸宽减左右页边距。"""
    return Emu(section.page_width - section.left_margin - section.right_margin)


def set_table_full_width(table: Table, total: Length) -> None:
    """表格占满版心，列宽交给 Word 按内容自动分配。

    python-docx 的 add_table 会按 document.sections[-1] 的版心去写表格网格和每个
    单元格的 tcW，而本模板最后一节是横向附录节（24.62cm）——照抄那个宽度，正文里
    每张表都会越过页边距。这里三处一起纠正：整表 100% 宽、网格按 total 均分、
    单元格宽度交给自动。均分只是起点，autofit 会按内容再调，所以不必在代码里写死
    每一列多宽（设计 §18：版式常量不散落在生成器里）。
    """
    table.autofit = True
    _set_table_width_pct(table, 5000)
    share = Emu(int(total) // len(table.columns))
    for column in table.columns:
        column.width = share
    for row in table.rows:
        for cell in row.cells:
            _set_cell_width_auto(cell)


def set_fixed_columns(table: Table, widths: list[Length]) -> None:
    """固定列宽。照片版式要求左右两栏严格等宽，不能让 Word 按内容调。"""
    table.autofit = False
    _set_table_width_dxa(table, Emu(sum(int(width) for width in widths)))
    for index, column in enumerate(table.columns):
        column.width = widths[index]
    for row in table.rows:
        for index, cell in enumerate(row.cells):
            cell.width = widths[index]


def _set_table_width_pct(table: Table, fiftieths_of_percent: int) -> None:
    element = table._tbl.tblPr.find(qn("w:tblW"))
    if element is None:
        element = OxmlElement("w:tblW")
        table._tbl.tblPr.append(element)
    element.set(qn("w:type"), "pct")
    element.set(qn("w:w"), str(fiftieths_of_percent))


def _set_table_width_dxa(table: Table, width: Length) -> None:
    element = table._tbl.tblPr.find(qn("w:tblW"))
    if element is None:
        element = OxmlElement("w:tblW")
        table._tbl.tblPr.append(element)
    element.set(qn("w:type"), "dxa")
    element.set(qn("w:w"), str(int(width.twips)))


def _set_cell_width_auto(cell) -> None:
    element = cell._tc.get_or_add_tcPr().find(qn("w:tcW"))
    if element is None:
        element = OxmlElement("w:tcW")
        cell._tc.get_or_add_tcPr().append(element)
    element.set(qn("w:type"), "auto")
    element.set(qn("w:w"), "0")


def merge_cells(table: Table, top: int, left: int, bottom: int, right: int):
    """合并一个矩形区域，返回合并后的单元格并清空其内容。

    合并后的单元格会带上每个原单元格的段落，不清掉就会多出一串空行——正式报告的
    评定表里「结构」「等级」这类列跨十几行，多出来的空行会把行高撑得没法看。
    """
    cell = table.cell(top, left)
    if (bottom, right) != (top, left):
        cell = cell.merge(table.cell(bottom, right))
    for paragraph in list(cell.paragraphs[1:]):
        paragraph._p.getparent().remove(paragraph._p)
    cell.paragraphs[0].text = ""
    cell.vertical_alignment = WD_ALIGN_VERTICAL.CENTER
    return cell


def merge_down(table: Table, column: int, start_row: int, end_row: int):
    """纵向合并一列上的若干行。"""
    return merge_cells(table, start_row, column, end_row, column)


def merge_across(table: Table, row: int, start_column: int, end_column: int):
    """横向合并一行上的若干列。"""
    return merge_cells(table, row, start_column, row, end_column)


def iter_body_blocks(document: DocumentObject) -> list[Paragraph | Table]:
    """按文档顺序产出 body 的顶层段落和表格，用于断言生成结果的相对位置。"""
    blocks: list[Paragraph | Table] = []
    for child in document.element.body.iterchildren():
        if isinstance(child, CT_P):
            blocks.append(Paragraph(child, document))
        elif isinstance(child, CT_Tbl):
            blocks.append(Table(child, document))
    return blocks
