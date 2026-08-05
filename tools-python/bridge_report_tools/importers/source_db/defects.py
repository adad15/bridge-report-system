"""把来源库的病害记录组装成契约里的病害候选。

只做能在解析阶段确定的事：构件认领、尺寸拼装、标度搬运、指标换算。
**不选评定树节点**——那要看桥型与构件类别，是后端在导入落库时的上下文，
而且构件重新绑定后还会重算，解析器越权只会产生一份很快作废的结果。
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from bridge_report_tools.importers.source_db.reader import ComponentNode, SourceDefect

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


def h21_indicator_id(index_code: str) -> str:
    """`5.1.1-2` → `h21.defect.5_1_1_2`。"""
    return "h21.defect." + index_code.replace(".", "_").replace("-", "_")


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


def _measurements(defect: SourceDefect) -> list[dict[str, Any]]:
    """尺寸在来源库里已经拆成数值列与单位列，直接搬，不回头解析文字。"""
    measurements = []
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
    return measurements


def build_defect_candidates(
    defects: list[SourceDefect],
    tree: list[ComponentNode],
    indicator_codes: dict[str, tuple[str, str]],
    h21_indicator_ids: set[str],
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

        # H21 里没有的编号一律留空。硬凑一个不存在的指标只会让下游拿着它查不到东西，
        # 而这些病害的类型文字（如「渗水泛碱」）本来就能走别名匹配。
        code, _ = indicator_codes.get(defect.judge_index_id, ("", ""))
        indicator = h21_indicator_id(code) if code else ""
        standard_indicator = indicator if indicator in h21_indicator_ids else None

        candidate_id = f"source_defect_{position:04d}"
        links[defect.id] = DefectLink(candidate_id, defect.tree_id)
        candidates.append({
            "candidate_id": candidate_id,
            "component_name": component_name,
            "component_number": component_number,
            "defect_type": defect.name,
            "defect_location": defect.position,
            "defect_description": defect.description,
            "defect_scale": defect.degree,
            "measurements": _measurements(defect),
            "measurement_text": defect.description or None,
            "standard_defect_indicator_id": standard_indicator,
            # 选节点是后端的活：要看桥型与构件类别，且构件重绑后会重算。
            "rating_tree_version_id": None,
            "rating_tree_node_id": None,
            "rating_tree_match_method": None,
            "rating_tree_match_evidence": None,
            "photo_references": [],
            "group_review_status": "待确认",
            "remark": SOURCE_REMARK,
            "source_ref": {"source_type": SOURCE_TYPE},
            "confidence": 1.0,
            "review_status": "待确认",
            "warnings": warnings,
        })
    return candidates, links
