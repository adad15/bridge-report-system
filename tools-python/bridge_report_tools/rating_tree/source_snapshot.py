"""Export the source application's bridge rating tree as a sanitized snapshot."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sqlite3
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

SOURCE_TABLE_COLUMNS = {
    "judgeTree": ("id", "chapterNum", "name", "levelCode"),
    "judgeIndex": ("id", "tableNum", "name"),
    "judgeTree2Index": ("id", "treeId", "indexId", "displayOrder"),
}
BRIDGE_CHAPTER = re.compile(r"^(?:[5-9]|10)(?:\.|$)")


def _columns(db: sqlite3.Connection, table: str) -> set[str]:
    return {str(row[1]) for row in db.execute(f'pragma table_info("{table}")')}


def _validate_schema(db: sqlite3.Connection) -> None:
    tables = {
        str(row[0])
        for row in db.execute("select name from sqlite_master where type='table'")
    }
    for table, required in SOURCE_TABLE_COLUMNS.items():
        if table not in tables:
            raise ValueError(f"source database is missing table {table}")
        missing = set(required) - _columns(db, table)
        if missing:
            raise ValueError(
                f"source database table {table} is missing columns "
                + ", ".join(sorted(missing)))


def _number_key(value: str) -> tuple[tuple[int, ...], str]:
    return tuple(int(part) for part in re.findall(r"\d+", value)), value


def export_snapshot(
    source_db: str | Path,
    *,
    exported_at: str | None = None,
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Return `(source_tree, metadata)` without business records or local paths."""
    path = Path(source_db)
    if not path.is_file():
        raise FileNotFoundError(path)
    database_sha256 = hashlib.sha256(path.read_bytes()).hexdigest()
    db = sqlite3.connect(f"file:{path.as_posix()}?mode=ro", uri=True)
    try:
        _validate_schema(db)
        all_groups = [
            {
                "source_group_id": str(group_id),
                "display_number": str(number),
                "display_name": str(name),
                "level_code": str(level_code),
            }
            for group_id, number, name, level_code in db.execute(
                "select id, chapterNum, name, levelCode from judgeTree")
            if BRIDGE_CHAPTER.match(str(number))
        ]
        group_ids = {group["source_group_id"] for group in all_groups}
        relation_rows = [
            row
            for row in db.execute(
                "select id, treeId, indexId, displayOrder from judgeTree2Index")
            if str(row[1]) in group_ids
        ]
        indicator_ids = {str(row[2]) for row in relation_rows}
        indicators = [
            {
                "source_indicator_id": str(indicator_id),
                "display_number": str(number),
                "display_name": str(name),
            }
            for indicator_id, number, name in db.execute(
                "select id, tableNum, name from judgeIndex")
            if str(indicator_id) in indicator_ids
        ]
    finally:
        db.close()

    groups = sorted(
        all_groups,
        key=lambda item: (
            item["level_code"],
            _number_key(item["display_number"]),
            item["source_group_id"],
        ),
    )
    indicators.sort(
        key=lambda item: (
            _number_key(item["display_number"]),
            item["display_name"],
            item["source_indicator_id"],
        ))
    group_by_id = {item["source_group_id"]: item for item in groups}
    indicator_by_id = {item["source_indicator_id"]: item for item in indicators}
    relations = [
        {
            "source_relation_id": str(relation_id),
            "source_group_id": str(group_id),
            "source_indicator_id": str(indicator_id),
            "display_order": int(display_order),
        }
        for relation_id, group_id, indicator_id, display_order in relation_rows
    ]
    relations.sort(key=lambda item: (
        group_by_id[item["source_group_id"]]["level_code"],
        item["display_order"],
        _number_key(indicator_by_id[item["source_indicator_id"]]["display_number"]),
        item["source_relation_id"],
    ))
    pair_count = len({
        (item["source_group_id"], item["source_indicator_id"])
        for item in relations
    })
    if pair_count != len(relations):
        raise ValueError("source bridge tree contains duplicate group-indicator pairs")

    source_tree = {
        "snapshot_version": 1,
        "included_chapters": [5, 6, 7, 8, 9, 10],
        "groups": groups,
        "indicators": indicators,
        "relations": relations,
    }
    metadata = {
        "snapshot_version": 1,
        "exported_at": exported_at or datetime.now(timezone.utc).isoformat(),
        "source_database_sha256": f"sha256:{database_sha256}",
        "source_table_row_counts": {
            "judgeTree": len(groups),
            "judgeIndex": len(indicators),
            "judgeTree2Index": len(relations),
        },
        "unique_group_indicator_pairs": pair_count,
        "contains_business_records": False,
    }
    return source_tree, metadata


def _write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def write_snapshot(
    source_db: str | Path,
    output_dir: str | Path,
    *,
    exported_at: str | None = None,
) -> None:
    tree, metadata = export_snapshot(source_db, exported_at=exported_at)
    target = Path(output_dir)
    _write_json(target / "source-tree.json", tree)
    _write_json(target / "source-metadata.json", metadata)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-db", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--exported-at")
    args = parser.parse_args()
    write_snapshot(args.source_db, args.output_dir, exported_at=args.exported_at)


if __name__ == "__main__":
    main()
