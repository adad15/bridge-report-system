import base64
import sqlite3

import pytest

from bridge_report_tools.importers.source_db.photos import (
    DEFAULT_SECTION_MAP,
    build_photo_candidates,
    photo_caption,
)
from bridge_report_tools.importers.source_db.reader import ComponentNode, SourcePhoto

JPEG = b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\xff\xd9"

TREE = [
    ComponentNode("t-part1", "上部结构", "001", 1, None, None),
    ComponentNode("t-a", "25-1#板", "001001001", 3, None, "空心板"),
    ComponentNode("t-b", "1-1#板", "001001002", 3, None, "空心板"),
    ComponentNode("t-part2", "下部结构", "002", 1, None, None),
    ComponentNode("t-c", "3#墩盖梁", "002003001", 3, None, "盖梁"),
    ComponentNode("t-part3", "桥面系", "003", 1, None, None),
    ComponentNode("t-d", "第3孔桥面", "003001001", 3, None, "铺装"),
    ComponentNode("t-r", "1-1#板~1-25#板", "001001003", 3, None, "空心板"),
]


from bridge_report_tools.importers.source_db.defects import DefectLink

#: 源病害 id 与契约候选 id 刻意不同——写成同一个会掩盖"照片按哪个 id 查"的错误。
TREE_IDS: dict[str, DefectLink] = {}


def candidate(source_id, tree_id, defect_type="受渗水侵蚀", description="受渗水侵蚀"):
    candidate_id = "source_defect_" + source_id.replace("d-", "")
    TREE_IDS[source_id] = DefectLink(candidate_id, tree_id)
    return {"candidate_id": candidate_id, "defect_type": defect_type,
            "defect_description": description, "photo_references": [], "warnings": []}


@pytest.fixture()
def source_db(tmp_path):
    path = tmp_path / "photos.sqlite"
    db = sqlite3.connect(path)
    db.execute("create table images (id text primary key, contentType text, fileName text)")
    for photo_id in ("i-1", "i-2", "i-3", "i-4"):
        db.execute("insert into images values (?,?,?)", (
            photo_id, "image/jpeg", "data:image/jpeg;base64," + base64.b64encode(JPEG).decode()))
    db.commit()
    db.close()
    return sqlite3.connect(f"file:{path.as_posix()}?mode=ro", uri=True)


def test_numbers_photos_per_structure_section(source_db, tmp_path):
    photos = [SourcePhoto("i-1", "d-1", "image/jpeg", 1600, 1200, "25-1#板"),
              SourcePhoto("i-2", "d-2", "image/jpeg", 1600, 1200, "3#墩盖梁"),
              SourcePhoto("i-3", "d-3", "image/jpeg", 1600, 1200, "第3孔桥面")]
    defects = [candidate("d-1", "t-a"), candidate("d-2", "t-c"), candidate("d-3", "t-d")]

    built, _ = build_photo_candidates(source_db, photos, defects, TREE_IDS, TREE, tmp_path)

    assert [p["photo_number"] for p in built] == ["2.1-1", "2.2-1", "2.3-1"]


def test_numbers_run_consecutively_inside_a_section(source_db, tmp_path):
    photos = [SourcePhoto(f"i-{n}", f"d-{n}", "image/jpeg", 1600, 1200, "25-1#板") for n in (1, 2, 3)]
    defects = [candidate(f"d-{n}", "t-a") for n in (1, 2, 3)]

    built, _ = build_photo_candidates(source_db, photos, defects, TREE_IDS, TREE, tmp_path)

    assert [p["photo_number"] for p in built] == ["2.1-1", "2.1-2", "2.1-3"]


def test_section_map_is_configurable(source_db, tmp_path):
    photos = [SourcePhoto("i-1", "d-1", "image/jpeg", 1600, 1200, "25-1#板")]
    defects = [candidate("d-1", "t-a")]

    built, _ = build_photo_candidates(
        source_db, photos, defects, TREE_IDS, TREE, tmp_path, section_map={"001": "3.5"})

    assert built[0]["photo_number"] == "3.5-1"


def test_composes_the_caption_from_component_and_defect_type():
    assert photo_caption("25-1#板", "受渗水侵蚀", "受渗水侵蚀，总面积 2.5㎡") == "25-1#板 受渗水侵蚀"


def test_caption_falls_back_to_the_description_when_the_type_is_blank():
    """来源软件允许病害不选类型；题注不能只剩构件名。"""
    assert photo_caption("管养牌", "", "基本完好") == "管养牌 基本完好"


def test_repeated_captions_are_left_alone(source_db, tmp_path):
    """报告里靠前面的照片编号区分，题注重复是正常的，不加序号后缀。"""
    photos = [SourcePhoto(f"i-{n}", f"d-{n}", "image/jpeg", 1600, 1200, "25-1#板") for n in (1, 2)]
    defects = [candidate(f"d-{n}", "t-a", "破损露筋") for n in (1, 2)]

    built, _ = build_photo_candidates(source_db, photos, defects, TREE_IDS, TREE, tmp_path)

    captions = [p["extracted_file"]["original_caption"] for p in built]
    assert captions == ["25-1#板 破损露筋", "25-1#板 破损露筋"]


def test_writes_each_photo_to_the_temporary_directory(source_db, tmp_path):
    photos = [SourcePhoto("i-1", "d-1", "image/jpeg", 1600, 1200, "25-1#板")]
    defects = [candidate("d-1", "t-a")]

    built, files = build_photo_candidates(source_db, photos, defects, TREE_IDS, TREE, tmp_path)

    assert len(files) == 1
    written = tmp_path / built[0]["extracted_file"]["temporary_file_name"]
    assert written.read_bytes() == JPEG
    assert written.suffix == ".jpg"


def test_binds_the_photo_to_its_defect_on_both_sides(source_db, tmp_path):
    photos = [SourcePhoto("i-1", "d-1", "image/jpeg", 1600, 1200, "25-1#板")]
    defects = [candidate("d-1", "t-a")]

    built, _ = build_photo_candidates(source_db, photos, defects, TREE_IDS, TREE, tmp_path)

    assert built[0]["linked_defect_candidate_id"] == "source_defect_1"
    # 归属来自外键而不是编号推断，所以匹配状态直接是已确认。
    assert built[0]["match_status"] == "已确认"
    assert built[0]["review_status"] == "待确认"
    reference = defects[0]["photo_references"][0]
    assert reference["photo_number"] == built[0]["photo_number"]
    assert reference["resolution"] == "matched"
    assert reference["resolved_defect_candidate_id"] == "source_defect_1"


def test_flags_a_photo_hanging_on_a_range_component(source_db, tmp_path):
    """范围行拆分后照片只能落在第一个构件上，要标出来让人确认。"""
    photos = [SourcePhoto("i-1", "d-1", "image/jpeg", 1600, 1200, "1-1#板~1-25#板")]
    defects = [candidate("d-1", "t-r")]

    built, _ = build_photo_candidates(source_db, photos, defects, TREE_IDS, TREE, tmp_path)

    assert any(w["code"] == "source_photo_on_range_component" for w in built[0]["warnings"])


def test_skips_a_photo_whose_defect_is_not_in_this_import(source_db, tmp_path):
    photos = [SourcePhoto("i-1", "missing", "image/jpeg", 1600, 1200, "25-1#板")]

    built, files = build_photo_candidates(source_db, photos, [], TREE_IDS, TREE, tmp_path)

    assert built == []
    assert files == []


def test_numbering_is_stable_across_runs(source_db, tmp_path):
    photos = [SourcePhoto(f"i-{n}", f"d-{n}", "image/jpeg", 1600, 1200, "25-1#板") for n in (1, 2, 3)]

    first, _ = build_photo_candidates(
        source_db, photos, [candidate(f"d-{n}", "t-a") for n in (1, 2, 3)], TREE_IDS, TREE, tmp_path / "a")
    second, _ = build_photo_candidates(
        source_db, photos, [candidate(f"d-{n}", "t-a") for n in (1, 2, 3)], TREE_IDS, TREE, tmp_path / "b")

    assert [p["photo_number"] for p in first] == [p["photo_number"] for p in second]
    assert [p["candidate_id"] for p in first] == [p["candidate_id"] for p in second]


def test_default_section_map_covers_the_three_structure_parts():
    assert DEFAULT_SECTION_MAP == {"001": "2.1", "002": "2.2", "003": "2.3"}
