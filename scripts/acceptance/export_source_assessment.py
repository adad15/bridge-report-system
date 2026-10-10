"""Export the isolated formal assessment and its source facts through a forced read-only connection."""
from __future__ import annotations
import argparse, json, os, subprocess
from pathlib import Path
from uuid import UUID

def load(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest",type=Path)
    args=parser.parse_args()
    m=load(args.manifest)
    assert m["database"]=="bridge_report_acceptance" and m["ports"]["postgres"]==25432
    state=load(Path(m["run_directory"])/"scenario.json")
    record=str(UUID(state["sourceConfirmation"]["importId"]))
    year=str(UUID(state["sourceDatabase"]["years"]["2026"]["year"]["id"]))
    bridge=str(UUID(state["sourceDatabase"]["bridge"]["id"]))
    run=str(UUID(state["sourceConfirmation"]["confirmation"]["assessment_run_id"]))
    pg=load(Path(m["config_path"]))["postgres"]
    assert pg["database"]==m["database"] and pg["port"]==25432 and pg["host"] in ("127.0.0.1","localhost")
    env=os.environ.copy()
    for key in ("PGSERVICE","PGSERVICEFILE","PGOPTIONS"): env.pop(key,None)
    env.update(PGHOST=pg["host"],PGPORT=str(pg["port"]),PGDATABASE=pg["database"],
        PGUSER=pg["user"],PGPASSWORD=pg["password"],PGCLIENTENCODING="UTF8",
        PGOPTIONS="-c default_transaction_read_only=on -c statement_timeout=60000")
    sql=f"""
select json_build_object(
 'identity',json_build_object('database',current_database(),'port',inet_server_port(),'read_only',current_setting('transaction_read_only')),
 'run',(select row_to_json(r) from (
  select id,inspection_year_id,source_import_record_id,run_kind,formal_revision_number,
    technical_condition_package_id,component_inventory_revision_id,result_status,is_current,
    input_summary_json,input_checksum,rule_package_summary_json,rule_package_checksum,result_summary_json
  from assessment_runs where id='{run}'::uuid) r),
 'components',(select json_agg(row_to_json(c)) from (
  select bridge_component_id,standard_component_category_id,structure_part,score,grade,deduction,result_json
  from assessment_component_results where assessment_run_id='{run}'::uuid order by bridge_component_id) c),
 'parts',(select json_agg(row_to_json(p)) from (
  select result_level,result_key,parent_result_key,structure_part,standard_component_category_id,score,grade,weight,result_json
  from assessment_part_results where assessment_run_id='{run}'::uuid order by result_level,result_key) p),
 'controls',(select coalesce(json_agg(row_to_json(c)),'[]'::json) from (
  select rule_id,control_level,target_key,triggered,score_before,score_after,grade_before,grade_after,input_json,output_json
  from assessment_control_results where assessment_run_id='{run}'::uuid order by rule_id) c),
 'traces',(select json_agg(row_to_json(t)) from (
  select sequence_number,rule_id,trace_stage,target_type,target_key,input_json,output_json
  from assessment_rule_traces where assessment_run_id='{run}'::uuid order by sequence_number) t),
 'observations',(select json_agg(row_to_json(o)) from (
  select id,bridge_component_id,defect_location,rating_tree_node_id,standard_defect_indicator_id,
    defect_type,defect_description_raw,scale,review_status,source_raw_cells_json
  from defect_observations where source_import_record_id='{record}'::uuid order by id) o),
 'inventory',(select json_agg(row_to_json(i)) from (
  select e.bridge_component_id,e.component_number,e.site_component_type,
    m.standard_component_category_id,m.structure_part
  from bridge_component_inventory_entries e
  join bridge_component_standard_mappings m on m.inventory_entry_id=e.id and m.is_active
  where e.inventory_revision_id=(select component_inventory_revision_id from assessment_runs where id='{run}'::uuid)
    and e.is_active order by e.bridge_component_id) i),
 'year',(select row_to_json(y) from (
  select id,bridge_id,inspection_year,version_number,is_current,component_inventory_revision_id,overall_score,overall_grade
  from inspection_years where id='{year}'::uuid and bridge_id='{bridge}'::uuid) y)
);
"""
    result=subprocess.run(["D:/PostgreSQL/18/bin/psql.exe","-X","-w","-qAt","-v","ON_ERROR_STOP=1"],
        input=sql.encode("utf-8"),env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
    if result.returncode: raise RuntimeError(result.stderr.decode("utf-8"))
    data=json.loads(result.stdout.decode("utf-8"))
    assert data["identity"]=={"database":"bridge_report_acceptance","port":25432,"read_only":"on"}
    assert data["run"]["id"]==run and data["run"]["source_import_record_id"]==record
    assert data["run"]["inspection_year_id"]==year and data["run"]["run_kind"]=="正式"
    assert data["run"]["result_status"]=="成功" and data["run"]["is_current"]
    assert data["year"]["component_inventory_revision_id"]==data["run"]["component_inventory_revision_id"]
    assert len(data["observations"])==430 and len(data["components"])==5174
    log=Path(m["log_directory"])
    output=log/"source-assessment-raw.json"
    output.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding="utf-8")
    summary={
      "output":str(output),"bytes":output.stat().st_size,"run_id":run,
      "counts":{k:len(data[k]) for k in ("components","parts","controls","traces","observations","inventory")},
      "result":{k:data["run"]["result_summary_json"]["result"][k] for k in ("overall_score","calculated_grade","final_grade","bridge_type_id")},
      "trace_stages":{s:sum(t["trace_stage"]==s for t in data["traces"]) for s in sorted({t["trace_stage"] for t in data["traces"]})},
      "samples":{
       "component":{k:data["components"][0][k] for k in ("bridge_component_id","standard_component_category_id","score")},
       "part":{k:data["parts"][0][k] for k in ("result_level","result_key","score","grade")},
       "trace":{k:data["traces"][0][k] for k in ("sequence_number","rule_id","trace_stage")},
       "observation":{k:data["observations"][0][k] for k in ("bridge_component_id","standard_defect_indicator_id","scale")},
       "inventory":{k:data["inventory"][0][k] for k in ("bridge_component_id","component_number","standard_component_category_id")}
      }
    }
    print(json.dumps(summary,ensure_ascii=False,indent=2))
if __name__=="__main__": main()
