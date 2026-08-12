"""Generate the latest organization-bridge package from the source snapshot."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import tempfile
from pathlib import Path
from typing import Any

ENTRY_FILES = [
    "tree.json",
    "source-index-map.json",
    "corrections.json",
    "sources.json",
]


def _read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def _write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def _canonical_json(value: dict[str, Any]) -> bytes:
    return json.dumps(
        value,
        # JsonCpp's compact writer defaults to escaped non-ASCII text.
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def _append_digest(target: bytearray, name: str, content: bytes) -> None:
    name_bytes = name.encode("utf-8")
    target.extend(str(len(name_bytes)).encode("ascii"))
    target.extend(b":")
    target.extend(name_bytes)
    target.extend(str(len(content)).encode("ascii"))
    target.extend(b":")
    target.extend(content)


def calculate_package_checksum(package_root: str | Path) -> str:
    """Match `RatingTreePackageLoader::calculate_checksum()` byte for byte."""
    root = Path(package_root)
    manifest = _read_json(root / "manifest.json")
    files = sorted(manifest["entry_files"])
    normalized = copy.deepcopy(manifest)
    normalized.pop("content_checksum", None)
    normalized["entry_files"] = files
    digest_input = bytearray()
    _append_digest(digest_input, "manifest.json", _canonical_json(normalized))
    for name in files:
        _append_digest(digest_input, name, _canonical_json(_read_json(root / name)))
    return "sha256:" + hashlib.sha256(digest_input).hexdigest()


def _slug(number: str) -> str:
    return re.sub(r"[^0-9A-Za-z]+", "_", number).strip("_")


def _indicator_base(number: str) -> str:
    return number.rsplit("-", 1)[0] if "-" in number else number


def _natural_number_key(number: str) -> tuple[tuple[int, ...], str]:
    """Sort dotted and dashed display numbers by their numeric segments."""
    return tuple(int(part) for part in re.findall(r"\d+", number)), number


def _parent_groups(groups: list[dict[str, Any]]) -> dict[str, str | None]:
    by_level = {item["level_code"]: item for item in groups}
    result: dict[str, str | None] = {}
    for item in groups:
        parent = by_level.get(item["level_code"][:-3])
        result[item["source_group_id"]] = (
            parent["source_group_id"] if parent is not None else None)
    return result


def _load_h21_indicators(h21_path: Path) -> tuple[dict[str, dict[str, Any]], dict[str, dict[str, Any]]]:
    document = _read_json(h21_path / "defect-indicators.json")
    by_id: dict[str, dict[str, Any]] = {}
    by_table: dict[str, dict[str, Any]] = {}
    for definition in document["definitions"]:
        components = definition["applicable_component_ids"]
        for source in definition.get("indicators", []):
            indicator = copy.deepcopy(source)
            indicator["applicable_component_ids"] = components
            by_id[indicator["id"]] = indicator
            table = indicator.get("source_table")
            if table:
                if table in by_table:
                    raise ValueError(f"duplicate H21 source table {table}")
                by_table[table] = indicator
    return by_id, by_table


def _apply_corrections(
    relations: list[dict[str, Any]],
    corrections: list[dict[str, Any]],
) -> None:
    for correction in corrections:
        matches = [
            item
            for item in relations
            if item["source_group_number"] == correction["source_group_number"]
            and item["source_indicator_number"] == correction["source_indicator_number"]
        ]
        if len(matches) != 1:
            raise ValueError(
                f"correction {correction['correction_id']} must match exactly one relation")
        item = matches[0]
        field = correction["field"]
        target_field = {"name": "display_name"}.get(field, field)
        if item.get(target_field) != correction["source_value"]:
            raise ValueError(
                f"correction {correction['correction_id']} source value changed")
        item[target_field] = correction["corrected_value"]


def build_package_documents(
    source_dir: str | Path,
    h21_path: str | Path,
    scoring_overrides_path: str | Path | None = None,
) -> dict[str, dict[str, Any]]:
    source_root = Path(source_dir)
    snapshot = _read_json(source_root / "source-tree.json")
    metadata = _read_json(source_root / "source-metadata.json")
    corrections_document = _read_json(source_root / "corrections.json")
    scope_document = _read_json(source_root / "group-scope-map.json")
    overrides_document = _read_json(
        Path(scoring_overrides_path)
        if scoring_overrides_path is not None
        else source_root / "scoring-overrides.json")

    groups = snapshot["groups"]
    group_by_id = {item["source_group_id"]: item for item in groups}
    indicator_by_id = {
        item["source_indicator_id"]: item for item in snapshot["indicators"]
    }
    relations: list[dict[str, Any]] = []
    for source in snapshot["relations"]:
        group = group_by_id[source["source_group_id"]]
        indicator = indicator_by_id[source["source_indicator_id"]]
        relations.append({
            **source,
            "source_group_number": group["display_number"],
            "source_indicator_number": indicator["display_number"],
            "display_name": indicator["display_name"],
        })
    _apply_corrections(relations, corrections_document["corrections"])

    scope_by_number = {
        item["source_group_number"]: item for item in scope_document["groups"]
    }
    missing_scopes = {
        item["display_number"] for item in groups
    } - set(scope_by_number)
    if missing_scopes:
        raise ValueError("missing group scopes: " + ", ".join(sorted(missing_scopes)))
    override_by_pair = {
        (item["source_group_number"], item["source_indicator_number"]): item
        for item in overrides_document["overrides"]
    }
    if len(override_by_pair) != len(overrides_document["overrides"]):
        raise ValueError("duplicate scoring override")
    h21_by_id, h21_by_table = _load_h21_indicators(Path(h21_path))
    parents = _parent_groups(groups)
    group_has_relations = {item["source_group_id"] for item in relations}
    relation_sort_orders: dict[tuple[str, str], int] = {}
    relations_by_group: dict[str, list[dict[str, Any]]] = {}
    for relation in relations:
        relations_by_group.setdefault(
            relation["source_group_id"], []).append(relation)
    for group_relations in relations_by_group.values():
        ordered = sorted(
            group_relations,
            key=lambda item: (
                _natural_number_key(item["source_indicator_number"]),
                item["source_indicator_id"],
            ),
        )
        for index, relation in enumerate(ordered, start=1):
            relation_sort_orders[(
                relation["source_group_id"],
                relation["source_indicator_id"],
            )] = index * 10

    all_bridges = sorted({
        bridge
        for item in scope_document["groups"]
        for bridge in item["bridge_type_ids"]
    })
    all_components = sorted({
        component
        for item in scope_document["groups"]
        for component in item["component_category_ids"]
    })
    nodes: list[dict[str, Any]] = [{
        "id": "org.bridge.root",
        "parent_id": None,
        "display_number": "5-10",
        "display_name": "桥梁评定树",
        "node_type": "root",
        "sort_order": 0,
        "bridge_type_ids": all_bridges,
        "component_category_ids": all_components,
        "scoring_mode": "non_scoring",
        "is_selectable": False,
        "organization_note": "来源软件第5至10节桥梁结构；评分规则来自 H21。",
        "source_ids": ["source.organization.rating_tree"],
    }]
    for group in groups:
        scope = scope_by_number[group["display_number"]]
        parent_id = parents[group["source_group_id"]]
        nodes.append({
            "id": "org.bridge.group." + _slug(group["display_number"]),
            "parent_id": (
                "org.bridge.group." + _slug(group_by_id[parent_id]["display_number"])
                if parent_id is not None else "org.bridge.root"
            ),
            "display_number": group["display_number"],
            "display_name": group["display_name"],
            "node_type": (
                "component_group"
                if group["source_group_id"] in group_has_relations
                else "structure_group"
            ),
            "sort_order": int(group["level_code"][-3:]) * 10,
            "bridge_type_ids": scope["bridge_type_ids"],
            "component_category_ids": scope["component_category_ids"],
            "scoring_mode": "non_scoring",
            "is_selectable": False,
            "organization_note": "来源软件桥梁评定分组。",
            "source_ids": ["source.organization.rating_tree"],
        })

    mappings: list[dict[str, Any]] = []
    used_overrides: set[tuple[str, str]] = set()
    for relation in relations:
        group_number = relation["source_group_number"]
        indicator_number = relation["source_indicator_number"]
        pair = (group_number, indicator_number)
        scope = scope_by_number[group_number]
        node_id = (
            "org.bridge.defect." + _slug(indicator_number)
            if _indicator_base(indicator_number) == group_number
            else "org.bridge.defect." + _slug(group_number) + "__" + _slug(indicator_number)
        )
        h21 = h21_by_table.get(indicator_number)
        scoring_mode = "non_scoring"
        h21_indicator_id: str | None = None
        if h21 is not None:
            h21_indicator_id = h21["id"]
            scoring_mode = (
                "inherit_h21"
                if _indicator_base(indicator_number) == group_number
                else "reference_h21"
            )
        if pair in override_by_pair:
            override = override_by_pair[pair]
            used_overrides.add(pair)
            scoring_mode = override["scoring_mode"]
            h21_indicator_id = override.get("h21_indicator_id")
            source_scale_descriptions = override.get("source_scale_descriptions")
        else:
            source_scale_descriptions = None
        if scoring_mode == "non_scoring":
            h21_indicator_id = None
        elif h21_indicator_id not in h21_by_id:
            raise ValueError(f"unknown H21 indicator {h21_indicator_id} for {pair}")
        elif scoring_mode == "inherit_h21" and not set(
            scope["component_category_ids"]
        ).issubset(h21_by_id[h21_indicator_id]["applicable_component_ids"]):
            raise ValueError(f"H21 indicator is not applicable to group scope {pair}")

        node = {
            "id": node_id,
            "parent_id": "org.bridge.group." + _slug(group_number),
            "display_number": indicator_number,
            "display_name": relation["display_name"],
            "node_type": "defect",
            "sort_order": relation_sort_orders[(
                relation["source_group_id"],
                relation["source_indicator_id"],
            )],
            "bridge_type_ids": scope["bridge_type_ids"],
            "component_category_ids": scope["component_category_ids"],
            "scoring_mode": scoring_mode,
            "is_selectable": True,
            "organization_note": (
                "来源软件扩展病害，当前无 H21 评分依据。"
                if scoring_mode == "non_scoring"
                else "判定描述来自来源软件，扣分值引用 H21 对应标度曲线。"
                if source_scale_descriptions is not None
                else "节点结构来自来源软件，标度与扣分引用 H21。"
            ),
            "source_ids": ["source.organization.rating_tree"],
        }
        if h21_indicator_id is not None:
            node["h21_indicator_id"] = h21_indicator_id
            node["source_ids"].insert(0, "source.h21.official")
        if source_scale_descriptions is not None:
            allowed_scales = {
                str(scale) for scale in h21_by_id[h21_indicator_id]["allowed_scales"]
            }
            if not isinstance(source_scale_descriptions, dict) or \
                    set(source_scale_descriptions) != allowed_scales or \
                    any(not isinstance(text, str) or not text
                        for text in source_scale_descriptions.values()):
                raise ValueError(
                    f"source scale descriptions do not match H21 curve for {pair}")
            node["source_scale_descriptions"] = source_scale_descriptions
        nodes.append(node)
        mappings.append({
            "source_group_id": relation["source_group_id"],
            "source_indicator_id": relation["source_indicator_id"],
            "source_group_number": group_number,
            "source_indicator_number": indicator_number,
            "target_node_id": node_id,
        })

    unused_overrides = set(override_by_pair) - used_overrides
    if unused_overrides:
        raise ValueError(f"unused scoring overrides: {sorted(unused_overrides)}")
    if len(mappings) != metadata["unique_group_indicator_pairs"]:
        raise ValueError("generated source mapping coverage is incomplete")
    ids = [node["id"] for node in nodes]
    if len(ids) != len(set(ids)):
        raise ValueError("generated rating-tree node IDs are not unique")
    number_pairs = {
        (item["source_group_number"], item["source_indicator_number"])
        for item in mappings
    }
    if len(number_pairs) != len(mappings):
        raise ValueError("source number pairs are not unique")

    sources = {
        "sources": [
            {
                "id": "source.h21.official",
                "source_type": "technical_condition",
                "title": "JTG/T H21—2011《公路桥梁技术状况评定标准》",
                "reference": "standards/technical-condition/jtg-t-h21-2011/1.0.3",
            },
            {
                "id": "source.jtg5120.official",
                "source_type": "maintenance",
                "title": "JTG 5120—2021《公路桥涵养护规范》",
                "reference": "standards/maintenance/jtg-5120-2021/1.0.0",
            },
            {
                "id": "source.organization.rating_tree",
                "source_type": "organization",
                "title": "来源软件桥梁评定树脱敏结构快照",
                "reference": "standards/source-material/datacheck-bridge-tree/source-tree.json",
            },
        ],
        "snapshot": {
            "source_database_sha256": metadata["source_database_sha256"],
            "exported_at": metadata["exported_at"],
            "unique_group_indicator_pairs": metadata["unique_group_indicator_pairs"],
        },
    }
    return {
        "tree.json": {"nodes": nodes},
        "source-index-map.json": {"mappings": mappings},
        "corrections.json": corrections_document,
        "sources.json": sources,
    }


def write_package(
    source_dir: str | Path,
    h21_path: str | Path,
    output_dir: str | Path,
    *,
    package_version: str = "2.0.2",
    scoring_overrides_path: str | Path | None = None,
) -> None:
    output = Path(output_dir)
    documents = build_package_documents(
        source_dir, h21_path, scoring_overrides_path)
    manifest: dict[str, Any] = {
        "package_type": "rating_tree_extension",
        "tree_code": "organization-bridge",
        "tree_name": "单位桥梁评定树",
        "package_version": package_version,
        "contract_version": 1,
        "status": "active",
        "content_checksum": "sha256:" + "0" * 64,
        "entry_files": ENTRY_FILES,
    }
    _write_json(output / "manifest.json", manifest)
    for name, document in documents.items():
        _write_json(output / name, document)
    manifest["content_checksum"] = calculate_package_checksum(output)
    _write_json(output / "manifest.json", manifest)


def check_package(
    source_dir: str | Path,
    h21_path: str | Path,
    output_dir: str | Path,
    *,
    package_version: str = "2.0.2",
    scoring_overrides_path: str | Path | None = None,
) -> bool:
    output = Path(output_dir)
    with tempfile.TemporaryDirectory() as temp:
        generated = Path(temp)
        write_package(
            source_dir,
            h21_path,
            generated,
            package_version=package_version,
            scoring_overrides_path=scoring_overrides_path,
        )
        expected = {"manifest.json", *ENTRY_FILES}
        return all(
            (generated / name).read_bytes() == (output / name).read_bytes()
            for name in expected
        )


def main() -> None:
    repository = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=repository / "standards/source-material/datacheck-bridge-tree",
    )
    parser.add_argument(
        "--h21-path",
        type=Path,
        default=repository / "standards/technical-condition/jtg-t-h21-2011/1.0.3",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=repository / "standards/rating-tree/organization-bridge/2.0.2",
    )
    parser.add_argument("--package-version", default="2.0.2")
    parser.add_argument(
        "--scoring-overrides",
        type=Path,
        default=(repository / "standards/source-material/datacheck-bridge-tree/"
                 "scoring-overrides.json"),
    )
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.check:
        if not check_package(
            args.source_dir,
            args.h21_path,
            args.output_dir,
            package_version=args.package_version,
            scoring_overrides_path=args.scoring_overrides,
        ):
            raise SystemExit("generated package differs from committed package")
        return
    write_package(
        args.source_dir,
        args.h21_path,
        args.output_dir,
        package_version=args.package_version,
        scoring_overrides_path=args.scoring_overrides,
    )


if __name__ == "__main__":
    main()
