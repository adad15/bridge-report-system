"""报告模板契约：`periodic_inspection_v1`。

契约规定一套模板必须提供哪些内容块、允许出现哪些占位符和 Word 域。模板可以改变
章节顺序、标题名称和分节方式（设计 §7.4），但不能少掉核心内容块。

将来若出现内容体系完全不同的报告，新增一个 ReportContract 实例，而不是在生成器里
按模板名称写分支（设计 §7.3）。
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Final


CONTRACT_PERIODIC_INSPECTION_V1: Final = "periodic_inspection_v1"

ANCHOR_PREFIX: Final = "[[REPORT:"
ANCHOR_SUFFIX: Final = "]]"
PLACEHOLDER_PREFIX: Final = "{{"
PLACEHOLDER_SUFFIX: Final = "}}"

#: 表头样表的标记。独占一段，紧随其后的表格就是该内容块的表头样表：
#: 模板作者在 Word 里拖列宽、改表头文字，生成器照着建表（设计 §7.7）。
TABLE_MARKER_PREFIX: Final = "[[TABLE:"
TABLE_MARKER_SUFFIX: Final = "]]"


def table_marker(block: str) -> str:
    return f"{TABLE_MARKER_PREFIX}{block}{TABLE_MARKER_SUFFIX}"


# 每个内容块的表格列。
#
# **列数与列序是契约，不是版式。** 模板作者删掉一列或调换两列，生成器照填不误——
# 「病害位置」的内容会印到「病害类型」下面，看着完全正常但全错。所以列数在这里定死，
# 样表对不上就拒绝启用模板。
#
# 这里的文字是默认表头；模板样表可以改写措辞（如「病害扣分」写成「扣分」），
# 但不能改列数。
DEFECT_TABLE_COLUMNS: Final = (
    "序号",
    "部件名称",
    "构件编号",
    "病害位置",
    "病害类型",
    "病害特征",
    "标度",
    "病害扣分",
    "构件评分",
    "照片编号",
)

#: 表4.1-1 部件权重计算表，照正式报告实测的列结构。
COMPONENT_WEIGHTS_COLUMNS: Final = (
    "部位", "序号", "名称", "权重", "重新分配后权重", "构件数量", "备注",
)

#: 表4.1-2 总体技术状况评定表，照正式报告实测的列结构。
ASSESSMENT_RESULT_COLUMNS: Final = (
    "结构",
    "类别",
    "评价部件",
    "构件数量",
    "构件评分",
    "桥梁部件技术状况评分",
    "桥梁结构技术状况评分",
    "桥梁结构组成权重",
    "等级",
    "桥梁总体技术状况评分",
    "综合评级",
)

PERSONNEL_TABLE_COLUMNS: Final = ("序号", "姓名", "单位", "职称", "资格证书编号", "职责")

EQUIPMENT_TABLE_COLUMNS: Final = (
    "序号",
    "设备名称",
    "型号规格",
    "资产编号",
    "量程",
    "精度",
    "检定证书编号",
    "检定有效期",
    "用途",
)

#: 内容块 -> 列定义。只有这里列出的块支持表头样表。
#:
#: 两张附录卡片都不在其中：附表1 是评定卡片（上半基本信息、中间十六个部件的等级、
#: 下半签署栏），附录2 是九段式的桥梁基本状况卡片，都不是"表头 + 数据行"的表，
#: 没有可调的表头行。
TABLE_COLUMNS: Final = {
    "DEFECT_TABLES": DEFECT_TABLE_COLUMNS,
    "COMPONENT_WEIGHTS": COMPONENT_WEIGHTS_COLUMNS,
    "ASSESSMENT_RESULT": ASSESSMENT_RESULT_COLUMNS,
    "PERSONNEL_TABLE": PERSONNEL_TABLE_COLUMNS,
    "EQUIPMENT_LIST": EQUIPMENT_TABLE_COLUMNS,
}


# 结构部位代码 -> 数据库 defect_observations.structure_part 的字面值。
#
# 顺序即报告里的输出顺序，与 frontend/src/archive/ComponentListPanel.tsx 的
# STRUCTURE_PART_ORDER 一致。两份正式报告的第 2 章都是按上部、下部、桥面系各起一节，
# 每节自带病害表、照片和与上次检查的对比，所以这三类内容块必须能按部位重复出现。
STRUCTURE_PARTS: Final = (
    ("SUPERSTRUCTURE", "上部结构"),
    ("SUBSTRUCTURE", "下部结构"),
    ("DECK", "桥面系"),
    ("WHOLE_BRIDGE", "全桥"),
    ("OTHER", "其他"),
)
STRUCTURE_PART_CODES: Final = frozenset(code for code, _ in STRUCTURE_PARTS)
STRUCTURE_PART_BY_CODE: Final = dict(STRUCTURE_PARTS)


def anchor_text(block: str, part: str | None = None) -> str:
    body = block if part is None else f"{block}:{part}"
    return f"{ANCHOR_PREFIX}{body}{ANCHOR_SUFFIX}"


def numbering_key(block: str, part: str | None = None) -> str:
    """编号格式的配置键。按部位重复的内容块每个部位各有一套编号（表2.1-、表2.2-…）。"""
    return block if part is None else f"{block}:{part}"


# 人员角色使用稳定代码（设计 §15.3）。模板配置声明本模板需要哪些角色，
# 年度报告页面据此动态显示必填项。
PERSONNEL_ROLES: Final = frozenset(
    {"approver", "reviewer", "lead_inspector", "compiler", "participant"}
)

#: 角色代码在报告里的写法。代码是稳定标识，中文只在这里出现一次。
PERSONNEL_ROLE_LABELS: Final = {
    "approver": "批准",
    "reviewer": "审核",
    "lead_inspector": "检测负责人",
    "compiler": "编制",
    "participant": "参加人员",
}


@dataclass(frozen=True)
class BridgeFigureSlot:
    """§1.1 的一个图件槽位在报告里怎么出。"""

    slot: str
    #: 编号前缀。同一个前缀共用一条序列：正式报告里地理位置图是图 1-1，
    #: 后面的示意图从 1-2 接着编；照片另起一条，从照片 1-1 开始。
    prefix: str
    #: 序列分组。图和示意图共用 "figure"，照片是 "photo"。
    sequence: str
    #: 题注里桥名后面跟的那一截。
    title_suffix: str


#: §1.1 图件的出图次序与写法，次序就是报告里的图号次序。
#:
#: 章号写死成 1：桥梁概况固定在第 1 章，模板契约里 BRIDGE_PROFILE 锚点就在 1.1。
BRIDGE_FIGURE_SLOTS: Final = (
    BridgeFigureSlot("LOCATION_MAP", "图", "figure", "地理位置图"),
    BridgeFigureSlot("LAYOUT_DRAWING", "示意图", "figure", "桥型布置图"),
    BridgeFigureSlot("CROSS_SECTION", "示意图", "figure", "横断面图"),
    BridgeFigureSlot("OVERVIEW_PHOTO", "照片", "photo", "全貌"),
    BridgeFigureSlot("DECK_PHOTO", "照片", "photo", "桥面"),
    BridgeFigureSlot("UNDERSIDE_PHOTO", "照片", "photo", "桥下"),
)

#: §1.1 所在的章号。
BRIDGE_FIGURE_CHAPTER: Final = 1


def personnel_placeholder(role: str) -> str:
    return f"personnel.{role}.names"


@dataclass(frozen=True)
class ReportContract:
    contract_type: str

    #: 缺少任意一个都不能启用模板——没有这些内容块的文档不构成一份定期检测报告。
    required_anchors: frozenset[str]
    #: 允许出现但不强制的内容块。
    optional_anchors: frozenset[str]
    #: 除人员标量外的已知标量占位符。未知占位符使模板校验失败（设计 §7.2）。
    base_placeholders: frozenset[str]
    personnel_roles: frozenset[str]
    #: 需要模板配置"表 2-{n}"这类编号格式的内容块（设计 §7.5）。
    numbered_blocks: frozenset[str]
    #: 必须写成 [[REPORT:BLOCK:PART]] 的内容块——正式报告按结构部位各起一节。
    per_part_blocks: frozenset[str]

    @property
    def all_anchors(self) -> frozenset[str]:
        return self.required_anchors | self.optional_anchors

    def is_per_part(self, block: str) -> bool:
        return block in self.per_part_blocks

    @property
    def all_placeholders(self) -> frozenset[str]:
        personnel = {personnel_placeholder(role) for role in self.personnel_roles}
        return self.base_placeholders | frozenset(personnel)


PERIODIC_INSPECTION_V1: Final = ReportContract(
    contract_type=CONTRACT_PERIODIC_INSPECTION_V1,
    # 第 4 章按正式报告拆成三节：4.1 综合评定（4.1.1 部件权重分配、4.1.2 技术状况
    # 等级）、4.2 单项控制指标、4.3 等级综合评定。四个内容块各占一节，缺一节报告
    # 就讲不完评定过程。
    required_anchors=frozenset(
        {
            "BRIDGE_PROFILE",
            "DEFECT_TABLES",
            "DEFECT_PHOTOS",
            "PREVIOUS_COMPARISON",
            "COMPONENT_WEIGHTS",
            "ASSESSMENT_RESULT",
            "CONTROL_INDICATOR",
            "OVERALL_ASSESSMENT",
            "CONCLUSION",
            "ASSESSMENT_APPENDIX",
            "BRIDGE_CARD",
        }
    ),
    # 人员表和设备表可选：签字页也可以只用 {{personnel.*.names}} 标量填写（设计 §8），
    # 模板不摆整张表并不影响报告成立。
    optional_anchors=frozenset({"PERSONNEL_TABLE", "EQUIPMENT_LIST"}),
    # 前 8 个来自设计 §7.2；后 4 个是照两份正式报告的封面和页眉补的，各有数据库来源：
    # inspection_year / project_name / inspection_org 取自 inspection_years，
    # report_date 由 ReportContext 在构造时一次性写定（§5.4 的一次生成内部一致）。
    base_placeholders=frozenset(
        {
            "report_no",
            "bridge_name",
            "route_code",
            "route_name",
            "administrative_region",
            "inspection_date",
            "overall_grade",
            "comparison_year",
            "inspection_year",
            "report_date",
            "project_name",
            "inspection_org",
        }
    ),
    personnel_roles=PERSONNEL_ROLES,
    # 照片也在这里：报告里的图号按模板结构现编，与库里存的 defect_photos.photo_number
    # 无关（设计 §7.5「照片编号：报告号与系统号是两回事」）。库里那些 2.1-1 是导入器
    # 为填满契约必填字段现编的，且发号顺序按病害 UUID，与构件台账顺序完全对不上；
    # 而且 2.1 的含义是"第 2 章第 1 节"，模板一旦按 §7.4 重排章节就必然错位。
    numbered_blocks=frozenset(
        {
            "DEFECT_TABLES",
            "DEFECT_PHOTOS",
            "COMPONENT_WEIGHTS",
            "ASSESSMENT_RESULT",
            "ASSESSMENT_APPENDIX",
        }
    ),
    per_part_blocks=frozenset({"DEFECT_TABLES", "DEFECT_PHOTOS", "PREVIOUS_COMPARISON"}),
)


CONTRACTS: Final = {PERIODIC_INSPECTION_V1.contract_type: PERIODIC_INSPECTION_V1}


def get_contract(contract_type: str) -> ReportContract:
    try:
        return CONTRACTS[contract_type]
    except KeyError:
        raise KeyError(f"未知的模板契约类型：{contract_type}") from None


# Word 域约束（设计 §7.6、§20.6）。
#
# 表号、图号、正文引用和动态页眉文字一律由生成器确定性写入，不借 Word 域计算，
# 因此除目录和页码外的域全部拒绝——这些域会把历史文档的编号漂移带进新报告。
FIELDS_ALLOWED_ANYWHERE: Final = frozenset({"TOC", "PAGE", "NUMPAGES"})

#: 只允许出现在 TOC 域结果内部：由 Word/WPS 更新目录时自动生成，不是模板作者写的。
FIELDS_ALLOWED_INSIDE_TOC: Final = frozenset({"PAGEREF", "HYPERLINK"})

#: 明确点名拒绝的域，用于给出比"不在白名单里"更具体的错误信息。
FIELDS_EXPLICITLY_REJECTED: Final = frozenset({"SEQ", "REF", "STYLEREF"})
