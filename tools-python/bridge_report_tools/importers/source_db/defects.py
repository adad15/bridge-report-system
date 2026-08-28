"""把来源库的病害记录组装成契约里的病害候选。

只做能在解析阶段确定的事：构件认领、尺寸拼装、标度搬运、来源身份搬运。
**不选评定树节点**——那要看桥型与构件类别，是后端在导入落库时的上下文，
而且构件重新绑定后还会重算，解析器越权只会产生一份很快作废的结果。
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from bridge_report_tools.importers.source_db.reader import ComponentNode, SourceDefect
from bridge_report_tools.importers.measurements import normalize_unit, parse_measurements

#: 来源软件的尺寸列名 → 契约里的维度名。两边用词一致，保持原样便于对账。
DIMENSION_TYPES = {
    "数量": "数量",
    "长度": "长度",
    "宽度": "宽度",
    "面积一": "面积",
}

#: 契约的 source_ref.source_type 只有 word / manual。源库导入两者都不贴切，
#: 取 word 表示"来自导入"——写 manual 会让这些病害被当成人工新增的。
SOURCE_TYPE = "word"
SOURCE_REMARK = "由来源软件离线库导入"


@dataclass(frozen=True)
class DefectLink:
    """源病害 id → 契约候选 id 与来源构件节点 id。

    照片是按源病害 id 绑定的，而契约候选另有自己的 id；不显式记下这层对应，
    照片就永远挂不上去。
    """

    candidate_id: str
    tree_id: str


def resolve_component(tree: list[ComponentNode], tree_id: str) -> ComponentNode | None:
    for node in tree:
        if node.id == tree_id:
            return node
    return None


def _warning(code: str, message: str) -> dict[str, str]:
    return {"code": code, "message": message, "severity": "warning"}


def _component_name(
    component: ComponentNode,
    by_level: dict[str, ComponentNode],
    category_names: dict[str, str] | None,
) -> tuple[str, list[dict[str, str]]]:
    """契约要求 component_name 非空，取不到就沿回退链走。

    后端校验对这个字段用的是 require_non_empty_string，空串会让**整份契约**校验失败、
    整次导入报错。所以宁可退到一个不够准确但非空的值，并带上警告让人复核。
    """
    warnings: list[dict[str, str]] = []
    parent = by_level.get(component.parent_level_code)
    if parent is not None:
        name = parent.name
        if category_names is not None:
            name = category_names.get(component.parent_level_code, name)
        if name:
            return name, warnings
    warnings.append(_warning(
        "source_component_category_missing",
        f"构件 {component.name} 在来源库里取不到所属构件类别，已按回退规则填写，请复核。"))
    if component.member_type_name:
        return component.member_type_name, warnings
    part = by_level.get(component.level_code[:3])
    if part is not None and part.name:
        return part.name, warnings
    return component.name, warnings


def _same_measurement(
    source: dict[str, Any], parsed: dict[str, Any],
) -> bool:
    """判断来源结构化值与描述文本中的值是否表达同一个尺寸。"""
    if source["value_type"] != parsed["value_type"]:
        return False
    if normalize_unit(source["unit"]) != normalize_unit(parsed["unit"]):
        return False
    if source["value_type"] == "single":
        return source["value"] == parsed["value"]
    return (
        source["minimum_value"] == parsed["minimum_value"]
        and source["maximum_value"] == parsed["maximum_value"]
    )


def _dimension_key(dimension_type: str) -> str:
    """合并时把来源列“面积”和描述解析出的“总面积”视为同一维度。"""
    return "面积" if dimension_type == "总面积" else dimension_type


def _measurements(
    defect: SourceDefect, candidate_id: str,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """优先搬来源结构化列，再用病害描述补齐缺失的尺寸维度。"""
    measurements: list[dict[str, Any]] = []
    for column, dimension_type in DIMENSION_TYPES.items():
        if column not in defect.dimensions:
            continue
        raw, unit = defect.dimensions[column]
        try:
            value = float(raw)
        except ValueError:
            continue
        measurements.append({
            "dimension_type": dimension_type,
            "value_type": "single",
            "value": value,
            "minimum_value": None,
            "maximum_value": None,
            "unit": unit,
            "is_approximate": False,
            "source_text": f"{raw}{unit}",
        })

    parsed_measurements, parse_warnings = parse_measurements(
        defect.description or None, candidate_id)
    warnings = [warning.model_dump() for warning in parse_warnings]

    source_by_dimension = {
        _dimension_key(measurement["dimension_type"]): measurement
        for measurement in measurements
    }
    conflicted_dimensions: set[str] = set()
    for parsed_model in parsed_measurements:
        parsed = parsed_model.model_dump()
        dimension_type = parsed["dimension_type"]
        dimension_key = _dimension_key(dimension_type)
        source = source_by_dimension.get(dimension_key)
        if source is None:
            measurements.append(parsed)
            continue
        if (
            _same_measurement(source, parsed)
            or dimension_key in conflicted_dimensions
        ):
            continue
        conflicted_dimensions.add(dimension_key)
        warnings.append({
            "code": "source_measurement_conflict",
            "message": (
                f"来源结构化{dimension_type}与病害描述中的尺寸表达不一致，"
                "已保留来源结构化值，请人工复核。"
            ),
            "severity": "warning",
            "target_candidate_id": candidate_id,
        })

    return measurements, warnings


def build_defect_candidates(
    defects: list[SourceDefect],
    tree: list[ComponentNode],
    group_codes: dict[str, tuple[str, str]],
    indicator_codes: dict[str, tuple[str, str]],
    category_names: dict[str, str] | None = None,
) -> tuple[list[dict[str, Any]], dict[str, DefectLink]]:
    """返回 (病害候选, 源病害 id → DefectLink)。

    映射按**源病害 id** 索引，因为照片就是按它绑定的。构件节点 id 一并带上，
    但两者都不属于契约——契约模型是 extra="forbid"，往候选字典里塞私有键会让整份
    数据校验失败，所以单独返回。
    """
    by_level = {node.level_code: node for node in tree if node.level_code}
    candidates: list[dict[str, Any]] = []
    links: dict[str, DefectLink] = {}
    for position, defect in enumerate(defects, start=1):
        warnings: list[dict[str, str]] = []
        component = resolve_component(tree, defect.tree_id)
        if component is None:
            component_number = None
            component_name = "未知构件"
            warnings.append(_warning(
                "source_component_not_found",
                f"病害 {defect.id} 指向的构件在来源库里不存在，请人工指定构件。"))
        else:
            component_number = component.name
            component_name, name_warnings = _component_name(component, by_level, category_names)
            warnings.extend(name_warnings)

        group_number, _ = group_codes.get(defect.judge_tree_id, ("", ""))
        indicator_number, _ = indicator_codes.get(defect.judge_index_id, ("", ""))

        candidate_id = f"source_defect_{position:04d}"
        measurements, measurement_warnings = _measurements(defect, candidate_id)
        warnings.extend(measurement_warnings)
        links[defect.id] = DefectLink(candidate_id, defect.tree_id)
        candidates.append({
            "candidate_id": candidate_id,
            "component_name": component_name,
            "component_number": component_number,
            "defect_type": defect.name,
            "defect_location": defect.position,
            "defect_description": defect.description,
            "defect_scale": defect.degree,
            "measurements": measurements,
            "measurement_text": defect.description or None,
            "source_defect_group_id": defect.judge_tree_id or None,
            "source_defect_group_number": group_number or None,
            "source_defect_indicator_id": defect.judge_index_id or None,
            "source_defect_indicator_number": indicator_number or None,
            # 选节点和它派生的标准指标都是后端的活：要看桥型与构件类别，且构件重绑
            # 会重算。5.0 之后它们连同构件解析一起存在关系表里，解析结果不再回到这份
            # JSON；解析器只报来源怎么标的。
            "photo_references": [],
            "group_review_status": "待确认",
            "remark": SOURCE_REMARK,
            "source_ref": {"source_type": SOURCE_TYPE},
            "confidence": 1.0,
            "review_status": "待确认",
            "warnings": warnings,
        })
    return candidates, links
