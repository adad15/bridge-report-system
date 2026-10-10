"""Read-only verification of the 2026 isolated source-database confirmation."""
from __future__ import annotations
import hashlib, json, os, subprocess, sys
from collections import Counter
from pathlib import Path
from uuid import UUID

def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))
def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream,"sha256").hexdigest()
def signature(component, facts):
    return (component, facts.get("defect_location") or "", facts["defect_description"],
            None if facts.get("defect_scale") is None else str(facts["defect_scale"]))

env_manifest=read(sys.argv[1])
source_manifest=read(sys.argv[2])
log=Path(env_manifest["log_directory"])
state=read(Path(env_manifest["run_directory"])/"scenario.json")
config=read(env_manifest["config_path"])
pg=config["postgres"]
assert pg["host"] in ("127.0.0.1","localhost") and pg["port"]==25432
assert pg["database"]=="bridge_report_acceptance"
import_id=str(UUID(state["sourceConfirmation"]["importId"]))
bridge_id=state["sourceDatabase"]["bridge"]["id"]
year_id=state["sourceDatabase"]["years"]["2026"]["year"]["id"]
process_env=os.environ.copy()
for key in ("PGSERVICE","PGSERVICEFILE","PGOPTIONS"):
    process_env.pop(key,None)
process_env.update(PGHOST=pg["host"],PGPORT=str(pg["port"]),PGDATABASE=pg["database"],
    PGUSER=pg["user"],PGPASSWORD=pg["password"],PGCLIENTENCODING="UTF8",
    PGOPTIONS="-c default_transaction_read_only=on -c statement_timeout=15000")
sql=f"""
select json_build_object(
 'identity',json_build_object('database',current_database(),'port',inet_server_port(),'read_only',current_setting('transaction_read_only')),
 'observations',(select json_agg(row_to_json(o)) from (
   select d.*,
     (select coalesce(json_agg(row_to_json(m)),'[]'::json) from defect_measurements m where m.defect_observation_id=d.id) measurements,
     (select coalesce(json_agg(row_to_json(p)),'[]'::json) from (
        select p.*,a.storage_relative_path,a.file_hash from defect_photos p join archived_files a on a.id=p.archived_file_id
        where p.defect_observation_id=d.id) p) photos
   from defect_observations d where d.source_import_record_id='{import_id}'::uuid) o),
 'other_year_observations',(select count(*) from defect_observations where bridge_id='{bridge_id}'::uuid and inspection_year_id<>'{year_id}'::uuid),
 'year',(select row_to_json(y) from inspection_years y where id='{year_id}'::uuid),
 'import_status',(select import_status from import_records where id='{import_id}'::uuid)
);
"""
result=subprocess.run(["D:/PostgreSQL/18/bin/psql.exe","-X","-w","-qAt","-v","ON_ERROR_STOP=1"],
    input=sql.encode("utf-8"),env=process_env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
    creationflags=subprocess.CREATE_NO_WINDOW)
if result.returncode:
    raise SystemExit(result.stderr.decode("utf-8"))
data=json.loads(result.stdout.decode("utf-8"))
assert data["identity"]=={"database":"bridge_report_acceptance","port":25432,"read_only":"on"}
(log/"source-confirm-database.json").write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding="utf-8")
observations=data["observations"]
assert len(observations)==430 and data["other_year_observations"]==0
assert data["import_status"]=="已确认"
assert data["year"]["component_inventory_revision_id"]==state["sourceConfirmation"]["inventory"]["revision"]["id"]
assert all(o["bridge_id"]==bridge_id and o["inspection_year_id"]==year_id for o in observations)
initial=read(log/"source-confirm-initial-review.json")["parsed_result"]
review=read(log/"source-confirm-final-review.json")["parsed_result"]
workspace=read(log/"source-confirm-reviewed-workspace.json")
sources={d["candidate_id"]:d for d in initial["defects"]}
assert len(sources)==381
for d in review["defects"]:
    for key in ("component_number","component_name","defect_description","defect_location","measurements","measurement_text","quantity_text","defect_scale"):
        assert d[key]==sources[d["candidate_id"]][key],(d["candidate_id"],key)
expected=[]
photo_owners={}
split_counts={}
for group in workspace["groups"]:
    assert group["status"]=="bound"
    for member in group["members"]:
        active=[i for i in member["instances"] if i["instance_status"]=="active"]
        assert sum(i["is_photo_owner"] for i in active)==1
        if len(active)>1:
            split_counts[member["source_candidate_id"]]=len(active)
        for instance in active:
            facts=instance["effective_facts"]
            sig=signature(instance["bridge_component_id"],facts)
            expected.append((sig,facts))
            if instance["is_photo_owner"]:
                photo_owners[member["source_candidate_id"]]=sig
actual_signatures={}
for o in observations:
    sig=(o["bridge_component_id"],o["defect_location"] or "",o["defect_description_raw"],o["scale"])
    actual_signatures[o["id"]]=sig
assert Counter(sig for sig,_ in expected)==Counter(actual_signatures.values())
expected_dimensions=Counter()
for sig,facts in expected:
    for m in facts["measurements"]:
        expected_dimensions[(sig,m["dimension_type"],m["value_type"],m["value"],m["minimum_value"],m["maximum_value"],m["unit"],m["source_text"])]+=1
actual_dimensions=Counter()
unparsed=0
for o in observations:
    for m in o["measurements"]:
        if m["measurement_type"]=="未识别尺寸":
            assert m["raw_text"]==next(f["measurement_text"] for s,f in expected if s==actual_signatures[o["id"]])
            unparsed+=1
        else:
            actual_dimensions[(actual_signatures[o["id"]],m["measurement_type"],m["value_type"],m["numeric_value"],m["minimum_value"],m["maximum_value"],m["unit"],m["raw_text"])]+=1
assert actual_dimensions==expected_dimensions,(expected_dimensions-actual_dimensions,actual_dimensions-expected_dimensions)
photo_by_path={p["extracted_file"]["archive_relative_path"]:p for p in initial["photos"]}
assert len(photo_by_path)==455
seen=[]
archive=Path(config["archive"]["root"]).resolve()
for o in observations:
    for p in o["photos"]:
        original=photo_by_path[p["storage_relative_path"]]
        assert actual_signatures[o["id"]]==photo_owners[original["linked_defect_candidate_id"]]
        assert p["source_import_record_id"]==import_id and p["photo_number"] is None
        path=(archive/p["storage_relative_path"]).resolve()
        assert path.is_relative_to(archive)
        expected_hash=original["extracted_file"].get("file_hash")
        actual_hash=digest(path)
        if expected_hash:
            assert actual_hash==expected_hash.removeprefix("sha256:")
        assert actual_hash==p["file_hash"].removeprefix("sha256:")
        seen.append(p["storage_relative_path"])
assert len(seen)==455 and len(set(seen))==455
origins=Counter()
for o in observations:
    origin=o["source_raw_cells_json"].get("range_split_origin")
    if origin:
        candidate=origin["source_candidate_id"]
        assert origin["source_component_number"]==sources[candidate]["component_number"]
        assert origin["split_count"]==split_counts[candidate]
        origins[candidate]+=1
assert dict(origins)==split_counts
assert digest(source_manifest["source_path"])==source_manifest["source_sha256_before"]
assert digest(source_manifest["snapshot_path"])==source_manifest["snapshot_sha256"]
report={"passed":True,"identity":data["identity"],"source_records":381,"bound_groups":333,
 "observations":len(observations),"structured_measurements":sum(actual_dimensions.values()),
 "unquantified_text_rows":unparsed,"measurements":sum(len(o["measurements"]) for o in observations),
 "photos":len(seen),"unique_photo_owners_verified":True,"photo_hashes_verified":455,
 "split_source_counts":split_counts,"source_facts_and_measurements_unchanged":True,
 "original_and_snapshot_hashes_unchanged":True,"other_year_observations":data["other_year_observations"],
 "inventory_revision":data["year"]["component_inventory_revision_id"]}
(log/"source-confirm-verification.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps(report,ensure_ascii=False,indent=2))
