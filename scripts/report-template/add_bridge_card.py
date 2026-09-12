"""给已发布的模板补上附录2 的卡片：三个卡片样式 + 一个内容锚点。

模板现在是手工维护的，所以这个脚本只做两处必要的加法，先备份，可重复跑：

1. 新增三个命名样式（报告卡片表头 / 报告卡片文字 / 报告卡片分段），宋体小五、
   数字 Times New Roman。两张附录卡片的格子比正文表格密得多，五号排不下。
   已存在的样式不动——模板作者手改过的字号、边距一概保留。
2. 把「附录2 桥梁基本状况卡片」标题下面的空段落改成 `[[REPORT:BRIDGE_CARD]]`，
   生成器才知道卡片画在哪儿。

用法（在仓库根目录）：
    tools-python/.venv/Scripts/python.exe scripts/report-template/add_bridge_card.py
"""

from __future__ import annotations

import shutil
import sys
from datetime import datetime
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools-python"))

from docx import Document  # noqa: E402
from docx.enum.style import WD_STYLE_TYPE  # noqa: E402
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_LINE_SPACING  # noqa: E402
from docx.oxml import OxmlElement  # noqa: E402
from docx.oxml.ns import qn  # noqa: E402
from docx.oxml.text.paragraph import CT_P  # noqa: E402
from docx.shared import Pt  # noqa: E402
from docx.text.paragraph import Paragraph  # noqa: E402

from bridge_report_tools.reports.contract import anchor_text  # noqa: E402
from bridge_report_tools.reports.styles import (  # noqa: E402
    STYLE_BODY,
    STYLE_CARD_BAND,
    STYLE_CARD_CELL,
    STYLE_CARD_HEADER,
)

TEMPLATE = (
    REPO_ROOT / "templates" / "report" / "periodic_inspection_v1"
    / "periodic-inspection-v1.docx"
)

FONT_SONG = "宋体"
FONT_ASCII = "Times New Roman"
CARD_SIZE_PT = 9  # 小五

#: 附录2 的标题。锚点插在它下面。
APPENDIX_TWO_PREFIX = "附录2"

ANCHOR = anchor_text("BRIDGE_CARD")


def _set_font(style, *, bold: bool) -> None:
    """中文字体只能落在 w:rPr/w:rFonts 的 w:eastAsia 上，python-docx 不直接支持。"""
    style.font.name = FONT_ASCII
    style.font.size = Pt(CARD_SIZE_PT)
    style.font.bold = bold
    rpr = style.element.get_or_add_rPr()
    fonts = rpr.find(qn("w:rFonts"))
    if fonts is None:
        fonts = OxmlElement("w:rFonts")
        rpr.append(fonts)
    fonts.set(qn("w:ascii"), FONT_ASCII)
    fonts.set(qn("w:hAnsi"), FONT_ASCII)
    fonts.set(qn("w:eastAsia"), FONT_SONG)


def add_card_styles(document) -> list[str]:
    """加三个卡片样式。已经有的不动，免得覆盖手改过的版式。"""
    existing = {style.name for style in document.styles}
    added: list[str] = []
    for name, bold, alignment in (
        (STYLE_CARD_HEADER, True, WD_ALIGN_PARAGRAPH.CENTER),
        (STYLE_CARD_CELL, False, WD_ALIGN_PARAGRAPH.CENTER),
        (STYLE_CARD_BAND, True, WD_ALIGN_PARAGRAPH.LEFT),
    ):
        if name in existing:
            continue
        style = document.styles.add_style(name, WD_STYLE_TYPE.PARAGRAPH)
        style.base_style = document.styles["Normal"]
        _set_font(style, bold=bold)
        style.paragraph_format.alignment = alignment
        style.paragraph_format.space_before = Pt(0)
        style.paragraph_format.space_after = Pt(0)
        style.paragraph_format.line_spacing_rule = WD_LINE_SPACING.SINGLE
        added.append(name)
    return added


def add_anchor(document) -> bool:
    """把附录2 标题后面的第一个空段落改成锚点。"""
    body = document.element.body
    children = [child for child in body.iterchildren() if isinstance(child, CT_P)]
    paragraphs = [Paragraph(child, document) for child in children]

    for index, paragraph in enumerate(paragraphs):
        text = paragraph.text.strip()
        if not (
            text.startswith(APPENDIX_TWO_PREFIX)
            and paragraph.style.name.startswith(("Heading", "标题"))
        ):
            continue
        for follower in paragraphs[index + 1:]:
            if ANCHOR in follower.text:
                return False
            if follower.text.strip():
                raise SystemExit(
                    f"「{APPENDIX_TWO_PREFIX}」标题后面不是空段落，而是"
                    f"{follower.text.strip()[:40]!r}；请人工确认后再跑。"
                )
            if follower._p.xpath("./w:pPr/w:sectPr"):
                continue  # 分节符段落不能占用，跳过看下一段。
            follower.style = document.styles[STYLE_BODY]
            follower.text = ANCHOR
            return True
        raise SystemExit(f"「{APPENDIX_TWO_PREFIX}」标题后面没有可用的空段落。")
    raise SystemExit(f"找不到「{APPENDIX_TWO_PREFIX}」的标题段落。")


def main() -> None:
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else TEMPLATE
    if not target.is_file():
        print(f"找不到模板：{target}")
        raise SystemExit(1)

    document = Document(str(target))
    backup = target.with_suffix(f".{datetime.now():%Y%m%d-%H%M%S}.bak.docx")
    shutil.copy2(target, backup)

    added = add_card_styles(document)
    anchored = add_anchor(document)
    document.save(str(target))

    print(f"备份：{backup}")
    print(f"新增样式：{'、'.join(added) if added else '无（已经有了）'}")
    print(f"插入锚点 {ANCHOR}：{'是' if anchored else '否（已经有了）'}")


if __name__ == "__main__":
    main()
