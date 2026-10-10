"""Reparse the fixed source snapshot and compare it with the earlier UI evidence."""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools-python"))
from bridge_report_tools.importers.source_db.context import SourceImportRequest, parse_source_import


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def dimensions_without_provenance(defect: dict) -> Counter:
    return Counter(tuple(sorted((key, value) for key, value in item.items()
                                if key != "source_text"))
                   for item in defect["measurements"])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_manifest", type=Path)
    parser.add_argument("acceptance_manifest", type=Path)
    args = parser.parse_args()
    source = json.loads(args.source_manifest.read_text(encoding="utf-8"))
    environment = json.loads(args.acceptance_manifest.read_text(encoding="utf-8"))
    config = json.loads(Path(environment["config_path"]).read_text(encoding="utf-8"))
    archive = Path(config["archive"]["root"]).resolve()
    evidence = Path(environment["log_directory"])
    summary = json.loads((evidence / "source-db-import-summary.json").read_text(encoding="utf-8"))
    log = Path(source["log_directory"]) / "binding-and-fidelity"
    log.mkdir(exist_ok=True)
    runtime = Path(source["snapshot_path"]).parent / "fidelity-replay"
    results = []
    for year, item in sorted(summary["years"].items()):
        old = json.loads((evidence / f"source-db-review-{year}.json").read_text(encoding="utf-8"))["parsed_result"]
        response = parse_source_import(SourceImportRequest(
            source_db_path=Path(source["snapshot_path"]), task_id=item["taskId"],
            temporary_photo_output_dir=runtime / year,
            import_mode="已有桥年度导入",
            file_role=old["import_context"]["file_role"], data_role=old["inspection"]["data_role"],
            selected_bridge_system_number=old["bridge_check"]["selected_bridge_system_number"],
            selected_bridge_name="百股大桥", inspection_year=int(year),
            inspection_date=old["inspection"]["inspection_date"],
            report_number=old["inspection"]["report_number"], project_name=old["inspection"]["project_name"],
            archived_file_system_number=old["import_context"]["archived_file_system_number"],
            import_record_system_number=old["import_context"]["import_record_system_number"],
        ))
        new = json.loads(response.data.model_dump_json())
        assert new["contract"]["parser_version"] == "0.1.1"
        before = {defect["candidate_id"]: defect for defect in old["defects"]}
        after = {defect["candidate_id"]: defect for defect in new["defects"]}
        assert before.keys() == after.keys()
        changed = []
        for candidate_id, defect in after.items():
            original = before[candidate_id]
            assert {k: v for k, v in defect.items() if k != "measurements"} == {
                k: v for k, v in original.items() if k != "measurements"
            }, (year, candidate_id, "facts changed")
            assert dimensions_without_provenance(defect) == dimensions_without_provenance(original), (year, candidate_id)
            if defect["measurements"] != original["measurements"]:
                changed.append(candidate_id)
        old_photos = {photo["candidate_id"]: photo for photo in old["photos"]}
        assert old_photos.keys() == {photo["candidate_id"] for photo in new["photos"]}
        for photo in new["photos"]:
            original = old_photos[photo["candidate_id"]]
            assert {k: v for k, v in photo.items() if k != "extracted_file"} == {
                k: v for k, v in original.items() if k != "extracted_file"
            }
            assert photo["extracted_file"]["original_caption"] == original["extracted_file"]["original_caption"]
            archived = (archive / original["extracted_file"]["archive_relative_path"]).resolve()
            assert archived.is_relative_to(archive)
            extracted = runtime / year / photo["extracted_file"]["temporary_file_name"]
            assert digest(extracted) == digest(archived), (year, photo["candidate_id"])
        (log / f"reparsed-{year}.json").write_text(
            json.dumps(new, ensure_ascii=False, indent=2), encoding="utf-8")
        results.append({
            "year": int(year), "defects": len(after), "photos": len(new["photos"]),
            "facts_and_measurement_values_unchanged": True,
            "photo_bytes_and_links_unchanged": True,
            "dimension_provenance_or_order_changes": changed,
        })
    assert digest(Path(source["source_path"])) == source["source_sha256_before"]
    assert digest(Path(source["snapshot_path"])) == source["snapshot_sha256"]
    result = {"source_unchanged": True, "parser_version": "0.1.1", "years": results}
    (log / "fidelity-replay.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
