"""Read-only inventory evidence and binding prerequisites; never creates a ledger."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sqlite3
from collections import Counter
from pathlib import Path


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_manifest", type=Path)
    parser.add_argument("acceptance_manifest", type=Path)
    args = parser.parse_args()
    source = json.loads(args.source_manifest.read_text(encoding="utf-8"))
    environment = json.loads(args.acceptance_manifest.read_text(encoding="utf-8"))
    snapshot = Path(source["snapshot_path"]).resolve()
    output = Path(source["log_directory"]) / "binding-and-fidelity"
    output.mkdir(exist_ok=True)
    assert digest(snapshot) == source["snapshot_sha256"]
    summary = json.loads(
        (Path(environment["log_directory"]) / "source-db-import-summary.json")
        .read_text(encoding="utf-8"))
    database = sqlite3.connect(snapshot.as_uri() + "?mode=ro", uri=True)
    database.row_factory = sqlite3.Row
    years = []
    try:
        supplementary = {
            table: database.execute(f'SELECT count(*) FROM "{table}"').fetchone()[0]
            for table in ("entityStructInfo", "entityCheckRecord", "entityCheckData", "iLISData")
        }
        for year, item in sorted(summary["years"].items()):
            task_id = item["taskId"]
            tree = [dict(row) for row in database.execute(
                "SELECT id,name,levelCode,nodeType,memberCount,memberTypeName "
                "FROM taskTrees WHERE taskId=? ORDER BY levelCode,id", (task_id,))]
            by_id = {node["id"]: node for node in tree}
            defects = [dict(row) for row in database.execute(
                "SELECT id,treeId FROM outerCheckData WHERE taskId=?", (task_id,))]
            linked = Counter(row["treeId"] for row in defects)
            categories = []
            for category in tree:
                if category["nodeType"] != 2:
                    continue
                children = [node for node in tree if node["nodeType"] == 3
                            and node["levelCode"][:-3] == category["levelCode"]]
                names = Counter(node["name"] for node in children)
                categories.append({
                    "name": category["name"], "level_code": category["levelCode"],
                    "declared_member_count": category["memberCount"],
                    "concrete_nodes": len(children),
                    "nodes_with_defects": sum(node["id"] in linked for node in children),
                    "defect_records": sum(linked[node["id"]] for node in children),
                    "duplicate_names": {name: count for name, count in names.items() if count > 1},
                    "nodes": children,
                })
            properties = [dict(row) for row in database.execute(
                "SELECT GroupName,Name,DisplayName,Value FROM CustomPropertyValue "
                "WHERE taskId=? AND Name IN "
                "('桥梁分孔','结构体系','桥台','桥墩','支座','构件编号规则','桥长','最大跨径')",
                (task_id,))]
            review = json.loads(
                (Path(environment["log_directory"]) / f"source-db-review-{year}.json")
                .read_text(encoding="utf-8"))
            binding = []
            warnings = []
            range_total = 0
            for defect in review["parsed_result"]["defects"]:
                number = defect.get("component_number") or ""
                item_class = "single_number_requires_confirmed_inventory"
                expanded = None
                suggestion = None
                if "~" in number or "～" in number:
                    # Independently list the source's span/member form. This is an
                    # audit expectation, not a replacement for the C++ range parser.
                    match = re.fullmatch(
                        r"(\d+)-(\d+)(#.+)[~～](\d+)-(\d+)(#.+)", number)
                    item_class = "range_requires_preview"
                    if match and match[1] == match[4] and match[3] == match[6]:
                        first, last = int(match[2]), int(match[5])
                        if first < last and last - first < 1000:
                            expanded = [f"{match[1]}-{i}{match[3]}"
                                        for i in range(first, last + 1)]
                    if expanded is None:
                        item_class = "range_requires_manual_investigation"
                    range_total += len(expanded or [number])
                else:
                    range_total += 1
                    if "两侧" in number:
                        item_class = "two_sides_require_explicit_pair_selection"
                    elif (match := re.fullmatch(r"第(\d+)孔桥面", number)):
                        item_class = "legacy_pavement_number_requires_confirmation"
                        suggestion = f"{match[1]}#跨桥面铺装"
                binding.append({
                    "candidate_id": defect["candidate_id"],
                    "category": defect["component_name"], "number": number,
                    "classification": item_class, "expected_range_numbers": expanded,
                    "suggested_number_not_applied": suggestion,
                })
            for kind in ("defects", "photos"):
                for obj in review["parsed_result"][kind]:
                    if obj.get("warnings"):
                        warnings.append({
                            "kind": kind, "candidate_id": obj["candidate_id"],
                            "number": obj.get("component_number"),
                            "description": obj.get("defect_description"),
                            "warnings": obj["warnings"],
                        })
            years.append({
                "year": int(year), "task_id": task_id,
                "declared_total": sum(category["declared_member_count"] or 0
                                      for category in categories),
                "concrete_nodes": sum(len(category["nodes"]) for category in categories),
                "nodes_with_defects": len(linked),
                "orphan_defect_ids": [row["id"] for row in defects if row["treeId"] not in by_id],
                "categories": categories, "structural_properties": properties,
                "binding_class_counts": Counter(row["classification"] for row in binding),
                "after_numeric_range_expansion_only": range_total,
                "binding_prerequisites": binding, "object_warnings": warnings,
                "warning_code_counts": Counter(w["code"] for obj in warnings for w in obj["warnings"]),
            })
    finally:
        database.close()
    assert digest(snapshot) == source["snapshot_sha256"]
    assert digest(Path(source["source_path"])) == source["source_sha256_before"]
    result = {
        "source_unchanged": True, "snapshot_unchanged": True,
        "actual_inventory_matching_performed": False,
        "supplementary_table_rows": supplementary, "years": years,
    }
    (output / "inventory-profile.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({
        "report": str(output / "inventory-profile.json"),
        "years": [{key: year[key] for key in (
            "year", "declared_total", "concrete_nodes", "nodes_with_defects",
            "binding_class_counts", "after_numeric_range_expansion_only",
            "warning_code_counts")} for year in years],
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
