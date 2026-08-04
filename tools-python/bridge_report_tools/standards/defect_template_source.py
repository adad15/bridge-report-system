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


def classify(triples: list[SourceTriple], h21_indicator_ids: set[str]) -> Classified:
    """按指标编号能否落到 H21 分三档。

    b 档是同一章节里超出 H21 编号范围的条目——那是单位评定树自己扩的指标（水损、
    各类"其它病害"）。它们必须和 c 档区分开：c 档属于经常检查、隧道等别的标准，
    本期不做；b 档要靠一张小对表落到评定树节点上，丢掉就会损失最有价值的映射。
    """
    chapters = {identifier.rsplit("_", 1)[0].split(".")[-1] for identifier in h21_indicator_ids}
    result = Classified()
    for triple in triples:
        if h21_indicator_id(triple.index_code) in h21_indicator_ids:
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
