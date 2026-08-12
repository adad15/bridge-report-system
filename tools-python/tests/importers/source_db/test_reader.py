import sqlite3

import pytest

from bridge_report_tools.importers.source_db.reader import (
    SourceDatabaseError,
    load_component_tree,
    load_defects,
    load_photo_content,
    load_photos,
    load_task,
    open_source_db,
)

TASK = "task-1"


def build_db(path, *, skip_table=None, drop_column=None):
    """最小来源库：列名与真实离线库一致，只保留实测有数据的那些。"""
    db = sqlite3.connect(path)
    schema = {
        "tasks": "id text primary key, name text, checkDate text, score real, grade integer",
        "taskTrees": ("id text primary key, taskId text, name text, levelCode text, nodeType integer,"
                      " memberCount integer, memberTypeName text"),
        "outerCheckData": ('id text primary key, taskId text, treeId text, name text, data text, pos text,'
                           ' degree integer, judgeIndexId text, judgeTreeId text, diseaseDefinition text,'
                           ' "数量" text, "数量单位" text, "长度" text, "长度单位" text,'
                           ' "宽度" text, "宽度单位" text, "面积一" text, "面积一单位" text, "走向" text'),
        "images": ("id text primary key, taskId text, fileName text, contentType text, w integer, h integer,"
                   " ForeignTable text, ForeignKey text, memberNum text"),
        "judgeTree": "id text primary key, chapterNum text, name text",
        "judgeIndex": "id text primary key, tableNum text, name text",
    }
    for table, columns in schema.items():
        if table == skip_table:
            continue
        if drop_column and drop_column[0] == table:
            columns = ", ".join(c for c in columns.split(", ") if not c.startswith(drop_column[1]))
        db.execute(f"create table {table} ({columns})")
    if skip_table != "tasks":
        db.execute("insert into tasks values (?,?,?,?,?)", (TASK, "百股大桥", "2024-06-21", 82.96, 2))
    if skip_table != "taskTrees":
        for row in [
            ("t-part", TASK, "上部结构", "001", 1, None, None),
            ("t-cat", TASK, "上部承重构件", "001001", 2, 825, None),
            ("t-a", TASK, "25-1#板", "001001001", 3, None, "空心板"),
            ("t-b", TASK, "1-1#板~1-25#板", "001001002", 3, None, "空心板"),
        ]:
            db.execute("insert into taskTrees values (?,?,?,?,?,?,?)", row)
    # 缺列场景只需要建出残缺的表结构，不必再塞数据。
    if drop_column and drop_column[0] == "outerCheckData":
        db.commit()
        db.close()
        return path
    if skip_table != "outerCheckData":
        db.execute("insert into outerCheckData values (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", (
            "d-1", TASK, "t-a", "受渗水侵蚀，混凝土剥蚀破损", "受渗水侵蚀，总面积：2.5㎡", "左侧翼缘板",
            2, "idx-1", "jt-1", "受渗水侵蚀，总面积：${面积一}",
            None, None, None, None, None, None, "2.5", "㎡", None))
        db.execute("insert into outerCheckData values (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", (
            "d-2", TASK, "t-b", "", "存在熏黑痕迹", None,
            2, "idx-other", "jt-1", None,
            None, None, None, None, None, None, None, None, None))
    if skip_table != "images":
        db.execute("insert into images values (?,?,?,?,?,?,?,?,?)", (
            "i-1", TASK, "data:image/jpeg;base64,/9j/4AAQ", "image/jpeg", 1600, 1200,
            "outerCheckData", "d-1", "25-1#板"))
    if skip_table != "judgeTree":
        db.execute("insert into judgeTree values ('jt-1','5.1.1','板式构件')")
    db.commit()
    db.close()
    return path


@pytest.fixture()
def source_db(tmp_path):
    return build_db(tmp_path / "source.sqlite")


def test_opens_the_database_read_only(source_db, monkeypatch):
    """来源库是别人的数据，任何一步都不许写回去。"""
    seen = []
    original = sqlite3.connect
    monkeypatch.setattr(sqlite3, "connect", lambda t, *a, **k: (seen.append((t, k.get("uri"))), original(t, *a, **k))[1])

    open_source_db(source_db).close()

    assert seen and seen[0][1] is True
    assert "mode=ro" in seen[0][0]


def test_refuses_a_database_missing_a_required_table(tmp_path):
    path = build_db(tmp_path / "no-images.sqlite", skip_table="images")

    with pytest.raises(SourceDatabaseError) as excinfo:
        open_source_db(path)

    assert excinfo.value.code == "source_db_table_missing"
    assert "images" in str(excinfo.value)


def test_refuses_a_database_missing_a_required_column(tmp_path):
    """对方升级后砍掉一列时要立刻报错，不能静默产出残缺数据。"""
    path = build_db(tmp_path / "no-degree.sqlite", drop_column=("outerCheckData", "degree"))

    with pytest.raises(SourceDatabaseError) as excinfo:
        open_source_db(path)

    assert excinfo.value.code == "source_db_column_missing"
    assert "degree" in str(excinfo.value)


def test_loads_the_task(source_db):
    with open_source_db(source_db) as db:
        task = load_task(db, TASK)

    assert task.name == "百股大桥"
    assert task.check_date == "2024-06-21"


def test_reports_an_unknown_task_id(source_db):
    with open_source_db(source_db) as db:
        with pytest.raises(SourceDatabaseError) as excinfo:
            load_task(db, "nope")

    assert excinfo.value.code == "source_task_not_found"


def test_loads_the_component_tree_with_parents(source_db):
    with open_source_db(source_db) as db:
        nodes = load_component_tree(db, TASK)

    by_id = {n.id: n for n in nodes}
    assert by_id["t-a"].name == "25-1#板"
    assert by_id["t-a"].member_type_name == "空心板"
    # 父级靠层级码前缀推出来，来源库里没有 parentId 列。
    assert by_id["t-a"].parent_level_code == "001001"
    assert by_id["t-cat"].member_count == 825


def test_loads_defects_with_dimensions_and_indicator(source_db):
    with open_source_db(source_db) as db:
        defects = load_defects(db, TASK)

    first = next(d for d in defects if d.id == "d-1")
    assert first.tree_id == "t-a"
    assert first.name == "受渗水侵蚀，混凝土剥蚀破损"
    assert first.degree == 2
    assert first.judge_index_id == "idx-1"
    assert first.position == "左侧翼缘板"
    # 只收实测有数据的维度，空的不进来。
    assert first.dimensions == {"面积一": ("2.5", "㎡")}


def test_keeps_a_defect_whose_type_is_blank(source_db):
    """来源软件允许病害不选类型，这不是脏数据。"""
    with open_source_db(source_db) as db:
        blank = next(d for d in load_defects(db, TASK) if d.id == "d-2")

    assert blank.name == ""
    assert blank.degree == 2
    assert blank.dimensions == {}


def test_loads_photo_metadata_without_the_base64_body(source_db):
    """一座桥 42 MB 的 base64，列清单时不能读进内存。"""
    with open_source_db(source_db) as db:
        photos = load_photos(db, TASK)

    assert len(photos) == 1
    photo = photos[0]
    assert photo.defect_id == "d-1"
    assert (photo.width, photo.height) == (1600, 1200)
    assert photo.member_number == "25-1#板"
    assert not hasattr(photo, "content")


def test_reads_one_photo_body_on_demand(source_db):
    with open_source_db(source_db) as db:
        content_type, payload = load_photo_content(db, "i-1")

    assert content_type == "image/jpeg"
    assert payload.startswith(b"\xff\xd8")


def test_reading_is_stable_across_runs(source_db):
    with open_source_db(source_db) as db:
        assert load_defects(db, TASK) == load_defects(db, TASK)
        assert load_component_tree(db, TASK) == load_component_tree(db, TASK)
        assert load_photos(db, TASK) == load_photos(db, TASK)


def test_indexes_indicator_codes_by_id(tmp_path):
    """编号会重复，只有 judgeIndex.id 是唯一键。"""
    path = tmp_path / "idx.sqlite"
    build_db(path)
    db = sqlite3.connect(path)
    db.execute("insert into judgeIndex values ('i-a','5.1.1-13','水损（参照混凝土碳化执行）')")
    db.execute("insert into judgeIndex values ('i-b','5.1.1-13','桥面板其它病害')")
    db.commit()
    db.close()

    from bridge_report_tools.importers.source_db.reader import load_indicator_codes

    with open_source_db(path) as connection:
        codes = load_indicator_codes(connection)

    assert codes["i-a"] == ("5.1.1-13", "水损（参照混凝土碳化执行）")
    assert codes["i-b"] == ("5.1.1-13", "桥面板其它病害")


def test_indexes_group_codes_by_id(source_db):
    from bridge_report_tools.importers.source_db.reader import load_group_codes

    with open_source_db(source_db) as connection:
        codes = load_group_codes(connection)

    assert codes["jt-1"] == ("5.1.1", "板式构件")
