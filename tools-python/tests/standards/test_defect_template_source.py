import sqlite3

import pytest

from bridge_report_tools.standards.defect_template_source import (
    SourceTriple,
    classify,
    h21_indicator_id,
    load_h21_indicator_ids,
    load_source_triples,
)


def build_source_db(path, rows) -> None:
    """最小来源库：五张表的结构与真实离线库一致，只保留用到的列。

    rows 的每一项是 (分组编号, 分组名, 指标 id, 指标编号, 指标名, 模板名)。
    指标 id 单独给，是因为真实库里 tableNum 会重复，它不是主键。
    """
    db = sqlite3.connect(path)
    db.executescript(
        """
        create table judgeTree (id text primary key, chapterNum text, name text);
        create table judgeIndex (id text primary key, tableNum text, name text);
        create table sysConfig (id text primary key, catag text, name text);
        create table judgeTree2Index (treeId text, indexId text);
        create table judgeIndex2diseaseType (indexId text, diseaseTypeId text);
        """
    )
    trees, indexes, templates = {}, {}, {}
    for group_code, group_name, index_id, index_code, index_name, template in rows:
        if group_code not in trees:
            trees[group_code] = f"tree-{len(trees)}"
            db.execute("insert into judgeTree values (?,?,?)", (trees[group_code], group_code, group_name))
        if index_id not in indexes:
            indexes[index_id] = index_id
            db.execute("insert into judgeIndex values (?,?,?)", (index_id, index_code, index_name))
        if template not in templates:
            templates[template] = f"tpl-{len(templates)}"
            db.execute(
                "insert into sysConfig values (?,?,?)", (templates[template], "diseaseType", template)
            )
        db.execute("insert into judgeTree2Index values (?,?)", (trees[group_code], index_id))
        db.execute("insert into judgeIndex2diseaseType values (?,?)", (index_id, templates[template]))
    # 混入一条别的配置类别，确认查询不会把它当成病害模板。
    db.execute("insert into sysConfig values ('other','bridgeType','梁式桥')")
    db.commit()
    db.close()


SAMPLE = [
    ("5.1.1", "上部承重构件、上部一般构件", "idx-a", "5.1.1-2", "剥落、掉角", "受渗水侵蚀，混凝土剥蚀破损"),
    ("5.1.1", "上部承重构件、上部一般构件", "idx-b", "5.1.1-13", "水损（参照混凝土碳化执行）", "渗水泛碱"),
    # 同一个 tableNum，不同分组下是完全不同的指标——真实库里就是这样。
    ("6.1.3", "桥面板", "idx-c", "5.1.1-13", "桥面板其它病害", "横向裂缝"),
    ("1.1.1", "桥面铺装", "idx-d", "1.1.1-6", "桥面贯通横缝", "横向裂缝"),
    # 涵洞分组借用了桥梁栏杆的指标编号：编号是 H21 的，但分组不属于定检。
    ("12.4-4", "排水沟", "idx-e", "5.1.1-2", "剥落、掉角", "破损"),
]


@pytest.fixture()
def source_db(tmp_path):
    path = tmp_path / "source.sqlite"
    build_source_db(path, SAMPLE)
    return path


def test_loads_every_group_index_template_triple(source_db) -> None:
    triples = load_source_triples(source_db)

    assert len(triples) == 5
    assert SourceTriple(
        "5.1.1", "上部承重构件、上部一般构件", "idx-b", "5.1.1-13", "水损（参照混凝土碳化执行）", "渗水泛碱"
    ) in triples


def test_keeps_indices_apart_when_they_share_a_table_number(source_db) -> None:
    """5.1.1-13 是两个不同指标；按编号归并会张冠李戴。"""
    same_code = [t for t in load_source_triples(source_db) if t.index_code == "5.1.1-13"]

    assert len(same_code) == 2
    assert {t.index_id for t in same_code} == {"idx-b", "idx-c"}
    assert {t.index_name for t in same_code} == {"水损（参照混凝土碳化执行）", "桥面板其它病害"}


def test_repeated_relations_collapse_to_one_triple(tmp_path) -> None:
    path = tmp_path / "dup.sqlite"
    row = ("5.1.1", "上部承重构件", "idx-a", "5.1.1-13", "水损", "渗水泛碱")
    build_source_db(path, [row, row, row])

    assert len(load_source_triples(path)) == 1


def test_export_order_is_stable_so_reruns_diff_clean(source_db) -> None:
    first = load_source_triples(source_db)
    second = load_source_triples(source_db)

    assert first == second
    assert first == sorted(first, key=lambda t: t.sort_key)


def test_opens_the_source_database_read_only(source_db, monkeypatch) -> None:
    """来源库是别人的数据，任何一步都不许写回去。"""
    seen = []
    original = sqlite3.connect

    def spy(target, *args, **kwargs):
        seen.append((target, kwargs.get("uri", False)))
        return original(target, *args, **kwargs)

    monkeypatch.setattr(sqlite3, "connect", spy)
    load_source_triples(source_db)

    assert seen, "没有打开过数据库"
    target, uri = seen[0]
    assert uri is True
    assert "mode=ro" in target


def test_maps_source_index_code_to_h21_indicator_id() -> None:
    assert h21_indicator_id("5.1.1-2") == "h21.defect.5_1_1_2"
    assert h21_indicator_id("10.1.2-7") == "h21.defect.10_1_2_7"


KNOWN = {"h21.defect.5_1_1_2", "h21.defect.6_1_3_1"}


def test_splits_triples_into_three_buckets(source_db) -> None:
    result = classify(load_source_triples(source_db), KNOWN)

    # a：编号对上 H21，且分组本身属于定检章节
    assert [t.index_id for t in result.mapped] == ["idx-a"]
    # b：同章节但超出 H21 编号，是评定树自己加的指标——不能当成别的标准丢掉
    assert {t.index_id for t in result.extensions} == {"idx-b", "idx-c"}
    # c：经常检查等别的标准
    # c：经常检查，以及借用了 H21 编号但分组属于涵洞的那条
    assert sorted(t.index_id for t in result.foreign) == ["idx-d", "idx-e"]
    assert result.counts == {"mapped": 1, "extensions": 2, "foreign": 2}


def test_extension_bucket_lists_the_indices_that_need_a_manual_row(source_db) -> None:
    result = classify(load_source_triples(source_db), KNOWN)

    # 对表按指标实例给，不能按编号——两个 5.1.1-13 要分别对。
    assert sorted(result.extension_indices) == [
        ("idx-b", "5.1.1-13", "水损（参照混凝土碳化执行）"),
        ("idx-c", "5.1.1-13", "桥面板其它病害"),
    ]


def test_reads_indicator_ids_from_the_real_h21_package(h21_package_root) -> None:
    ids = load_h21_indicator_ids(h21_package_root)

    assert "h21.defect.5_1_1_2" in ids
    # H21 的 5.1.1 只到 -12，没有水损；它是单位评定树加的。
    assert "h21.defect.5_1_1_13" not in ids
    assert len(ids) > 200


def test_component_map_template_lists_every_group_once(source_db) -> None:
    from bridge_report_tools.standards.defect_template_source import build_component_map_template

    triples = load_source_triples(source_db)
    result = classify(triples, KNOWN)

    rows = build_component_map_template(result.mapped, node_lookup={}, category_names={})

    assert [row["group_code"] for row in rows] == ["5.1.1"]
    assert rows[0]["group_name"] == "上部承重构件、上部一般构件"
    assert rows[0]["template_count"] == 1
    # 待人工填写的字段必须是空的，建议单独放，不能混为一谈。
    assert rows[0]["component_category_ids"] == []
    assert "suggested_component_category_ids" in rows[0]


def test_component_map_template_suggests_from_the_rating_tree(source_db) -> None:
    from bridge_report_tools.standards.defect_template_source import build_component_map_template

    result = classify(load_source_triples(source_db), KNOWN)
    lookup = {
        "h21.defect.5_1_1_2": [
            {"id": "org.node.a", "component_category_ids": ["h21.component.beam.upper_bearing"]},
        ]
    }

    rows = build_component_map_template(result.mapped, lookup, {"h21.component.beam.upper_bearing": "上部承重构件"})

    assert rows[0]["suggested_component_category_ids"] == ["h21.component.beam.upper_bearing"]
    assert rows[0]["suggestion_names"] == ["上部承重构件"]


def test_index_map_template_lists_each_extension_index(source_db) -> None:
    from bridge_report_tools.standards.defect_template_source import build_index_map_template

    result = classify(load_source_triples(source_db), KNOWN)

    rows = build_index_map_template(result.extensions)

    assert [row["index_id"] for row in rows] == ["idx-b", "idx-c"]
    assert rows[0]["index_code"] == "5.1.1-13"
    assert rows[0]["target_node_id"] == ""
    assert rows[0]["templates"] == ["渗水泛碱"]


def test_a_group_from_another_standard_never_enters_the_mapped_bucket(source_db) -> None:
    """12.4-4 排水沟属于涵洞，却挂着桥梁栏杆的指标编号；只看编号会把它当成定检。"""
    result = classify(load_source_triples(source_db), KNOWN)

    assert all(t.group_code != "12.4-4" for t in result.mapped)
    assert any(t.group_code == "12.4-4" for t in result.foreign)
