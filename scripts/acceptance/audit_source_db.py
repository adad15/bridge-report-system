"""Compare real SQLite facts and image bytes with the UI-imported candidates."""
import base64,hashlib,json,re,sqlite3,sys
from collections import Counter
from pathlib import Path
source=json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
environment=json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
log=Path(environment["log_directory"]);report=Path(source["log_directory"])
config=json.loads(Path(environment["config_path"]).read_text(encoding="utf-8"))
archive=Path(config["archive"]["root"]).resolve()
summary=json.loads((log/"source-db-import-summary.json").read_text(encoding="utf-8"))
db=sqlite3.connect(Path(source["snapshot_path"]).as_uri()+"?mode=ro",uri=True)
db.row_factory=sqlite3.Row
def candidate_key(d):
 return (d["component_number"],d["defect_location"],d["defect_description"],d["defect_scale"],d["source_defect_group_id"],d["source_defect_indicator_id"])
results=[]
try:
 for year,item in summary["years"].items():
  review=json.loads((log/f"source-db-review-{year}.json").read_text(encoding="utf-8"))
  data=review["parsed_result"];candidates={d["candidate_id"]:d for d in data["defects"]}
  rows=list(db.execute("select d.*,t.name as component_number from outerCheckData d left join taskTrees t on t.id=d.treeId and t.taskId=d.taskId where d.taskId=?",(item["taskId"],)))
  def source_key(row):
   return (row["component_number"],row["pos"] or "",row["data"] or "",row["degree"],row["judgeTreeId"] or None,row["judgeIndexId"] or None)
  assert Counter(source_key(r) for r in rows)==Counter(candidate_key(d) for d in candidates.values()),year
  expected_by_fact={}
  for d in candidates.values():expected_by_fact.setdefault(candidate_key(d),[]).append(d)
  dimension_count=0
  nonnumeric_dimensions=[]
  for row in rows:
   matches=expected_by_fact[source_key(row)]
   for column,dimension in [("数量","数量"),("长度","长度"),("宽度","宽度"),("面积一","面积")]:
    if row[column] in (None,""):continue
    try:value=float(row[column])
    except ValueError:
     bounds=re.fullmatch(r"\s*(\d+(?:\.\d+)?)\s*~\s*(\d+(?:\.\d+)?)\s*",str(row[column]))
     assert bounds is not None,("Unsupported source dimension",year,row["id"],column,row[column])
     low,high=map(float,bounds.groups())
     assert any(any(m["dimension_type"]==dimension and m["value_type"]=="range" and m["minimum_value"]==low and m["maximum_value"]==high and m["unit"]==(row[column+"单位"] or "") for m in d["measurements"]) for d in matches),(year,row["id"],column,"range mismatch")
     nonnumeric_dimensions.append({"source_defect_id":row["id"],"column":column,"raw":row[column],"unit":row[column+"单位"],"description":row["data"],"imported_measurements":[d["measurements"] for d in matches]})
     continue
    unit=row[column+"单位"] or ""
    assert any(any(m["dimension_type"]==dimension and m["value_type"]=="single" and m["value"]==value and m["unit"]==unit for m in d["measurements"]) for d in matches),(year,row["id"],column)
    dimension_count+=1
  facts={r["id"]:source_key(r) for r in rows}
  expected_photos=Counter()
  for row in db.execute("select ForeignKey,fileName from images where taskId=? and ForeignTable='outerCheckData'",(item["taskId"],)):
   assert row["ForeignKey"] in facts
   encoded=row["fileName"].split(",",1)[-1] if row["fileName"].startswith("data:") else row["fileName"]
   payload=base64.b64decode(encoded)
   expected_photos[(facts[row["ForeignKey"]],hashlib.sha256(payload).hexdigest())]+=1
  actual_photos=Counter()
  for photo in data["photos"]:
   path=(archive/photo["extracted_file"]["archive_relative_path"]).resolve()
   assert path.is_relative_to(archive)
   actual_photos[(candidate_key(candidates[photo["linked_defect_candidate_id"]]),hashlib.sha256(path.read_bytes()).hexdigest())]+=1
  assert expected_photos==actual_photos,year
  warnings=Counter(w["code"] for d in candidates.values() for w in d["warnings"])
  groups=dict(db.execute("select ForeignTable,count(*) from images where taskId=? group by ForeignTable",(item["taskId"],)))
  results.append({"year":int(year),"defects":len(rows),"structured_numeric_measurements":dimension_count,"structured_range_measurements":len(nonnumeric_dimensions),"verified_source_ranges":nonnumeric_dimensions,"object_warning_count":review["statistics"]["object_warning_count"],"defect_photos":len(data["photos"]),"all_photo_bytes_and_defect_links_match":True,"source_photo_groups":groups,"warnings":warnings,"bridge_check":data["bridge_check"],"ui_import_ms":item["elapsedMs"]})
finally:db.close()
def digest(path):
 with Path(path).open("rb") as stream:return hashlib.file_digest(stream,"sha256").hexdigest()
assert digest(source["source_path"])==source["source_sha256_before"]
assert digest(source["snapshot_path"])==source["snapshot_sha256"]
result={"source_unchanged":True,"snapshot_unchanged":True,"years":results,"formal_confirmation":"not_performed"}
(report/"source-data-audit.json").write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps(result,ensure_ascii=False,indent=2))
