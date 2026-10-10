"""Export one confirmed inventory revision through a forced read-only PostgreSQL session."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from uuid import UUID


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    parser.add_argument("revision_id", type=UUID)
    parser.add_argument("output", type=Path)
    parser.add_argument("--psql", default="D:/PostgreSQL/18/bin/psql.exe")
    args = parser.parse_args()
    config = json.loads(args.config.read_text(encoding="utf-8-sig"))["postgres"]
    env = os.environ.copy()
    for key in ("PGSERVICE", "PGSERVICEFILE", "PGOPTIONS"):
        env.pop(key, None)
    env.update(
        PGHOST=config["host"], PGPORT=str(config["port"]), PGDATABASE=config["database"],
        PGUSER=config["user"], PGPASSWORD=config["password"], PGCONNECT_TIMEOUT="5",
        PGCLIENTENCODING="UTF8",
        PGOPTIONS="-c default_transaction_read_only=on -c statement_timeout=10000",
    )
    # UUID parsing above constrains interpolation; credentials never enter SQL or output.
    sql = f"""
WITH chosen AS (
 SELECT id,bridge_id,revision_number,status,confirmed_at
 FROM bridge_component_inventory_revisions WHERE id='{args.revision_id}'::uuid
)
SELECT json_build_object(
 'read_only',current_setting('transaction_read_only'),
 'revision',(SELECT row_to_json(r) FROM chosen r),
 'bridge',(SELECT json_build_object('id',b.id,'bridge_name',b.bridge_name)
           FROM bridges b JOIN chosen r ON r.bridge_id=b.id),
 'years',(SELECT json_agg(row_to_json(y)) FROM (
    SELECT inspection_year,component_inventory_revision_id FROM inspection_years
    WHERE bridge_id=(SELECT bridge_id FROM chosen) ORDER BY inspection_year) y),
 'aliases',(SELECT coalesce(json_agg(row_to_json(a)),'[]'::json) FROM (
    SELECT ca.bridge_component_id,ca.alias_text FROM component_aliases ca
    JOIN bridge_components c ON c.id=ca.bridge_component_id
    WHERE c.bridge_id=(SELECT bridge_id FROM chosen) AND ca.is_manually_confirmed
    ORDER BY ca.bridge_component_id,ca.alias_text) a),
 'entries',(SELECT json_agg(row_to_json(e)) FROM (
    SELECT e.id,e.bridge_component_id,e.component_number,e.site_component_type,e.is_active,
      (SELECT coalesce(json_agg(row_to_json(m)),'[]'::json) FROM (
        SELECT standard_component_category_id,structure_part,confirmation_status,is_active
        FROM bridge_component_standard_mappings m WHERE m.inventory_entry_id=e.id
        ORDER BY m.is_active DESC,m.created_at,m.id) m) AS mappings
    FROM bridge_component_inventory_entries e
    WHERE e.inventory_revision_id=(SELECT id FROM chosen) ORDER BY e.sort_order,e.id) e)
);
"""
    result = subprocess.run(
        [args.psql, "-X", "-w", "-qAt", "-v", "ON_ERROR_STOP=1"],
        input=sql.encode("utf-8"), env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
    )
    if result.returncode:
        raise SystemExit("Read-only inventory export failed; check the configured PostgreSQL connection.")
    data = json.loads(result.stdout.decode("utf-8"))
    if data["read_only"] != "on" or not data["revision"]:
        raise SystemExit("Read-only session or requested revision is unavailable.")
    if data["revision"]["status"] not in ("已确认", "confirmed"):
        raise SystemExit("The inventory revision is not confirmed.")
    data["exported_at"] = datetime.now(timezone.utc).isoformat()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({
        "output": str(args.output), "revision": data["revision"],
        "entries": len(data["entries"]), "aliases": len(data["aliases"]),
        "sha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
