"""第 5 章结论与历史对比的确定性文字规则（设计 §12.2、§14）。

第一版不使用自由文本生成。措辞模板集中在这里，输入、排序和结果都可测试：同一份
`ReportContext` 不得因为运行时间或外部模型不同产生另一套结论。

这里只说数据支持的话。设计 §14 第 5 条点名禁止的四类内容——维修工程量、预算、工期、
结构安全结论——不在任何模板里，因为数据库里没有它们的依据。
"""

from __future__ import annotations

from decimal import ROUND_HALF_UP, Decimal

from bridge_report_tools.reports.report_context import (
    ReportContext,
    ReportStructurePart,
)


#: 历史对比的能力名称（设计 §12.2）。换成 confirmed_defect_comparison_v2 时，
#: 措辞和这条免责说明一并替换。
COMPARISON_CAPABILITY = "source_defect_count_delta_v1"

#: 必须随对比结论一起出现的说明（设计 §12.2）。条数变化不能证明病害身份关系，
#: 少了这句话，读者会把"增加 5 条"读成"新增 5 处病害"。
COMPARISON_CAVEAT = (
    "当前仅按来源病害行比较两次检查的记录条数；构件范围拆分只计一次。"
    "本结果不代表新增、消失或修复了相应病害。"
)

NO_COMPARISON_TEXT = "暂无可供对比的历史正式检查记录。"

#: 按等级给出的养护建议，即设计 §14 第 4 条的"受控规则文本"。
#:
#: 只说采取哪一类养护措施，不写工程量、预算、工期，也不下结构安全结论——那些
#: 数据库里没有依据（设计 §14 第 5 条）。措辞要改就改这张表，不散在代码里。
MAINTENANCE_ADVICE = {
    "1类": "技术状况良好，按日常保养要求进行养护。",
    "2类": "存在轻微缺损，不影响正常使用，建议按小修保养处理。",
    "3类": "存在中等缺损，尚能维持正常使用，建议安排中修。",
    "4类": "主要构件存在较大缺损，明显影响使用功能，建议安排大修。",
    "5类": "主要构件存在严重缺损，建议按危桥处置程序进一步核查并采取措施。",
}

#: 结论里点名的主要扣分病害条数。取固定条数而不是"扣分超过某值"，是为了让不同
#: 桥梁的结论长度可预期。
TOP_DEDUCTION_COUNT = 3

#: 各等级的状态描述，措辞照《公路桥梁技术状况评定标准》(JTG/T H21-2011) 的等级定义。
#: 第 4.3 节和第 5 章都引它，两处必须是同一句话。
GRADE_DESCRIPTION = {
    "1类": "完好状态",
    "2类": "有轻微缺损，对桥梁使用功能无影响",
    "3类": "有中等缺损，尚能维持正常使用",
    "4类": "主要构件有大的缺损，严重影响桥梁使用功能",
    "5类": "主要构件有严重缺损，桥梁处于危险状态",
}

#: 规范全称。全篇引用同一个写法，不能这一节写 JTG/TH21-2011、那一节写 JTG/T H21-2011。
STANDARD_CITATION = "《公路桥梁技术状况评定标准》（JTG/TH21-2011）"

#: 4.1.1 部件权重分配的引语。表号由生成器按模板配置填入，不写死在文字里——
#: 模板改了编号格式，正文里的引用要跟着变（设计 §7.5、§7.6）。
COMPONENT_WEIGHTS_INTRO = (
    f"按照{STANDARD_CITATION}中的评定方法，根据该桥的结构型式，"
    "将桥梁各部件权重值重新分配，见{number}所列。"
)

#: 4.1.2 桥梁技术状况等级的引语。
ASSESSMENT_RESULT_INTRO = (
    f"按照{STANDARD_CITATION}中的评定方法，桥梁综合评定等级结果见{{number}}所示。"
)

#: 单项控制指标未触发时的固定说法（H21 4.3）。
NO_CONTROL_INDICATOR_TEXT = (
    f"根据{STANDARD_CITATION}4.3 的规定"
    "（5 类桥梁单项控制指标），该桥不符合 4.3.1 的所有规定，"
    "不能采用单项控制指标来评定。"
)

#: 某个评价部件没有记录到病害时的说法。只说"未见明显病害"，不说"构件完好"——
#: 后者是评定结论，不是检查记录（设计 §10.3）。
NO_DEFECT_SENTENCE = "{name}状况良好，未见明显病害。"

#: 附录2 卡片「检测类别」一栏对本次检查的定性。本契约出的就是定期检测报告，
#: 特殊检查、经常检查另有其表，不会走到这里。
INSPECTION_CATEGORY = "定期检查"

#: 概要里不点名的兜底病害类型。「其它病害」是个标签，写进"查到了哪些病害"等于
#: 什么都没说；但它只在该部件还有别的类型时才滤掉——某个部件只有这一类时仍要列出，
#: 否则概要会说成"未见明显病害"，而实际记录着病害（河床、照明标志各有一条）。
SUMMARY_CATCH_ALL_TYPES = frozenset({"其它病害"})


def grade_phrase(grade: str | None) -> str:
    """「评定为 2类，处于"有轻微缺损，对桥梁使用功能无影响"」里的那半句。

    等级不认识时只说等级，不编描述。
    """
    if not grade:
        return ""
    description = GRADE_DESCRIPTION.get(grade)
    if description is None:
        return f"评定为 {grade}"
    return f"评定为 {grade}，处于“{description}”"


def control_indicator_paragraphs(context: ReportContext) -> list[str]:
    """4.2 桥梁技术状况等级单项控制指标。

    没触发就是没触发，照 H21 4.3 说清楚"不能采用单项控制指标来评定"；触发了就把
    评定引擎记下来的那几条原样列出，不改写、不归纳。
    """
    controls = context.assessment.triggered_controls
    if not controls:
        return [NO_CONTROL_INDICATOR_TEXT]
    lines = [
        "根据《公路桥梁技术状况评定标准》（JTG/T H21-2011）4.3 的规定，"
        "该桥符合下列单项控制指标："
    ]
    lines.extend(
        item.message + (f"（评定为 {item.grade_after}）" if item.grade_after else "")
        for item in controls
    )
    return lines


def overall_assessment_paragraphs(context: ReportContext) -> list[str]:
    """4.3 桥梁技术状况等级综合评定。"""
    assessment = context.assessment
    bridge = context.scalars.get("bridge_name") or "本桥"
    phrase = grade_phrase(assessment.overall_grade)
    return [
        f"综合该桥技术状况评分及单项控制指标，{bridge}总体技术状况评分为 "
        f"{format_score(assessment.overall_score)} 分"
        + (f"，{phrase}。" if phrase else "。")
    ]


def assessment_summary_sentence(context: ReportContext) -> str:
    """紧跟 表4.1-2 的那句话：由以上评定过程可知，该桥评定为 X 类，处于"…"。"""
    phrase = grade_phrase(context.assessment.overall_grade)
    return f"由以上评定过程可知，该桥{phrase}。" if phrase else "由以上评定过程可知。"


def format_score(value: float | None) -> str:
    """评分保留一位小数，按四舍五入。

    库里是 numeric(12,6)，原样印会出现 88.123456 这种数字。不能用 f"{v:.1f}"：
    Python 走的是"四舍六入五成双"，78.25 会印成 78.2，而工程报告里 78.25 应当是
    78.3。差的这 0.1 分可能正好跨过等级分界线。
    """
    if value is None:
        return "—"
    return str(Decimal(str(value)).quantize(Decimal("0.1"), rounding=ROUND_HALF_UP))


def defect_summary_lines(
    context: ReportContext, part: ReportStructurePart
) -> list[str]:
    """病害检查表前的概要：本结构部位每个评价部件各出现了哪些病害。

    一行一个评价部件，顺序跟规范原表走（上部承重构件、上部一般构件、支座……），
    **没有病害的部件也要列出来**——「支座状况良好，未见明显病害」本身就是检查结论，
    漏掉它读者不知道支座查没查过。

    病害类型按在病害表里首次出现的次序去重，与表格顺序一致，重复生成结果相同。
    部件清单取自 表4.1-1 的权重表；拿不到（没有规范包）时返回空，由调用方决定
    退回什么。
    """
    categories = [
        row
        for row in context.assessment.component_weights
        if row.part_code == part.part_code and row.present
    ]
    if not categories:
        return []

    types_by_category: dict[str, list[str]] = {}
    for row in part.defect_rows:
        if not row.part_name:
            continue
        seen = types_by_category.setdefault(row.part_name, [])
        if row.defect_type not in seen:
            seen.append(row.defect_type)

    lines: list[str] = []
    for category in categories:
        name = category.category_name or category.category_id
        found = types_by_category.get(name, [])
        named = [kind for kind in found if kind not in SUMMARY_CATCH_ALL_TYPES]
        # 全是兜底类型时保留原样：滤空了就会说成"未见明显病害"，而病害确实记着。
        shown = named or found
        if shown:
            lines.append(f"{name}：{'、'.join(shown)}。")
        else:
            lines.append(f"{name}：" + NO_DEFECT_SENTENCE.format(name=name))
    return lines


def _measure(value: float | int | None) -> str | None:
    """量值去掉无意义的尾零：664.60 印成 664.6，90.00 印成 90。"""
    if value is None:
        return None
    return f"{float(value):g}"


def _text(value: str | None) -> str | None:
    """空白等于没填。清空输入框留下的那个空格不该让报告印出半截话。"""
    if value is None:
        return None
    stripped = value.strip()
    return stripped or None


def _sentence(clauses: list[str]) -> str | None:
    """把present的分句拼成一句话。一句都没有就整句不出。"""
    return "，".join(clauses) + "。" if clauses else None


def bridge_profile_paragraphs(context: ReportContext) -> list[str]:
    """§1.1「桥梁概况」的叙述文字（设计 §8 第 1 章）。

    正式报告里这一节是几段话，不是一张两列表——两列表在附录2 的卡片里已经有了，
    §1.1 再摆一张只是把同一批数印两遍。

    **缺哪一项就少写哪一句。** 每个分句都自带它依赖的那几项，凑不齐就整句不出；
    一句都凑不齐就整段不出。绝不用「未知」「/」占位，也绝不把半截话印出去
    （设计 §14 第 5 条）。
    """
    profile = context.bridge_profile
    scalars = context.scalars
    name = _text(scalars.get("bridge_name")) or "本桥"

    # ---- 第一段：位置、规模、构造 ---------------------------------------
    sentences: list[str] = []

    route_name = _text(scalars.get("route_name"))
    route_code = _text(scalars.get("route_code"))
    region = _text(scalars.get("administrative_region"))
    station = _text(profile.station_mark)
    where = ""
    if route_name:
        where = f"{route_name}公路"
        if route_code:
            where += f"（{route_code}）"
    if region:
        where += f"{region}段"
    location_clauses: list[str] = []
    if where and station:
        location_clauses.append(f"{name}位于{where} {station} 处")
    elif where:
        location_clauses.append(f"{name}位于{where}")
    elif station:
        location_clauses.append(f"{name}中心桩号为 {station}")
    if profile.built_year:
        location_clauses.append(f"建成于 {profile.built_year} 年")
    if location_clauses:
        sentences.append(_sentence(location_clauses) or "")

    scale_clauses: list[str] = []
    if _text(profile.span_combination):
        scale_clauses.append(f"跨径布置为 {_text(profile.span_combination)}")
    if _measure(profile.bridge_length_m):
        scale_clauses.append(f"桥梁全长为 {_measure(profile.bridge_length_m)}m")
    if _measure(profile.skew_angle_deg):
        scale_clauses.append(f"斜交角为 {_measure(profile.skew_angle_deg)}°")
    if _text(profile.bridge_scale):
        scale_clauses.append(f"属{_text(profile.bridge_scale)}")
    if scale_clauses:
        sentences.append(_sentence(scale_clauses) or "")

    width_clauses: list[str] = []
    if _measure(profile.carriageway_width_m):
        width_clauses.append(f"桥面净宽为 {_measure(profile.carriageway_width_m)}m")
    if _measure(profile.sidewalk_width_m):
        width_clauses.append(
            f"左、右侧各设置 {_measure(profile.sidewalk_width_m)}m 的人行道"
        )
    if width_clauses:
        sentences.append(_sentence(width_clauses) or "")

    deck_clauses: list[str] = []
    if _text(profile.deck_pavement):
        deck_clauses.append(f"桥面铺装采用{_text(profile.deck_pavement)}")
    joint_type = _text(profile.expansion_joint_type)
    joint_piers = _text(profile.expansion_joint_piers)
    if joint_type and joint_piers:
        deck_clauses.append(f"{joint_piers} 号墩顶设{joint_type}")
    elif joint_type:
        deck_clauses.append(f"伸缩缝为{joint_type}")
    if _text(profile.bearing_type):
        deck_clauses.append(f"支座为{_text(profile.bearing_type)}")
    if deck_clauses:
        sentences.append(_sentence(deck_clauses) or "")

    # 上下部结构用分号隔开，与正式报告一致：这是两件并列的事，不是四个并列分句。
    upper_clauses: list[str] = []
    if _text(profile.superstructure_form):
        upper_clauses.append(f"上部结构为{_text(profile.superstructure_form)}")
    if profile.girders_per_span:
        upper_clauses.append(f"每孔 {profile.girders_per_span} 片")
    if _measure(profile.girder_height_m):
        upper_clauses.append(f"梁高 {_measure(profile.girder_height_m)}m")
    lower_parts = [
        _text(profile.abutment_form),
        _text(profile.pier_form),
        _text(profile.foundation_form),
    ]
    lower = "，".join(part for part in lower_parts if part)
    lower_clauses = [f"下部结构为{lower}"] if lower else []
    if upper_clauses and lower_clauses:
        sentences.append("，".join(upper_clauses) + "；" + lower_clauses[0] + "。")
    elif upper_clauses:
        sentences.append(_sentence(upper_clauses) or "")
    elif lower_clauses:
        sentences.append(_sentence(lower_clauses) or "")

    if _text(profile.design_load):
        sentences.append(f"设计荷载为{_text(profile.design_load)}。")

    paragraphs = ["".join(sentences)] if sentences else []

    # ---- 第二段：参建与管理单位 -----------------------------------------
    org_clauses: list[str] = []
    for label, value in (
        ("设计单位", profile.design_org),
        ("施工单位", profile.construction_org),
        ("管养单位", profile.maintenance_org),
        ("监管单位", profile.supervision_org),
    ):
        if _text(value):
            org_clauses.append(f"{label}为{_text(value)}")
    if org_clauses:
        paragraphs.append(f"该桥{'，'.join(org_clauses)}。")

    return paragraphs


def comparison_paragraphs(
    part: ReportStructurePart, comparison_year: str | None
) -> list[str]:
    """某个结构部位的历史对比结论（设计 §12.2）。

    没有可对比的历史检查时输出固定说明，不省略这一块——模板里留着的小节标题
    下面必须有话，否则读者以为内容漏掉了。
    """
    comparison = part.comparison
    if not comparison.has_previous:
        return [NO_COMPARISON_TEXT]

    year = f"{comparison_year} 年" if comparison_year else "所选历史检查"
    delta = comparison.delta
    if delta > 0:
        change = f"较所选检查记录增加 {delta} 条"
    elif delta < 0:
        change = f"较所选检查记录减少 {abs(delta)} 条"
    else:
        change = "与所选检查记录持平"

    return [
        f"{year}{part.part_label}共记录病害 "
        f"{comparison.previous_source_defect_count} 条，本次检查共记录 "
        f"{comparison.current_source_defect_count} 条，{change}。",
        COMPARISON_CAVEAT,
    ]


def conclusion_paragraphs(context: ReportContext) -> list[str]:
    """第 5 章的结论与建议（设计 §14）。

    四段固定顺序：全桥等级、各结构评定、主要扣分病害、养护建议。缺依据的段落
    直接不出，不用"无"或"暂无数据"占位——报告里的每一句都要有出处。
    """
    assessment = context.assessment
    paragraphs: list[str] = []

    grade = assessment.overall_grade
    paragraphs.append(
        f"本次检查，{context.scalars.get('bridge_name') or '本桥'}全桥技术状况评分 "
        f"{format_score(assessment.overall_score)} 分"
        + (f"，评定为 {grade}。" if grade else "。")
    )

    if assessment.parts:
        details = "；".join(
            f"{part.part_label} {format_score(part.score)} 分"
            + (f"（{part.grade}）" if part.grade else "")
            for part in assessment.parts
        )
        paragraphs.append(f"各结构技术状况评定结果：{details}。")

    top = assessment.top_deductions[:TOP_DEDUCTION_COUNT]
    if top:
        items = "；".join(
            f"{item.component_number or '未编号构件'}{item.defect_type}"
            f"（扣 {format_score(item.deduction)} 分）"
            for item in top
        )
        paragraphs.append(f"对评分影响较大的病害为：{items}。")

    advice = MAINTENANCE_ADVICE.get(grade or "")
    if advice:
        paragraphs.append(advice)

    return paragraphs
