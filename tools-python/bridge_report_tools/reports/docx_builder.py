"""Docx Builder：把 ReportContext 装配进模板（设计 §18）。

顺序按设计 §18：复制模板 -> 替换标量 -> 在锚点处装配内容 -> 保存。域更新是下一环
（§19），不在这里做。

两条贯穿全篇的纪律：

* **只用模板里的命名样式**，不在代码里散字体、字号和边距常量（§18）。换版式改模板，
  生成器不动。
* **没有依据就不输出**。没照片的病害不生成占位图（§11.9），没病害的部位不生成假表格行
  （§10.3），装不出来的内容块直接报错而不是留个空段落——静默的错误输出是本设计从头到尾
  要防的东西。
"""

from __future__ import annotations

import shutil
from dataclasses import dataclass
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Callable

from docx import Document
from docx.document import Document as DocumentObject
from docx.enum.table import (
    WD_ALIGN_VERTICAL,
    WD_ROW_HEIGHT_RULE,
    WD_TABLE_ALIGNMENT,
)
from docx.image.exceptions import UnrecognizedImageError
from docx.image.image import Image
from docx.oxml import OxmlElement
from docx.oxml.table import CT_Tbl
from docx.oxml.text.paragraph import CT_P
from docx.shared import Emu, Length, Mm
from docx.table import Table, _Cell
from docx.text.paragraph import Paragraph

from bridge_report_tools.reports.conclusion import (
    ASSESSMENT_RESULT_INTRO,
    COMPONENT_WEIGHTS_INTRO,
    INSPECTION_CATEGORY,
    assessment_summary_sentence,
    bridge_profile_paragraphs,
    comparison_paragraphs,
    conclusion_paragraphs,
    control_indicator_paragraphs,
    defect_summary_lines,
    format_score,
    overall_assessment_paragraphs,
)
from bridge_report_tools.reports.contract import (
    TABLE_COLUMNS,
    TABLE_MARKER_PREFIX,
    ASSESSMENT_RESULT_COLUMNS,
    COMPONENT_WEIGHTS_COLUMNS,
    DEFECT_TABLE_COLUMNS,
    EQUIPMENT_TABLE_COLUMNS,
    PERSONNEL_ROLE_LABELS,
    PERSONNEL_TABLE_COLUMNS,
    STRUCTURE_PART_BY_CODE,
    numbering_key,
    personnel_placeholder,
)
from bridge_report_tools.reports.docx_edit import (
    BlockInserter,
    content_width,
    merge_across,
    merge_cells,
    merge_down,
    replace_placeholders,
    section_of,
    set_fixed_columns,
    set_table_full_width,
)
from bridge_report_tools.reports.docx_scan import (
    LOCATION_BODY,
    AnchorHit,
    TableSampleHit,
    iter_paragraphs,
    scan_document,
    scan_table_samples,
    visible_runs,
)
from bridge_report_tools.reports.errors import ReportBuildError
from bridge_report_tools.reports.photo_resampler import resample
from bridge_report_tools.reports.report_context import (
    ReportAssessment,
    ReportContext,
    ReportPhoto,
    ReportStructurePart,
)
from bridge_report_tools.reports.styles import (
    STYLE_BODY,
    STYLE_CARD_BAND,
    STYLE_CARD_CELL,
    STYLE_CARD_HEADER,
    STYLE_PHOTO,
    STYLE_PHOTO_CAPTION,
    STYLE_PHOTO_LAYOUT,
    STYLE_TABLE,
    STYLE_TABLE_CAPTION,
    STYLE_TABLE_CELL,
    STYLE_TABLE_HEADER,
)


NUMBER_TOKEN = "{n}"

# 列头与列数是契约，定义在 contract.py；模板的表头样表可以改写措辞和列宽，
# 但改不了列数（设计 §7.7）。这里只是给渲染函数取个短名字。
DEFECT_TABLE_HEADERS = DEFECT_TABLE_COLUMNS
WEIGHT_TABLE_HEADERS = COMPONENT_WEIGHTS_COLUMNS
RESULT_TABLE_HEADERS = ASSESSMENT_RESULT_COLUMNS
PERSONNEL_TABLE_HEADERS = PERSONNEL_TABLE_COLUMNS
EQUIPMENT_TABLE_HEADERS = EQUIPMENT_TABLE_COLUMNS

#: 正式报告里"没有内容"的单元格写斜杠，不是留空。
ABSENT_MARK = "/"
NO_COMPONENT_TEXT = "无此构件"

#: 附表1 是一张固定版式的卡片，不是"表头 + 数据行"的表，所以不支持表头样表。
#: 十二列只是合并用的网格：上半每行三对「标签/取值」（四列一对），中间的部件等级表
#: 按 1/2/2/5/2 分，下半各行按需要合并。
APPENDIX_CARD_GRID = 12

#: 卡片上半四行，每行三个字段；取值在渲染时按标签取，档案里没有的留空。
APPENDIX_CARD_HEAD = (
    ("桥梁编码", "主要结构", "上次检查日期"),
    ("桥梁名称", "桥长（m）", "建成年月"),
    ("路线名称", "最大跨径（m）", "本次检查日期"),
    ("桥位桩号", "管养单位", "上次大中修日期"),
)

#: 卡片下半的行数，用来算表格总行数。
APPENDIX_CARD_FOOT_ROWS = 5

#: 附录2 桥梁基本状况卡片的网格：三组字段并排，每组十列。三十列只是合并用的网格，
#: 不是三十条真列——F 段五栏、G 段十一栏都按这三十列切。
BRIDGE_CARD_GRID = 30
BRIDGE_CARD_GROUP = 10
BRIDGE_CARD_LAST = BRIDGE_CARD_GRID - 1

#: 「桥梁总体照片」「桥梁正面照片」两格的行高。库里没有这两张照片，格子留空，
#: 但高度要留出来，读者才看得出这里该贴照片。
BRIDGE_CARD_PHOTO_HEIGHT: Length = Mm(45)

#: F 段检测评定历史、G 段养护处治记录的数据行数。
BRIDGE_CARD_HISTORY_ROWS = 3
BRIDGE_CARD_TREATMENT_ROWS = 1

#: F 段五栏：编号、名称、占用的列区间。
BRIDGE_CARD_HISTORY_COLUMNS = (
    (75, "评定时间", 0, 4),
    (76, "检测类别", 5, 9),
    (77, "桥梁技术状况评定结果/特殊检查结论", 10, 19),
    (78, "处治对策", 20, 24),
    (79, "下次检测时间", 25, 29),
)

#: G 段十一栏。列宽按名称长短分，「处治类别」最长给四列，「时间（段）」两列。
BRIDGE_CARD_TREATMENT_COLUMNS = (
    (80, "时间（段）", 0, 1),
    (81, "处治类别（维修、加固、改造）", 2, 5),
    (82, "处治原因", 6, 8),
    (83, "处治范围", 9, 11),
    (84, "工程费用", 12, 14),
    (85, "经费来源", 15, 16),
    (86, "处治质量评定", 17, 18),
    (87, "建设单位", 19, 21),
    (88, "设计单位", 22, 24),
    (89, "施工单位", 25, 27),
    (90, "监理单位", 28, 29),
)

#: 附录2 卡片的固定版式，照正式报告的《桥梁基本状况卡片》逐行抄下来。
#:
#: 每项的第一个元素是版式类型：
#:   band      整行合并的分段行（A、B、C…）
#:   fields    一行三组「编号 / 名称 / 取值」；组里带第三个元素就是「联系电话」那种
#:   wide      一个字段占满整行；名称留空就只剩编号（H 段的第 91 格）
#:   material  左边竖着写组名，右边若干行的形式与材料
#:   history   F 段；treatment  G 段；photos  I 段的两张全景照片
#:
#: 「D 桥梁结构信息」出现两次是原卡片就有的分法：前一段是分孔和结构体系，
#: 后一段是各部位的形式与材料。
BRIDGE_CARD_LAYOUT: tuple[tuple, ...] = (
    ("band", "A 桥梁所处行政区划代码："),
    ("band", "B 行政识别数据"),
    ("fields", ((1, "路线编号"), (2, "路线名称"), (3, "路线等级"))),
    ("fields", ((4, "桥梁编号"), (5, "桥梁名称"), (6, "桥位桩号"))),
    ("fields", ((7, "功能类型"), (8, "被跨越道路（通道）名称"), (9, "被跨越道路（通道）桩号"))),
    ("fields", ((10, "设计荷载"), (11, "桥梁坡度"), (12, "桥梁平曲线半径"))),
    ("fields", ((13, "建成年限"), (14, "设计单位"), (15, "施工单位"))),
    ("fields", ((16, "监理单位"), (17, "业主单位"), (18, "管理单位", "联系电话"))),
    (
        "fields",
        (
            (19, "养护单位", "联系电话"),
            (20, "交通运输执法单位", "联系电话"),
            (21, "监管单位", "联系电话"),
        ),
    ),
    ("band", "C 桥梁技术指标"),
    ("fields", ((22, "桥梁全长(m)"), (23, "桥面总宽(m)"), (24, "行车道宽(m)"))),
    ("fields", ((25, "人行道宽度(m)"), (26, "护栏或防撞墙高度(m)"), (27, "中央分隔带宽度(m)"))),
    (
        "fields",
        ((28, "桥面标准净空(m)"), (29, "桥面实际净空(m)"), (30, "桥下通航等级及标准净空(m)")),
    ),
    ("fields", ((31, "桥下实际净空(m)"), (32, "引道总宽(m)"), (33, "引道线形或半径曲线半径(m)"))),
    ("fields", ((34, "设计洪水频率及其水位"), (35, "历史洪水位"), (36, "设计地震动峰值加速度系数"))),
    ("wide", (37, "桥面高程(m)")),
    ("band", "D 桥梁结构信息"),
    ("wide", (38, "桥梁分孔（m）")),
    ("wide", (39, "结构体系")),
    ("band", "D 桥梁结构信息"),
    (
        "material",
        "上部结构形式与材料",
        (
            (40, "主梁"),
            (41, "主拱圈"),
            (42, "桥（索）塔"),
            (43, "拱上建筑"),
            (44, "主缆"),
            (45, "斜拉索（含索力）"),
            (46, "吊杆（含索力）"),
            (47, "系杆"),
        ),
    ),
    (
        "material",
        "桥面形式与材料",
        (
            (48, "桥面铺装"),
            (49, "伸缩缝"),
            (50, "人行道、路缘"),
            (51, "栏杆、护栏"),
            (52, "照明、标志"),
        ),
    ),
    (
        "material",
        "下部结构形式与材料",
        ((53, "桥台"), (54, "桥墩"), (55, "锥坡、护坡"), (56, "翼墙、耳墙")),
    ),
    ("material", "基础形式与材料", ((57, "基础"), (58, "锚锭"))),
    (
        "material",
        "支座形式、材料与附属设施",
        ((59, "支座"), (60, "桥梁防撞设施"), (61, "航标及排水系统"), (62, "调治构造物")),
    ),
    ("band", "E 桥梁档案资料"),
    ("fields", ((63, "设计图纸"), (64, "设计文件"), (65, "竣工图纸"))),
    ("fields", ((66, "施工文件（含施工缺陷处理）"), (67, "验收文件"), (68, "行政审批文件"))),
    ("fields", ((69, "定期检查资料"), (70, "特殊检查资料"), (71, "历次维修、加固资料"))),
    ("fields", ((72, "其他档案"), (73, "档案形式"), (74, "建档时间（年/月）"))),
    ("band", "F 桥梁检测评定历史"),
    ("history",),
    ("band", "G 养护处治记录"),
    ("treatment",),
    ("band", "H 需要说明的事项"),
    ("wide", (91, "")),
    ("band", "I 其他"),
    ("photos", ((92, "桥梁总体照片"), (93, "桥梁正面照片"))),
    ("fields", ((94, "桥梁工程师"), (95, "填卡人"), (96, "填卡日期"))),
)

#: 某个结构部位没有正式病害时输出的确定性结论（设计 §10.3）。
#: 只说"没记录到"，不说"构件完好"或"构件不存在"——后两者数据里没有依据。
NO_DEFECT_TEXT = "本次检查未记录明显病害。"

#: 照片两栏版式（设计 §11.3-§11.6）。
PHOTO_COLUMNS = 2
#: 图片框高度固定，两栏等宽等高；图片按比例缩放进框，不裁剪不拉伸。
PHOTO_FRAME_HEIGHT: Length = Mm(52)
#: 单元格左右内边距之和，从版心宽度里扣掉，免得图片把表格撑宽。
PHOTO_CELL_PADDING: Length = Mm(4)


@dataclass(frozen=True)
class BuildResult:
    output_path: Path
    #: 实际装配了的锚点，按文档顺序，供任务日志和最终校验对账。
    blocks_rendered: list[str]
    #: 模板里出现、但上下文没给取值的标量占位符。正常情况下为空——模板校验只保证
    #: 占位符名字认识，取值缺不缺要到这里才知道。
    missing_placeholders: list[str]


class BuildInputs:
    """一次装配用得到的全部东西，省得每个渲染函数都带一长串参数。"""

    def __init__(self, document: DocumentObject, context: ReportContext,
                 archive_root: Path, photo_cache: Path):
        self.document = document
        self.context = context
        self.archive_root = archive_root
        #: 重采样后的照片副本放这里，生成结束即删。归档原图不动。
        self.photo_cache = photo_cache
        #: 内容块 -> 模板画的表头样表。没画样表的块不在里面。
        self.samples: dict[str, TableSampleHit] = {
            sample.block: sample
            for sample in scan_table_samples(document)
            if sample.has_table and sample.block in TABLE_COLUMNS
        }
        #: 编号格式 -> 已发出的号数。序列按格式串走，不按内容块走：正式报告里
        #: 4.1.1 的部件权重表是 表4.1-1，4.1.2 的评定表是 表4.1-2，两张表分属两个
        #: 内容块却共用一条编号序列。谁跟谁共号由模板配的格式串决定，不写死在代码里。
        self.numbers: dict[str, int] = {}

    def next_number(self, number_format: str) -> str:
        self.numbers[number_format] = self.numbers.get(number_format, 0) + 1
        return number_format.replace(NUMBER_TOKEN, str(self.numbers[number_format]))


Renderer = Callable[[BuildInputs, AnchorHit, BlockInserter], None]


# --------------------------------------------------------------------------
# 通用小工具
# --------------------------------------------------------------------------


def _cell_text(cell: _Cell, text: str, style: str) -> None:
    """写单元格：用单元格自带的第一个空段落，不再追加，免得多出空行。

    水平居中来自样式（报告表头 / 报告表格文字），垂直居中在这里设——它是单元格
    属性不是段落属性，样式表达不了。合并单元格本来就垂直居中，普通单元格若顶端
    对齐，两者并排会明显错位。
    """
    paragraph = cell.paragraphs[0]
    paragraph.style = style
    paragraph.text = text
    cell.vertical_alignment = WD_ALIGN_VERTICAL.CENTER


def _no_split(table: Table) -> None:
    """行内不跨页断开，但行与行之间可以断。

    注意不能改用段落的"与下段同页"来实现：那样一行接一行连成一串，整张表变成一个
    不可分割的块，Word 只好把它整体推到下一页——实测 50 行的下部结构病害表因此与
    前面的概要文字断开，另起一页开始。

    cantSplit 在 trPr 里必须排在 tblHeader 之前，所以插到最前面而不是追加。
    """
    for row in table.rows:
        properties = row._tr.get_or_add_trPr()
        properties.insert(0, OxmlElement("w:cantSplit"))


def _repeat_header(table: Table) -> None:
    """表头在跨页时重复。长病害表必然跨页，没有表头的续页读不了。"""
    properties = table.rows[0]._tr.get_or_add_trPr()
    properties.append(OxmlElement("w:tblHeader"))


def _content_table(table: Table, inputs: "BuildInputs", into: BlockInserter,
                   block: str) -> None:
    """内容表格的共同设置：列宽、表头、表头跨页重复、行内不断页。

    模板画了表头样表就照它的列宽和表头文字来（设计 §7.7）；没画就退回"占满版心、
    列宽交给 Word 按内容分配"——样表是可选的，老模板照常能用。
    """
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    sample = inputs.samples.get(block)
    if sample is not None and sample.widths and all(sample.widths):
        set_fixed_columns(table, [Emu(width) for width in sample.widths])
        headers = sample.headers
    else:
        set_table_full_width(table, _available_width(inputs, into))
        headers = list(TABLE_COLUMNS[block])
    header_cells = table.rows[0].cells
    for index, header in enumerate(headers):
        _cell_text(header_cells[index], header, STYLE_TABLE_HEADER)
    _no_split(table)
    _repeat_header(table)


def _data_rows(table: Table):
    """表头之后的数据行，一次取完。

    别用 table.cell(r, c) 逐格取：python-docx 每次调用都重建整张表的单元格网格，
    297 行的病害表因此要 158 秒。按行取是线性的。
    """
    return [row.cells for row in list(table.rows)[1:]]


def _fill(cells, values) -> None:
    for index, value in enumerate(values):
        _cell_text(cells[index], value, STYLE_TABLE_CELL)


def _equal_columns(total: Length, count: int) -> list[Length]:
    """等分的列宽表。整除余下的几个 EMU 不补，肉眼看不出来。"""
    return [Emu(int(total) // count)] * count


def _text_or_dash(value: object) -> str:
    """空值统一写成短横。留空的单元格与"这一项确实没有"在纸面上分不开。"""
    if value is None or value == "":
        return "—"
    return str(value)


def _number(inputs: BuildInputs, key: str) -> str:
    """按模板配置的格式发一个表号（设计 §7.5）。

    同一个格式串共用一条序列，按文档顺序递增——4.1.1 和 4.1.2 两张表都配
    「表4.1-{n}」，于是自然得到 表4.1-1 和 表4.1-2。
    """
    number_format = inputs.context.number_format(key)
    if number_format is None or NUMBER_TOKEN not in number_format:
        raise ReportBuildError(
            code="report_number_format_missing",
            message=f"模板没有配置 {key} 的编号格式，无法生成表号或图号。",
        )
    return inputs.next_number(number_format)


def _available_width(inputs: BuildInputs, into: BlockInserter) -> Length:
    """锚点所在分节的版心宽度。

    必须按锚点所在的节算：本模板最后一节是横向附录节，拿 sections[-1] 会把正文里
    的表格排成 24.62cm 宽，越过页边距。
    """
    return content_width(section_of(inputs.document, into.anchor))


def _part_or_empty(context: ReportContext, part_code: str) -> ReportStructurePart | None:
    if part_code not in STRUCTURE_PART_BY_CODE:
        raise ReportBuildError(
            code="report_anchor_part_unknown",
            message=f"锚点里的结构部位代码 {part_code} 未知。",
        )
    return context.part(part_code)


# --------------------------------------------------------------------------
# 内容块：病害表（设计 §10）
# --------------------------------------------------------------------------


def render_defect_tables(inputs: BuildInputs, anchor: AnchorHit, into: BlockInserter) -> None:
    part = _part_or_empty(inputs.context, anchor.part or "")
    if part is None:
        into.paragraph(NO_DEFECT_TEXT, style=STYLE_BODY)
        return

    # 表前先按评价部件概述查到了什么病害，没病害的部件也列出来（设计 §10.4）。
    summary = defect_summary_lines(inputs.context, part)
    for line in summary:
        into.paragraph(line, style=STYLE_BODY)

    if not part.defect_rows:
        # 不生成空表格，也不声称构件完好或不存在（设计 §10.3）。概要已经逐个部件
        # 说过"未见明显病害"时不必再来一句笼统的。
        if not summary:
            into.paragraph(NO_DEFECT_TEXT, style=STYLE_BODY)
        return

    caption = _number(inputs, numbering_key(anchor.block, anchor.part))
    into.paragraph(f"{caption}  {part.part_label}病害表", style=STYLE_TABLE_CAPTION)

    table = into.table(len(part.defect_rows) + 1, len(DEFECT_TABLE_HEADERS), STYLE_TABLE)
    _content_table(table, inputs, into, "DEFECT_TABLES")

    for cells, row in zip(_data_rows(table), part.defect_rows):
        values = (
            str(row.row_number),
            _text_or_dash(row.part_name),
            _text_or_dash(row.component_number),
            _text_or_dash(row.defect_location),
            row.defect_type,
            row.description,
            _text_or_dash(row.scale),
            _text_or_dash(row.deduction),
            _text_or_dash(row.component_score),
            # 与图题共用同一批号码（设计 §11.8）。上下文模型已经把这条不变量验过。
            "、".join(row.photo_numbers) if row.photo_numbers else "—",
        )
        _fill(cells, values)


# --------------------------------------------------------------------------
# 内容块：病害照片（设计 §11）
# --------------------------------------------------------------------------


def render_defect_photos(inputs: BuildInputs, anchor: AnchorHit, into: BlockInserter) -> None:
    part = _part_or_empty(inputs.context, anchor.part or "")
    if part is None or not part.photos:
        # 没照片就什么都不出：不生成虚假占位图（设计 §11.9）。
        return

    frame_width = Emu(
        int(_available_width(inputs, into)) // PHOTO_COLUMNS - int(PHOTO_CELL_PADDING)
    )
    photos = part.photos
    # 整块照片装进一张表，每两行一组（上排图片、下排图题）。不做成"一行一张表"是
    # 因为紧挨着的两张表会被 Word 合并——实测 6 张表读回来只剩 5 张。
    rows = 2 * ((len(photos) + PHOTO_COLUMNS - 1) // PHOTO_COLUMNS)
    table = into.table(rows, PHOTO_COLUMNS, STYLE_PHOTO_LAYOUT)
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    column_width = Emu(int(frame_width) + int(PHOTO_CELL_PADDING))
    set_fixed_columns(table, [column_width] * PHOTO_COLUMNS)

    for index, photo in enumerate(photos):
        row, column = 2 * (index // PHOTO_COLUMNS), index % PHOTO_COLUMNS
        image_paragraph = table.cell(row, column).paragraphs[0]
        image_paragraph.style = STYLE_PHOTO
        _insert_picture(inputs, image_paragraph, photo, frame_width)

        # 图题落在表格单元格里，与正式报告的载体一致（设计 §11.11）——我们自己的
        # Word 导入器也是从单元格里扫题注的，段落里的题注它读不到。
        _cell_text(table.cell(row + 1, column), photo.caption, STYLE_PHOTO_CAPTION)

    # 最后一行只剩一张时右栏留空（设计 §11.6），不把它拉宽占满整行——那样这一张的
    # 尺寸就和前面所有照片不一致了。
    for column in range(len(photos) % PHOTO_COLUMNS or PHOTO_COLUMNS, PHOTO_COLUMNS):
        table.cell(rows - 2, column).paragraphs[0].style = STYLE_PHOTO
        _cell_text(table.cell(rows - 1, column), "", STYLE_PHOTO_CAPTION)

    _no_split(table)


def _insert_picture(
    inputs: BuildInputs, paragraph: Paragraph, photo: ReportPhoto, frame_width: Length
) -> None:
    path = (inputs.archive_root / photo.storage_relative_path).resolve()
    if not path.is_file():
        # 生成前检查本该拦住（设计 §11.10）；真到这一步还缺文件，说明归档在生成
        # 期间被动过，只能中止——绝不能出一份少图的报告。
        raise ReportBuildError(
            code="report_photo_file_missing",
            message=f"照片 {photo.report_number} 的归档文件不存在：{photo.storage_relative_path}",
        )

    try:
        # 按版面尺寸重采样后再嵌入：归档原图是证据要留全尺寸，报告里那张只有
        # 6.9cm 宽，原样嵌 455 张会做出一份 126MB 的报告（设计 §11.4）。
        embedded = resample(
            path, int(frame_width), int(PHOTO_FRAME_HEIGHT), inputs.photo_cache
        )
        width, height = _fit(embedded, frame_width, PHOTO_FRAME_HEIGHT)
        paragraph.add_run().add_picture(str(embedded), width=width, height=height)
    except UnrecognizedImageError as error:
        raise ReportBuildError(
            code="report_photo_unreadable",
            message=f"照片 {photo.report_number} 无法作为图片读取：{photo.storage_relative_path}",
        ) from error


def _fit(path: Path, frame_width: Length, frame_height: Length) -> tuple[Length, Length]:
    """按比例完整缩放进框，不裁剪、不拉伸（设计 §11.4）。"""
    with path.open("rb") as stream:
        image = Image.from_file(stream)
    scale = min(int(frame_width) / int(image.width), int(frame_height) / int(image.height))
    scale = min(scale, 1.0)
    return Emu(int(int(image.width) * scale)), Emu(int(int(image.height) * scale))


# --------------------------------------------------------------------------
# 内容块：人员表与设备表（设计 §15）
# --------------------------------------------------------------------------


def render_personnel_table(inputs: BuildInputs, anchor: AnchorHit, into: BlockInserter) -> None:
    people = inputs.context.personnel
    if not people:
        return
    table = into.table(len(people) + 1, len(PERSONNEL_TABLE_HEADERS), STYLE_TABLE)
    _content_table(table, inputs, into, "PERSONNEL_TABLE")
    for offset, (cells, person) in enumerate(zip(_data_rows(table), people), start=1):
        values = (
            str(offset),
            person.full_name,
            _text_or_dash(person.organization),
            _text_or_dash(person.professional_title),
            _text_or_dash(person.qualification_certificate_no),
            PERSONNEL_ROLE_LABELS.get(person.role_code, person.role_code),
        )
        _fill(cells, values)


def render_equipment_list(inputs: BuildInputs, anchor: AnchorHit, into: BlockInserter) -> None:
    equipment = inputs.context.equipment
    if not equipment:
        return
    table = into.table(len(equipment) + 1, len(EQUIPMENT_TABLE_HEADERS), STYLE_TABLE)
    _content_table(table, inputs, into, "EQUIPMENT_LIST")
    for offset, (cells, item) in enumerate(zip(_data_rows(table), equipment), start=1):
        values = (
            str(offset),
            item.equipment_name,
            _text_or_dash(item.model_spec),
            _text_or_dash(item.asset_number),
            _text_or_dash(item.measurement_range),
            _text_or_dash(item.accuracy),
            _text_or_dash(item.calibration_certificate_no),
            _text_or_dash(item.calibration_valid_until),
            _text_or_dash(item.purpose),
        )
        _fill(cells, values)


# --------------------------------------------------------------------------
# 内容块：桥梁概况、历史对比、评定结果、附表1、结论（设计 §12-§14）
# --------------------------------------------------------------------------


def render_bridge_profile(inputs: BuildInputs, anchor: AnchorHit, into: BlockInserter) -> None:
    """§1.1 桥梁概况：几段叙述文字，不是两列表。

    正式报告这一节就是这么写的；而且同样这批数在附录2 的卡片里已经排成表了，
    §1.1 再摆一张只是把同一批数印两遍。

    档案还没录的项不出现——句子按可用的字段自己缩短，缺得太多就整段不出，
    不留「未知」也不留半截话（措辞规则都在 conclusion 模块里）。
    """
    for text in bridge_profile_paragraphs(inputs.context):
        into.paragraph(text, style=STYLE_BODY)


def render_previous_comparison(
    inputs: BuildInputs, anchor: AnchorHit, into: BlockInserter
) -> None:
    """某个结构部位与历史检查的对比结论（设计 §12.2）。

    措辞在 conclusion 模块里，这里只把段落摆进文档——免责说明必须紧跟结论，两者
    不能被分开装配到不同位置，那样"增加 5 条"就会被单独读成"新增 5 处病害"。
    """
    part = _part_or_empty(inputs.context, anchor.part or "")
    if part is None:
        into.paragraph(NO_DEFECT_TEXT, style=STYLE_BODY)
        return
    year = inputs.context.scalars.get("comparison_year")
    for text in comparison_paragraphs(part, year):
        into.paragraph(text, style=STYLE_BODY)


def render_component_weights(
    inputs: BuildInputs, anchor: AnchorHit, into: BlockInserter
) -> None:
    """4.1.1 部件权重分配，即 表4.1-1（设计 §13.1）。

    本桥没有的部件也要列出来并注明"无此构件"——那一行的权重正是被摊给同部位其余
    部件的那部分，不写出来读者看不懂重分配后的数字是怎么来的。
    """
    assessment = _assessment_or_raise(inputs.context)
    rows = assessment.component_weights
    if not rows:
        return

    caption = _number(inputs, numbering_key(anchor.block, anchor.part))
    into.paragraph(COMPONENT_WEIGHTS_INTRO.format(number=caption), style=STYLE_BODY)
    into.paragraph(f"{caption}  桥梁部件权重计算表", style=STYLE_TABLE_CAPTION)

    table = into.table(len(rows) + 1, len(WEIGHT_TABLE_HEADERS), STYLE_TABLE)
    _content_table(table, inputs, into, "COMPONENT_WEIGHTS")

    for cells, row in zip(_data_rows(table), rows):
        values = (
            "",  # 「部位」列下面按结构合并
            str(row.order),
            _text_or_dash(row.category_name or row.category_id),
            _weight(row.configured_weight),
            _weight(row.effective_weight) if row.present else ABSENT_MARK,
            str(row.component_count) if row.component_count else ABSENT_MARK,
            ABSENT_MARK if row.present else NO_COMPONENT_TEXT,
        )
        _fill(cells, values)

    for start, end, label in _row_groups([row.part_label for row in rows]):
        _cell_text(merge_down(table, 0, start + 1, end + 1), label, STYLE_TABLE_CELL)


def render_assessment_result(
    inputs: BuildInputs, anchor: AnchorHit, into: BlockInserter
) -> None:
    """4.1.2 桥梁技术状况等级，即 表4.1-2 总体技术状况评定表（设计 §13）。

    版式照正式报告：每个评价部件下按构件评分分档，一档一行；「结构」「桥梁结构
    技术状况评分」等列按结构合并，「桥梁总体技术状况评分」「综合评级」贯通全表。
    """
    assessment = _assessment_or_raise(inputs.context)
    if not assessment.categories:
        return

    caption = _number(inputs, numbering_key(anchor.block, anchor.part))
    into.paragraph(ASSESSMENT_RESULT_INTRO.format(number=caption), style=STYLE_BODY)
    into.paragraph(f"{caption}  总体技术状况评定表", style=STYLE_TABLE_CAPTION)

    # 先把每个部件占几行算清楚：没有分档数据时至少占一行，否则整张表会塌掉。
    spans = [max(1, len(category.score_bands)) for category in assessment.categories]
    table = into.table(sum(spans) + 1, len(RESULT_TABLE_HEADERS), STYLE_TABLE)
    _content_table(table, inputs, into, "ASSESSMENT_RESULT")

    part_by_code = {part.part_code: part for part in assessment.parts}
    part_labels: list[str] = []
    row = 1
    for order, (category, span) in enumerate(zip(assessment.categories, spans), start=1):
        for offset, band in enumerate(category.score_bands or [None]):
            _cell_text(table.cell(row + offset, 3),
                       str(band.component_count) if band else str(category.component_count),
                       STYLE_TABLE_CELL)
            _cell_text(table.cell(row + offset, 4),
                       format_score(band.score) if band else "—", STYLE_TABLE_CELL)
        _cell_text(merge_down(table, 1, row, row + span - 1), str(order), STYLE_TABLE_CELL)
        _cell_text(merge_down(table, 2, row, row + span - 1),
                   _text_or_dash(category.category_name or category.category_id),
                   STYLE_TABLE_CELL)
        _cell_text(merge_down(table, 5, row, row + span - 1),
                   format_score(category.score), STYLE_TABLE_CELL)
        part_labels.extend([category.part_label] * span)
        row += span

    for start, end, label in _row_groups(part_labels):
        part = part_by_code.get(
            next(c.part_code for c in assessment.categories if c.part_label == label)
        )
        _cell_text(merge_down(table, 0, start + 1, end + 1), label, STYLE_TABLE_CELL)
        _cell_text(merge_down(table, 6, start + 1, end + 1),
                   format_score(part.score) if part else "—", STYLE_TABLE_CELL)
        _cell_text(merge_down(table, 7, start + 1, end + 1),
                   _weight(part.weight) if part else "—", STYLE_TABLE_CELL)
        _cell_text(merge_down(table, 8, start + 1, end + 1),
                   _grade_number(part.grade if part else None), STYLE_TABLE_CELL)

    last = len(part_labels)
    _cell_text(merge_down(table, 9, 1, last), format_score(assessment.overall_score),
               STYLE_TABLE_CELL)
    _cell_text(merge_down(table, 10, 1, last), _text_or_dash(assessment.overall_grade),
               STYLE_TABLE_CELL)

    into.paragraph(assessment_summary_sentence(inputs.context), style=STYLE_BODY)


def render_control_indicator(
    inputs: BuildInputs, anchor: AnchorHit, into: BlockInserter
) -> None:
    """4.2 桥梁技术状况等级单项控制指标（H21 4.3）。"""
    _assessment_or_raise(inputs.context)
    for text in control_indicator_paragraphs(inputs.context):
        into.paragraph(text, style=STYLE_BODY)


def render_overall_assessment(
    inputs: BuildInputs, anchor: AnchorHit, into: BlockInserter
) -> None:
    """4.3 桥梁技术状况等级综合评定。"""
    _assessment_or_raise(inputs.context)
    for text in overall_assessment_paragraphs(inputs.context):
        into.paragraph(text, style=STYLE_BODY)


def _row_groups(labels: list[str]) -> list[tuple[int, int, str]]:
    """把相邻相同的标签折成 (起, 止, 标签) 区间，用于纵向合并。"""
    groups: list[tuple[int, int, str]] = []
    for index, label in enumerate(labels):
        if groups and groups[-1][2] == label:
            groups[-1] = (groups[-1][0], index, label)
        else:
            groups.append((index, index, label))
    return groups


def _grade_number(grade: str | None) -> str:
    """评定表的「等级」列只印数字（2），综合评级那一列才带"类"。"""
    if not grade:
        return "—"
    return grade.removesuffix("类")


def render_assessment_appendix(
    inputs: BuildInputs, anchor: AnchorHit, into: BlockInserter
) -> None:
    """附表1：桥梁技术状况评定卡片（设计 §13.3）。

    版式照正式报告的卡片：上半是桥梁基本信息，中间是十六个规范部件逐项的评定等级
    （含"无此构件"），下半是总体评分、养护建议和签署栏。**不是**按部件汇总的那种
    数据表——那种表 4.1.2 已经出过一次了。

    档案里没有的项一律留空，不编数据（设计 §14 第 5 条）。哪些格子是空的，读者一眼
    看得见，也就知道该去补哪些档案。
    """
    assessment = _assessment_or_raise(inputs.context)
    components = assessment.component_weights
    if not components:
        return

    caption = _number(inputs, numbering_key(anchor.block, anchor.part))
    into.paragraph(f"{caption}  桥梁技术状况评定表", style=STYLE_TABLE_CAPTION)

    table = into.table(
        len(APPENDIX_CARD_HEAD) + 2 + len(components) + APPENDIX_CARD_FOOT_ROWS,
        APPENDIX_CARD_GRID,
        STYLE_TABLE,
    )
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    set_table_full_width(table, _available_width(inputs, into))
    _no_split(table)

    _appendix_card_head(inputs.context, table)
    top = len(APPENDIX_CARD_HEAD)
    _appendix_card_grades(assessment, table, top)
    _appendix_card_foot(assessment, table, top + 2 + len(components))


def _appendix_card_head(context: ReportContext, table: Table) -> None:
    """上半：桥梁基本信息。每行三对「标签 / 取值」，四列一对。"""
    scalars, profile = context.scalars, context.bridge_profile
    values = {
        "桥梁编码": profile.business_code,
        "桥梁名称": scalars.get("bridge_name"),
        "路线名称": scalars.get("route_name"),
        "桥位桩号": profile.station_mark,
        "主要结构": profile.bridge_type,
        "桥长（m）": _decimal(profile.bridge_length_m),
        "最大跨径（m）": profile.span_combination,
        "管养单位": profile.maintenance_org,
        "建成年月": str(profile.built_year) if profile.built_year else None,
        "本次检查日期": scalars.get("inspection_date"),
        # 上次检查日期与上次大中修日期库里还没有，留空（见设计 §13.3 的待补清单）。
        "上次检查日期": None,
        "上次大中修日期": None,
    }
    for row, labels in enumerate(APPENDIX_CARD_HEAD):
        for pair, label in enumerate(labels):
            left = pair * 4
            _cell_text(merge_across(table, row, left, left + 1), label, STYLE_CARD_HEADER)
            _cell_text(
                merge_across(table, row, left + 2, left + 3),
                values.get(label) or "",
                STYLE_CARD_CELL,
            )


def _appendix_card_grades(assessment: ReportAssessment, table: Table, top: int) -> None:
    """中间：十六个规范部件逐项的评定等级，按结构分组合并。"""
    _cell_text(merge_cells(table, top, 0, top + 1, 0), "序号", STYLE_CARD_HEADER)
    _cell_text(merge_across(table, top, 1, 4), "桥梁组成及评级", STYLE_CARD_HEADER)
    _cell_text(merge_across(table, top, 5, 11), "桥梁部件及评级", STYLE_CARD_HEADER)
    for left, right, title in (
        (1, 2, "桥梁组成"),
        (3, 4, "评定等级（1~5）"),
        (5, 9, "部件名称"),
        (10, 11, "评定等级（1~5）"),
    ):
        _cell_text(merge_across(table, top + 1, left, right), title, STYLE_CARD_HEADER)

    grade_of = {category.category_id: category.grade for category in assessment.categories}
    part_grade = {part.part_code: part.grade for part in assessment.parts}
    first = top + 2
    for offset, row in enumerate(assessment.component_weights):
        line = first + offset
        _cell_text(table.cell(line, 0), str(row.order), STYLE_CARD_CELL)
        _cell_text(
            merge_across(table, line, 5, 9),
            row.category_name or row.category_id,
            STYLE_CARD_CELL,
        )
        # 本桥没有的部件写斜杠，与 表4.1-1 的「无此构件」是同一件事。
        grade = _grade_number(grade_of.get(row.category_id)) if row.present else ABSENT_MARK
        _cell_text(merge_across(table, line, 10, 11), grade, STYLE_CARD_CELL)

    # 「桥梁组成」和它的等级按结构合并：上部三行、下部七行、桥面系六行。
    labels = [row.part_label for row in assessment.component_weights]
    codes = {row.part_label: row.part_code for row in assessment.component_weights}
    for start, end, label in _row_groups(labels):
        _cell_text(
            merge_cells(table, first + start, 1, first + end, 2), label, STYLE_CARD_CELL
        )
        _cell_text(
            merge_cells(table, first + start, 3, first + end, 4),
            _grade_number(part_grade.get(codes[label])),
            STYLE_CARD_CELL,
        )


def _appendix_card_foot(assessment: ReportAssessment, table: Table, top: int) -> None:
    """下半：总体评分、养护建议、签署栏。库里没有的项留空，不编。"""
    grade = assessment.overall_grade or ""
    rows = (
        (
            ("桥梁总体技术状况评分 Dr", 0, 3),
            (format_score(assessment.overall_score), 4, 5),
            ("总体技术状况等级", 6, 9),
            (grade, 10, 11),
        ),
        (
            ("综合考虑主要部件最差损坏状况最终评定桥梁技术等级", 0, 7),
            (grade, 8, 11),
        ),
        (
            ("全桥清洁评分（0~100）", 0, 3),
            ("", 4, 5),
            ("保养、小修状况评分（0~100）", 6, 9),
            ("", 10, 11),
        ),
        (("养护建议", 0, 1), ("", 2, 11)),
        (
            ("记录人", 0, 1),
            ("", 2, 3),
            ("负责人", 4, 5),
            ("", 6, 7),
            ("建议下次检查时间", 8, 9),
            ("", 10, 11),
        ),
    )
    for offset, cells in enumerate(rows):
        for index, (text, left, right) in enumerate(cells):
            style = STYLE_CARD_HEADER if index % 2 == 0 else STYLE_CARD_CELL
            _cell_text(merge_across(table, top + offset, left, right), text, style)


# --------------------------------------------------------------------------
# 内容块：附录2 桥梁基本状况卡片（设计 §13.3）
# --------------------------------------------------------------------------


def render_bridge_card(inputs: BuildInputs, anchor: AnchorHit, into: BlockInserter) -> None:
    """附录2：桥梁基本状况卡片，A 到 I 九段共九十六格。

    版式全部固定，写在 BRIDGE_CARD_LAYOUT 里；能从档案取到的格子由
    `_bridge_card_values` 给，取不到的留空。九十六格里大多数（路线等级、设计荷载、
    净空、各类形式与材料、档案资料）库里还没有对应字段——留空等档案补，不编数据。

    这张卡片不带表题：模板里「附录2 桥梁基本状况卡片」的标题就是它的题目，
    正式报告里也没有再编一个表号。
    """
    values = _bridge_card_values(inputs.context)
    table = into.table(_bridge_card_row_count(), BRIDGE_CARD_GRID, STYLE_TABLE)
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    # 三十列是合并用的网格，不是三十条真列，必须等分固定：交给 autofit 按内容分配，
    # 空格子会被压到看不见，整张卡片就散了。
    set_fixed_columns(table, _equal_columns(_available_width(inputs, into), BRIDGE_CARD_GRID))
    _no_split(table)

    row = 0
    for entry in BRIDGE_CARD_LAYOUT:
        row = _bridge_card_row(table, values, row, entry)


def _bridge_card_row_count() -> int:
    total = 0
    for entry in BRIDGE_CARD_LAYOUT:
        kind = entry[0]
        if kind == "material":
            total += len(entry[2])
        elif kind == "history":
            total += 2 + BRIDGE_CARD_HISTORY_ROWS
        elif kind == "treatment":
            total += 2 + BRIDGE_CARD_TREATMENT_ROWS
        else:
            total += 1
    return total


def _bridge_card_row(table: Table, values: dict[str, str], row: int, entry: tuple) -> int:
    """画一条版式，返回下一条从第几行开始。"""
    kind = entry[0]
    if kind == "band":
        _cell_text(merge_across(table, row, 0, BRIDGE_CARD_LAST), entry[1], STYLE_CARD_BAND)
        return row + 1
    if kind == "fields":
        for index, group in enumerate(entry[1]):
            _bridge_card_field(table, values, row, index, group)
        return row + 1
    if kind == "wide":
        return _bridge_card_wide(table, values, row, entry[1])
    if kind == "material":
        return _bridge_card_material(table, values, row, entry[1], entry[2])
    if kind == "history":
        return _bridge_card_history(table, values, row)
    if kind == "treatment":
        return _bridge_card_treatment(table, row)
    if kind == "photos":
        return _bridge_card_photos(table, row, entry[1])
    raise ReportBuildError(
        code="report_card_layout_unknown",
        message=f"附录2 卡片里出现了未知的版式类型 {kind}。",
    )


def _bridge_card_field(
    table: Table, values: dict[str, str], row: int, index: int, group: tuple
) -> None:
    """一组「编号 / 名称 / 取值」，占十列。

    带联系电话的组（管理、养护、执法、监管单位）把这十列再切一次：名称和取值各让
    出一列给电话。四组单位在正式卡片里就是这么排的。
    """
    left = index * BRIDGE_CARD_GROUP
    number, label = group[0], group[1]
    _cell_text(table.cell(row, left), str(number), STYLE_CARD_HEADER)
    if len(group) == 2:
        _cell_text(merge_across(table, row, left + 1, left + 3), label, STYLE_CARD_HEADER)
        _cell_text(
            merge_across(table, row, left + 4, left + 9),
            values.get(str(number), ""),
            STYLE_CARD_CELL,
        )
        return
    _cell_text(merge_across(table, row, left + 1, left + 2), label, STYLE_CARD_HEADER)
    _cell_text(
        merge_across(table, row, left + 3, left + 5),
        values.get(str(number), ""),
        STYLE_CARD_CELL,
    )
    _cell_text(merge_across(table, row, left + 6, left + 7), group[2], STYLE_CARD_HEADER)
    _cell_text(
        merge_across(table, row, left + 8, left + 9),
        values.get(f"{number}.tel", ""),
        STYLE_CARD_CELL,
    )


def _bridge_card_wide(table: Table, values: dict[str, str], row: int, field: tuple) -> int:
    """一个字段占满整行。名称留空就只有编号——H 段的第 91 格是块空白，没有名称。"""
    number, label = field
    _cell_text(table.cell(row, 0), str(number), STYLE_CARD_HEADER)
    value_start = 1
    if label:
        _cell_text(merge_across(table, row, 1, 3), label, STYLE_CARD_HEADER)
        value_start = 4
    _cell_text(
        merge_across(table, row, value_start, BRIDGE_CARD_LAST),
        values.get(str(number), ""),
        STYLE_CARD_CELL,
    )
    return row + 1


def _bridge_card_material(
    table: Table, values: dict[str, str], row: int, group_label: str, fields: tuple
) -> int:
    """D 段的形式与材料：左边一列竖着写组名，右边逐行是编号、名称和取值。"""
    last = row + len(fields) - 1
    _cell_text(merge_cells(table, row, 0, last, 1), group_label, STYLE_CARD_HEADER)
    for offset, (number, label) in enumerate(fields):
        line = row + offset
        _cell_text(table.cell(line, 2), str(number), STYLE_CARD_HEADER)
        _cell_text(merge_across(table, line, 3, 6), label, STYLE_CARD_HEADER)
        _cell_text(
            merge_across(table, line, 7, BRIDGE_CARD_LAST),
            values.get(str(number), ""),
            STYLE_CARD_CELL,
        )
    return last + 1


def _bridge_card_history(table: Table, values: dict[str, str], row: int) -> int:
    """F 段检测评定历史：编号行、名称行，再是数据行。

    第一行由本次检查填，其余行留空给手工补历次记录——库里眼下只存着本年度和用于
    对比的上一年度，凑不出完整的评定史。
    """
    for number, label, left, right in BRIDGE_CARD_HISTORY_COLUMNS:
        _cell_text(merge_across(table, row, left, right), str(number), STYLE_CARD_HEADER)
        _cell_text(merge_across(table, row + 1, left, right), label, STYLE_CARD_HEADER)
    for offset in range(BRIDGE_CARD_HISTORY_ROWS):
        line = row + 2 + offset
        for number, _, left, right in BRIDGE_CARD_HISTORY_COLUMNS:
            text = values.get(str(number), "") if offset == 0 else ""
            _cell_text(merge_across(table, line, left, right), text, STYLE_CARD_CELL)
    return row + 2 + BRIDGE_CARD_HISTORY_ROWS


def _bridge_card_treatment(table: Table, row: int) -> int:
    """G 段养护处治记录：编号行、名称行，数据行留空——维修加固记录不在本系统里。"""
    for number, label, left, right in BRIDGE_CARD_TREATMENT_COLUMNS:
        _cell_text(merge_across(table, row, left, right), str(number), STYLE_CARD_HEADER)
        _cell_text(merge_across(table, row + 1, left, right), label, STYLE_CARD_HEADER)
    for offset in range(BRIDGE_CARD_TREATMENT_ROWS):
        for _, _, left, right in BRIDGE_CARD_TREATMENT_COLUMNS:
            _cell_text(merge_across(table, row + 2 + offset, left, right), "", STYLE_CARD_CELL)
    return row + 2 + BRIDGE_CARD_TREATMENT_ROWS


def _bridge_card_photos(table: Table, row: int, fields: tuple) -> int:
    """I 段的两张全景照片。库里没有桥梁总体照和正面照，格子留空但留出高度。"""
    table.rows[row].height_rule = WD_ROW_HEIGHT_RULE.AT_LEAST
    table.rows[row].height = BRIDGE_CARD_PHOTO_HEIGHT
    half = BRIDGE_CARD_GRID // 2
    for index, (number, label) in enumerate(fields):
        left = index * half
        _cell_text(table.cell(row, left), str(number), STYLE_CARD_HEADER)
        _cell_text(merge_across(table, row, left + 1, left + 4), label, STYLE_CARD_HEADER)
        _cell_text(merge_across(table, row, left + 5, left + half - 1), "", STYLE_CARD_CELL)
    return row + 1


def _bridge_card_values(context: ReportContext) -> dict[str, str]:
    """卡片里能从档案取到的格子，键即卡片上印的编号。

    取不到的一律不进这个字典，那一格就是空的（设计 §14 第 5 条：不编数据）。
    """
    scalars, profile = context.scalars, context.bridge_profile
    assessment = context.assessment
    values = {
        "1": scalars.get("route_code"),
        "2": scalars.get("route_name"),
        "4": profile.business_code,
        "5": scalars.get("bridge_name"),
        "6": profile.station_mark,
        "10": profile.design_load,
        "13": str(profile.built_year) if profile.built_year else None,
        "14": profile.design_org,
        "15": profile.construction_org,
        # 档案里只有一个管养单位，落在「养护单位」上。「管理单位」是另一个角色，留空。
        "19": profile.maintenance_org,
        "21": profile.supervision_org,
        "22": _decimal(profile.bridge_length_m),
        "23": _decimal(profile.bridge_width_m),
        "24": _decimal(profile.carriageway_width_m),
        "25": _decimal(profile.sidewalk_width_m),
        "38": profile.span_combination,
        "39": profile.bridge_type,
        # 档案只存一条上部结构形式，它说的是主梁；主拱圈、索塔这些另有其事，不拿它填。
        "40": profile.superstructure_form,
        "48": profile.deck_pavement,
        "49": profile.expansion_joint_type,
        "53": profile.abutment_form,
        "54": profile.pier_form,
        "57": profile.foundation_form,
        "59": profile.bearing_type,
        # F 段的第一条评定记录就是本次检查。处治对策和下次检测时间没有依据，留空。
        "75": scalars.get("inspection_date"),
        "76": INSPECTION_CATEGORY if assessment.has_formal_run else None,
        "77": assessment.overall_grade if assessment.has_formal_run else None,
        "96": scalars.get("report_date"),
    }
    return {key: value for key, value in values.items() if value}


def render_conclusion(inputs: BuildInputs, anchor: AnchorHit, into: BlockInserter) -> None:
    """第 5 章结论与建议（设计 §14）。措辞规则集中在 conclusion 模块里。"""
    _assessment_or_raise(inputs.context)
    for text in conclusion_paragraphs(inputs.context):
        into.paragraph(text, style=STYLE_BODY)


def _assessment_or_raise(context: ReportContext) -> ReportAssessment:
    """第 4、5 章和附表1 都以正式评定为前提，没有就中止。

    生成前检查已经拦过（设计 §16）；真走到这一步说明评定在生成期间被撤了，
    这时绝不能出一份没有评分的定期检测报告。
    """
    if not context.assessment.has_formal_run:
        raise ReportBuildError(
            code="report_assessment_missing",
            message="当前年度没有生效的正式评定，无法生成技术状况评定和结论。",
        )
    return context.assessment


def _weight(value: float | None) -> str:
    """权重按两位小数印。库里是 numeric(12,8)，原样印是 0.30000000。"""
    return "—" if value is None else f"{value:.2f}"


def _decimal(value: float | None) -> str | None:
    return None if value is None else f"{value:g}"


RENDERERS: dict[str, Renderer] = {
    "BRIDGE_PROFILE": render_bridge_profile,
    "COMPONENT_WEIGHTS": render_component_weights,
    "CONTROL_INDICATOR": render_control_indicator,
    "OVERALL_ASSESSMENT": render_overall_assessment,
    "DEFECT_TABLES": render_defect_tables,
    "DEFECT_PHOTOS": render_defect_photos,
    "PREVIOUS_COMPARISON": render_previous_comparison,
    "ASSESSMENT_RESULT": render_assessment_result,
    "ASSESSMENT_APPENDIX": render_assessment_appendix,
    "BRIDGE_CARD": render_bridge_card,
    "CONCLUSION": render_conclusion,
    "PERSONNEL_TABLE": render_personnel_table,
    "EQUIPMENT_LIST": render_equipment_list,
}

#: 契约要求、但装配规则还没实现的内容块。第 7 步之后为空——留着这条通路是为了将来
#: 加新内容块时，报错能说清楚"还没做"而不是"不认识"。
PENDING_BLOCKS: frozenset[str] = frozenset()


# --------------------------------------------------------------------------
# 装配主流程（设计 §18）
# --------------------------------------------------------------------------


def _replace_scalars(document: DocumentObject, context: ReportContext) -> list[str]:
    """替换全篇的标量占位符，含页眉页脚。

    人员标量 {{personnel.<role>.names}} 由角色分组拼出（设计 §8、§15.3）：模板不摆
    整张人员表时，签字页就靠它填。
    """
    values = {name: (value or "") for name, value in context.scalars.items()}
    by_role: dict[str, list[str]] = {}
    for person in context.personnel:
        by_role.setdefault(person.role_code, []).append(person.full_name)
    for role, names in by_role.items():
        values[personnel_placeholder(role)] = "、".join(names)
    for role in PERSONNEL_ROLE_LABELS:
        values.setdefault(personnel_placeholder(role), "")

    missing: list[str] = []
    # 容器和它的 python-docx 父对象要配对：页眉页脚是各自独立的 part，拿 document
    # 当父对象会让段落解析到错误的 part 上。
    containers: list[tuple[object, object]] = [(document.element.body, document)]
    for section in document.sections:
        for part in (
            section.header,
            section.footer,
            section.first_page_header,
            section.first_page_footer,
            section.even_page_header,
            section.even_page_footer,
        ):
            # linked_to_previous 的页眉没有自己的内容，跳过以免重复替换上一节的。
            if part is not None and not part.is_linked_to_previous:
                containers.append((part._element, part))
    for container, parent in containers:
        for paragraph in iter_paragraphs(container, parent):
            missing.extend(replace_placeholders(paragraph, values))
    return sorted(set(missing))


def _render_anchors(inputs: BuildInputs) -> list[str]:
    """按文档顺序装配内容块。

    每轮重新扫描：上一轮插进去的表格改变了段落序号，用第一轮的扫描结果去定位后面的
    锚点会指偏。锚点独占段落且不会被生成内容再次引入，所以这个循环一定收敛。
    """
    rendered: list[str] = []
    while True:
        scan = scan_document(inputs.document)
        remaining = [hit for hit in scan.anchors if hit.location == LOCATION_BODY]
        if not remaining:
            return rendered
        hit = remaining[0]

        renderer = RENDERERS.get(hit.block)
        if renderer is None:
            code = (
                "report_block_not_implemented"
                if hit.block in PENDING_BLOCKS
                else "report_block_unknown"
            )
            detail = (
                "装配规则尚未实现（设计 §27 第 7 步）"
                if hit.block in PENDING_BLOCKS
                else "契约不认识这个内容块"
            )
            raise ReportBuildError(
                code=code, message=f"内容锚点 {hit.raw} {detail}。"
            )

        paragraph = _paragraph_at(inputs.document, hit)
        into = BlockInserter(inputs.document, paragraph)
        renderer(inputs, hit, into)
        into.finish()
        rendered.append(hit.name)


def _remove_table_samples(document: DocumentObject) -> int:
    """删掉表头样表标记及其后面的样表。

    样表是给模板作者调版式用的，不是报告内容；留在交付文件里就是一张空表加一行
    `[[TABLE:...]]`。最终校验（设计 §20.2）也会查这个标记的残留。
    """
    body = document.element.body
    removed = 0
    for child in list(body.iterchildren()):
        if child.getparent() is None or not isinstance(child, CT_P):
            continue
        text = "".join(run.text for run in visible_runs(Paragraph(child, document)))
        if TABLE_MARKER_PREFIX not in text:
            continue
        following = child.getnext()
        body.remove(child)
        if following is not None and isinstance(following, CT_Tbl):
            table = following
            following = table.getnext()
            body.remove(table)
        # 样表之间垫着空段（两张表挨着会被 Word 合并），一并收走，
        # 否则报告末尾会多出一串空行。
        #
        # 但带分节符的段落绝不能碰：它除了 sectPr 什么都没有，看着也是"空段"，
        # 删掉正文就失去自己的页面设置、继承最后那个横向节——实测页数从 104 涨到 166。
        while following is not None and isinstance(following, CT_P):
            if following.xpath("./w:pPr/w:sectPr"):
                break
            if "".join(run.text for run in visible_runs(Paragraph(following, document))).strip():
                break
            spacer = following
            following = spacer.getnext()
            body.remove(spacer)
        removed += 1
    return removed


def _paragraph_at(document: DocumentObject, hit: AnchorHit) -> Paragraph:
    for index, paragraph in enumerate(iter_paragraphs(document.element.body, document)):
        if index == hit.paragraph_index:
            return paragraph
    raise ReportBuildError(
        code="report_anchor_lost",
        message=f"找不到内容锚点 {hit.raw} 所在的段落（正文 #{hit.paragraph_index}）。",
    )


def build_report(
    template_path: Path,
    context: ReportContext,
    output_path: Path,
    archive_root: Path,
) -> BuildResult:
    """把上下文装配进模板，产出中间 .docx（设计 §18 第 1-7 步的 1-5、7 步）。

    第 6 步「标记域需要刷新」和域更新本身在 field_updater 里，不在这里。
    """
    if template_path.resolve() == output_path.resolve():
        raise ReportBuildError(
            code="report_output_overwrites_template",
            message="输出路径与模板路径相同，会就地改坏模板。",
        )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    # 在任务临时目录复制模板再改（设计 §18 第 1 步）：模板本身是受控归档文件，
    # 任何一次生成都不能碰它。
    shutil.copyfile(template_path, output_path)

    document = Document(str(output_path))
    with TemporaryDirectory(prefix="bridge-report-photos-") as cache:
        inputs = BuildInputs(document, context, archive_root, Path(cache))
        missing = _replace_scalars(document, context)
        rendered = _render_anchors(inputs)
        # 样表已经被读进 inputs.samples，正文里不留它（设计 §7.7）。
        _remove_table_samples(document)
        # 图片在 save 之前必须还在盘上：python-docx 是保存时才读进包里的。
        document.save(str(output_path))

    return BuildResult(
        output_path=output_path, blocks_rendered=rendered, missing_placeholders=missing
    )
