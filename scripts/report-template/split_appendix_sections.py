"""把附录拆成两节：附录1 竖版、附录2 横版（设计 §13.3）。

附表1 改成了固定版式的评定卡片，走竖版；只有附录2（桥梁基本状况卡片）需要横版。
原先整个附录都在横向节里。

顺带删掉 `[[TABLE:ASSESSMENT_APPENDIX]]` 样表——卡片不是"表头 + 数据行"的表，
不再支持表头样表。

模板是手工维护的，所以这个脚本只做必要的两处改动，先备份，可重复跑。

用法（在仓库根目录）：
    tools-python/.venv/Scripts/python.exe scripts/report-template/split_appendix_sections.py
"""

from __future__ import annotations

import copy
import shutil
import sys
from datetime import datetime
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools-python"))

from docx import Document  # noqa: E402
from docx.oxml import OxmlElement  # noqa: E402
from docx.oxml.ns import qn  # noqa: E402
from docx.oxml.table import CT_Tbl  # noqa: E402
from docx.oxml.text.paragraph import CT_P  # noqa: E402
from docx.text.paragraph import Paragraph  # noqa: E402

TEMPLATE = (
    REPO_ROOT / "templates" / "report" / "periodic_inspection_v1"
    / "periodic-inspection-v1.docx"
)

#: 正文节的序号（0 起）。附录1 的页面设置照抄它——同样的 A4 竖版、页边距和页眉页脚。
BODY_SECTION_INDEX = 4

#: 附录2 的标题。在它之前插分节符，前面就成了竖版的附录1。
APPENDIX_TWO_PREFIX = "附录2"


def section_breaks(document):
    return [
        child
        for child in document.element.body.iterchildren()
        if child.tag == qn("w:p") and child.xpath("./w:pPr/w:sectPr")
    ]


def drop_appendix_table_sample(document) -> bool:
    """删掉 [[TABLE:ASSESSMENT_APPENDIX]] 标记、它后面的样表和紧随的空段。"""
    body = document.element.body
    for child in list(body.iterchildren()):
        if not isinstance(child, CT_P):
            continue
        if "[[TABLE:ASSESSMENT_APPENDIX]]" not in Paragraph(child, document).text:
            continue
        following = child.getnext()
        body.remove(child)
        if isinstance(following, CT_Tbl):
            table, following = following, following.getnext()
            body.remove(table)
        if isinstance(following, CT_P) and not Paragraph(following, document).text.strip():
            body.remove(following)
        return True
    return False


def insert_portrait_break(document) -> bool:
    """在附录2 的标题之前插一个竖版分节符。"""
    body = document.element.body
    breaks = section_breaks(document)
    if len(breaks) <= BODY_SECTION_INDEX:
        raise SystemExit(f"分节结构与预期不符：只找到 {len(breaks)} 个分节符。")

    heading = None
    for child in body.iterchildren():
        if not isinstance(child, CT_P):
            continue
        paragraph = Paragraph(child, document)
        if paragraph.text.strip().startswith(APPENDIX_TWO_PREFIX) and paragraph.style.name.startswith(
            ("Heading", "标题")
        ):
            heading = child
            break
    if heading is None:
        raise SystemExit(f"找不到「{APPENDIX_TWO_PREFIX}」的标题段落。")

    # 它前面已经是分节符就别再插一个（可重复跑）。
    previous = heading.getprevious()
    while isinstance(previous, CT_P) and not Paragraph(previous, document).text.strip():
        if previous.xpath("./w:pPr/w:sectPr"):
            return False
        previous = previous.getprevious()

    # 附录1 的页面设置照抄正文节：同样的 A4 竖版、页边距和页眉页脚引用。
    source = breaks[BODY_SECTION_INDEX].xpath("./w:pPr/w:sectPr")[0]
    sect_pr = copy.deepcopy(source)
    # 页码不能在附录再重启一次，正文那份带着 pgNumType。
    for restart in sect_pr.findall(qn("w:pgNumType")):
        sect_pr.remove(restart)

    paragraph = OxmlElement("w:p")
    properties = OxmlElement("w:pPr")
    properties.append(sect_pr)
    paragraph.append(properties)
    heading.addprevious(paragraph)
    return True


def main() -> None:
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else TEMPLATE
    if not target.is_file():
        print(f"找不到模板：{target}")
        raise SystemExit(1)

    document = Document(str(target))
    before = len(document.sections)
    backup = target.with_suffix(f".{datetime.now():%Y%m%d-%H%M%S}.bak.docx")
    shutil.copy2(target, backup)

    removed = drop_appendix_table_sample(document)
    split = insert_portrait_break(document)
    document.save(str(target))

    check = Document(str(target))
    print(f"备份：{backup}")
    print(f"删除附表1 样表：{'是' if removed else '否（本来就没有）'}")
    print(f"插入竖版分节符：{'是' if split else '否（已经有了）'}")
    print(f"分节数：{before} -> {len(check.sections)}")
    for index, section in enumerate(check.sections):
        print(f"   节{index}: {section.page_width.cm:.1f} x {section.page_height.cm:.1f} cm")


if __name__ == "__main__":
    main()
