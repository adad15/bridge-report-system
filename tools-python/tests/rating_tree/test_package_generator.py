import json
from pathlib import Path

from bridge_report_tools.rating_tree.package_generator import (
    build_package_documents,
    calculate_package_checksum,
    write_package,
)

REPOSITORY = Path(__file__).resolve().parents[3]
SOURCE = REPOSITORY / "standards/source-material/datacheck-bridge-tree"
H21 = REPOSITORY / "standards/technical-condition/jtg-t-h21-2011/1.0.3"


def test_checksum_matches_the_existing_cpp_verified_package():
    package = REPOSITORY / "standards/rating-tree/organization-bridge/1.0.3"
    manifest = json.loads((package / "manifest.json").read_text(encoding="utf-8"))

    assert calculate_package_checksum(package) == manifest["content_checksum"]


def test_generates_all_403_source_relations_without_text_rules():
    documents = build_package_documents(SOURCE, H21)

    assert len(documents["source-index-map.json"]["mappings"]) == 403
    assert set(documents) == {
        "tree.json", "source-index-map.json", "corrections.json", "sources.json"}
    numbers = {
        (item["source_group_number"], item["source_indicator_number"]):
        item["target_node_id"]
        for item in documents["source-index-map.json"]["mappings"]
    }
    assert numbers[("9.1.2", "9.1.1-1")] == \
        "org.bridge.defect.9_1_2__9_1_1_1"
    assert numbers[("9.1.2", "9.1.2-1")] == "org.bridge.defect.9_1_2_1"


def test_applies_the_approved_correction_and_scoring_policies():
    nodes = {
        item["id"]: item
        for item in build_package_documents(SOURCE, H21)["tree.json"]["nodes"]
    }

    assert nodes["org.bridge.defect.9_2_1_11"]["display_name"] == "台身其它病害"
    assert nodes["org.bridge.defect.9_2_1_11"]["scoring_mode"] == "non_scoring"
    assert "h21_indicator_id" not in nodes["org.bridge.defect.9_2_1_11"]
    assert nodes["org.bridge.defect.9_1_2_1"]["h21_indicator_id"] == \
        "h21.defect.9_1_2"
    assert nodes["org.bridge.defect.9_1_2_2"]["scoring_mode"] == "non_scoring"


def test_water_damage_uses_source_scales_and_h21_deduction_curves():
    nodes = {
        item["id"]: item
        for item in build_package_documents(SOURCE, H21)["tree.json"]["nodes"]
    }

    pier_water = nodes["org.bridge.defect.9_1_1_10"]
    assert pier_water["scoring_mode"] == "reference_h21"
    assert pier_water["h21_indicator_id"] == "h21.defect.9_1_1_5"
    assert pier_water["source_scale_descriptions"] == {
        "1": "少量；范围＜5%",
        "2": "局部渗水泛碱；范围＜10%",
        "3": "渗水、水蚀严重；范围＜30%",
        "4": "—",
    }

    foundation_water = nodes["org.bridge.defect.6_2_1__9_2_1_10"]
    assert foundation_water["scoring_mode"] == "reference_h21"
    assert foundation_water["h21_indicator_id"] == "h21.defect.9_1_1_5"
    assert foundation_water["source_scale_descriptions"]["4"] == \
        "水蚀露筋且存在异常变化"

    assert nodes["org.bridge.defect.9_1_1_11"]["scoring_mode"] == \
        "non_scoring"


def test_orders_sibling_defects_by_natural_display_number():
    nodes = build_package_documents(SOURCE, H21)["tree.json"]["nodes"]
    pier_defects = sorted(
        (
            item for item in nodes
            if item["parent_id"] == "org.bridge.group.9_1_1"
        ),
        key=lambda item: item["sort_order"],
    )

    assert [item["display_number"] for item in pier_defects] == [
        "9.1.1-1",
        "9.1.1-2",
        "9.1.1-3",
        "9.1.1-4",
        "9.1.1-5",
        "9.1.1-6",
        "9.1.1-7",
        "9.1.1-8",
        "9.1.1-9",
        "9.1.1-10",
        "9.1.1-11",
    ]
    assert len({item["sort_order"] for item in pier_defects}) == 11


def test_generation_is_byte_for_byte_deterministic(tmp_path):
    first = tmp_path / "first"
    second = tmp_path / "second"
    write_package(SOURCE, H21, first)
    write_package(SOURCE, H21, second)

    assert {path.name: path.read_bytes() for path in first.iterdir()} == {
        path.name: path.read_bytes() for path in second.iterdir()}
