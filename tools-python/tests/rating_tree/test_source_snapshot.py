import sqlite3

import pytest

from bridge_report_tools.rating_tree.source_snapshot import export_snapshot


def build_source(path):
    db = sqlite3.connect(path)
    db.executescript(
        """
        create table judgeTree (id text, chapterNum text, name text, levelCode text, secret text);
        create table judgeIndex (id text, tableNum text, name text, diseaseTypeId text);
        create table judgeTree2Index (id text, treeId text, indexId text, displayOrder integer, rev text);
        insert into judgeTree values ('g9','9.1.2','盖梁和系梁','005001002','hidden');
        insert into judgeTree values ('g11','11','其它','007','hidden');
        insert into judgeIndex values ('i2','9.1.2-2','盖梁和系梁病害','hidden');
        insert into judgeIndex values ('i1','9.1.2-1','裂缝','hidden');
        insert into judgeTree2Index values ('r2','g9','i2',2,'hidden');
        insert into judgeTree2Index values ('r1','g9','i1',1,'hidden');
        insert into judgeTree2Index values ('r11','g11','i1',1,'hidden');
        """
    )
    db.commit()
    db.close()
    return path


def test_exports_only_bridge_chapters_in_explicit_order(tmp_path):
    tree, metadata = export_snapshot(
        build_source(tmp_path / "source.sqlite"),
        exported_at="2026-08-07T00:00:00Z",
    )

    assert [item["display_number"] for item in tree["groups"]] == ["9.1.2"]
    assert [item["source_relation_id"] for item in tree["relations"]] == ["r1", "r2"]
    assert metadata["unique_group_indicator_pairs"] == 2
    assert "source_db" not in metadata
    assert "secret" not in str(tree)


def test_opens_the_source_database_read_only(tmp_path, monkeypatch):
    source = build_source(tmp_path / "source.sqlite")
    seen = []
    original = sqlite3.connect
    monkeypatch.setattr(
        sqlite3,
        "connect",
        lambda target, *args, **kwargs: (
            seen.append((target, kwargs.get("uri"))),
            original(target, *args, **kwargs),
        )[1],
    )

    export_snapshot(source, exported_at="2026-08-07T00:00:00Z")

    assert seen[0][1] is True
    assert "mode=ro" in seen[0][0]


def test_rejects_a_changed_source_schema(tmp_path):
    source = build_source(tmp_path / "source.sqlite")
    db = sqlite3.connect(source)
    db.execute("alter table judgeTree rename column chapterNum to changed")
    db.commit()
    db.close()

    with pytest.raises(ValueError, match="chapterNum"):
        export_snapshot(source)
