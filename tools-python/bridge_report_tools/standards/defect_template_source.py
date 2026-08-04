"""从来源软件的本机离线库导出「构件分组 → 评定指标 → 病害描述模板」三元组。

来源软件（bridge.ilis.cn 的桌面壳）把整套规则库下载到了本机，模板与评定指标的对应
关系在那里是已确定的事实。本模块只读取，不写回。
"""

from __future__ import annotations

import json
import sqlite3
from dataclasses import dataclass, field
from pathlib import Path


@dataclass(frozen=True)
class SourceTriple:
    group_code: str
    group_name: str
    #: judgeIndex.id。真实库里 tableNum 会重复（5.1.1-13 是两个不同指标，
    #: 1.3.1 重复 24 次），只有这个是唯一键。
    index_id: str
    index_code: str
    index_name: str
    template_name: str

    @property
    def sort_key(self) -> tuple[str, ...]:
        return (self.group_code, self.index_code, self.template_name, self.index_id)


@dataclass
class Classified:
    """三档：a 直接对上 H21、b 单位扩展项、c 别的标准。"""

    mapped: list[SourceTriple] = field(default_factory=list)
    extensions: list[SourceTriple] = field(default_factory=list)
    foreign: list[SourceTriple] = field(default_factory=list)

    @property
    def counts(self) -> dict[str, int]:
        return {
            "mapped": len(self.mapped),
            "extensions": len(self.extensions),
            "foreign": len(self.foreign),
        }

    @property
    def extension_indices(self) -> list[tuple[str, str, str]]:
        """b 档需要人工对表的指标实例，按 id 去重。"""
        seen = {(t.index_id, t.index_code, t.index_name) for t in self.extensions}
        return sorted(seen)


_TRIPLE_QUERY = """
select distinct
    tree.chapterNum, tree.name,
    idx.id, idx.tableNum, idx.name,
    template.name
from judgeTree2Index link
join judgeTree tree on tree.id = link.treeId
join judgeIndex idx on idx.id = link.indexId
join judgeIndex2diseaseType relation on relation.indexId = idx.id
join sysConfig template
  on template.id = relation.diseaseTypeId and template.catag = 'diseaseType'
"""


def load_source_triples(db_path: str | Path) -> list[SourceTriple]:
    """只读打开来源库，返回排序稳定的三元组列表。"""
    uri = f"file:{Path(db_path).as_posix()}?mode=ro"
    connection = sqlite3.connect(uri, uri=True)
    try:
        rows = connection.execute(_TRIPLE_QUERY).fetchall()
    finally:
        connection.close()
    triples = {SourceTriple(*(str(value) for value in row)) for row in rows}
    return sorted(triples, key=lambda triple: triple.sort_key)


def h21_indicator_id(index_code: str) -> str:
    """`5.1.1-2` → `h21.defect.5_1_1_2`。"""
    return "h21.defect." + index_code.replace(".", "_").replace("-", "_")


def h21_chapter(index_code: str) -> str:
    """`5.1.1-2` → `5_1_1`；没有序号后缀时返回整段。"""
    head = index_code.split("-", 1)[0]
    return head.replace(".", "_")


def top_chapter(code: str) -> str:
    """`12.4-4` → `12`；取最前面那一级章节号。"""
    return code.split(".", 1)[0].split("-", 1)[0]


def classify(triples: list[SourceTriple], h21_indicator_ids: set[str]) -> Classified:
    """分三档：a 定检可机械映射、b 单位扩展项、c 别的标准。

    判断要同时看**指标编号**和**分组所属章节**。只看编号会漏掉这种情况：涵洞的
    `12.4-4 排水沟` 分组底下挂着桥梁栏杆的 `10.4.1-2 破损`——编号是 H21 的，但分组
    属于涵洞，不该按桥梁定检处理。

    b 档是同一章节里超出 H21 编号范围的条目——那是单位评定树自己扩的指标（水损、
    减震装置、各类"其它病害"）。它必须和 c 档区分开：c 档属于经常检查、隧道等别的
    标准，本期不做；b 档要靠一张小对表落到评定树节点上，丢掉就会损失最有价值的映射。
    """
    chapters = {identifier.rsplit("_", 1)[0].split(".")[-1] for identifier in h21_indicator_ids}
    inspection_chapters = {chapter.split("_", 1)[0] for chapter in chapters}
    result = Classified()
    for triple in triples:
        if top_chapter(triple.group_code) not in inspection_chapters:
            result.foreign.append(triple)
        elif h21_indicator_id(triple.index_code) in h21_indicator_ids:
            result.mapped.append(triple)
        elif h21_chapter(triple.index_code) in chapters:
            result.extensions.append(triple)
        else:
            result.foreign.append(triple)
    return result


def load_h21_indicator_ids(package_root: str | Path) -> set[str]:
    """读取 H21 规范包里全部病害指标 id。"""
    document = json.loads(
        (Path(package_root) / "defect-indicators.json").read_text(encoding="utf-8")
    )
    return {
        indicator["id"]
        for catalog in document["definitions"]
        for indicator in catalog.get("indicators", [])
    }


def load_rating_tree_nodes(package_root: str | Path) -> dict[str, list[dict]]:
    """按 h21_indicator_id 索引评定树的病害节点。"""
    document = json.loads((Path(package_root) / "tree.json").read_text(encoding="utf-8"))
    lookup: dict[str, list[dict]] = {}
    for node in document["nodes"]:
        indicator = node.get("h21_indicator_id")
        if indicator:
            lookup.setdefault(indicator, []).append(node)
    return lookup


def load_component_category_names(package_root: str | Path) -> dict[str, str]:
    document = json.loads(
        (Path(package_root) / "component-taxonomy.json").read_text(encoding="utf-8")
    )
    return {item["id"]: item.get("name", "") for item in document["definitions"]}


def build_component_map_template(
    mapped: list[SourceTriple],
    node_lookup: dict[str, list[dict]],
    category_names: dict[str, str],
) -> list[dict]:
    """生成「源构件分组 → H21 构件类别」的待填模板。

    建议值来自该分组下各指标所对应评定树节点的构件类别交集——交集为空时退回并集。
    建议与待填字段分开放：`component_category_ids` 始终留空，人工填写的才算数。
    """
    groups: dict[str, dict] = {}
    for triple in mapped:
        row = groups.setdefault(
            triple.group_code,
            {
                "group_code": triple.group_code,
                "group_name": triple.group_name,
                "indices": set(),
                "templates": set(),
                "_category_sets": [],
            },
        )
        row["indices"].add(f"{triple.index_code} {triple.index_name}")
        row["templates"].add(triple.template_name)
        categories = {
            category
            for node in node_lookup.get(h21_indicator_id(triple.index_code), [])
            for category in node.get("component_category_ids", [])
        }
        if categories:
            row["_category_sets"].append(categories)

    rows = []
    for row in sorted(groups.values(), key=lambda item: item["group_code"]):
        sets = row.pop("_category_sets")
        suggested = set.intersection(*sets) if sets else set()
        # 交集为空说明该分组下各指标的适用构件互不相同，只能给并集——这种建议偏宽，
        # 必须标出来，否则人工对表时会把一堆不相干的构件类别照单全收。
        basis = "各指标适用构件的交集"
        if not suggested and sets:
            suggested = set.union(*sets)
            basis = "交集为空，退回并集，偏宽需逐个核对"
        ordered = sorted(suggested)
        rows.append(
            {
                "group_code": row["group_code"],
                "group_name": row["group_name"],
                "index_count": len(row["indices"]),
                "template_count": len(row["templates"]),
                "indices": sorted(row["indices"]),
                "suggested_component_category_ids": ordered,
                "suggestion_names": [category_names.get(item, "") for item in ordered],
                "suggestion_basis": basis,
                "component_category_ids": [],
                "note": "",
            }
        )
    return rows


def build_index_map_template(extensions: list[SourceTriple]) -> list[dict]:
    """生成「单位扩展指标 → 评定树节点」的待填模板。

    按 `index_id` 一行，不按编号——同一个编号可能是两个不同指标。
    """
    indices: dict[str, dict] = {}
    for triple in extensions:
        row = indices.setdefault(
            triple.index_id,
            {
                "index_id": triple.index_id,
                "index_code": triple.index_code,
                "index_name": triple.index_name,
                "groups": set(),
                "templates": set(),
            },
        )
        row["groups"].add(f"{triple.group_code} {triple.group_name}")
        row["templates"].add(triple.template_name)
    return [
        {
            "index_id": row["index_id"],
            "index_code": row["index_code"],
            "index_name": row["index_name"],
            "groups": sorted(row["groups"]),
            "templates": sorted(row["templates"]),
            "target_node_id": "",
            "note": "",
        }
        for row in sorted(indices.values(), key=lambda item: (item["index_code"], item["index_id"]))
    ]
