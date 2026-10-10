"""Read-only SQLite backup for repeatable source-software acceptance."""
import datetime,hashlib,json,os,sqlite3,sys,uuid
from pathlib import Path
root=Path(__file__).resolve().parents[2]
source=(Path(sys.argv[1]) if len(sys.argv)>1 else Path(os.environ["APPDATA"])/"datacheck.hitek.com/databases/https_bridge.ilis.cn_0/1").resolve()
if not source.is_file():raise SystemExit("Source database not found: "+str(source))
run_id=datetime.datetime.now().strftime("%Y%m%d-%H%M%S")+"-"+uuid.uuid4().hex[:6]
out=root/"runtime"/"source-db-acceptance"/run_id
log=root/"logs"/"source-db-acceptance"/run_id
out.mkdir(parents=True);log.mkdir(parents=True)
snapshot=out/"source-snapshot.sqlite"
def digest(path):
 with path.open("rb") as stream:return hashlib.file_digest(stream,"sha256").hexdigest()
before=digest(source)
src=sqlite3.connect(source.as_uri()+"?mode=ro",uri=True)
dst=sqlite3.connect(snapshot)
try:
 src.backup(dst)
 assert dst.execute("pragma quick_check").fetchone()[0]=="ok"
finally:
 dst.close();src.close()
sys.path.insert(0,str(root/"tools-python"))
from bridge_report_tools.importers.source_db.reader import open_source_db,load_task_summaries
db=open_source_db(snapshot)
try:
 tasks=[vars(t) for t in load_task_summaries(db)]
finally:db.close()
after=digest(source)
manifest={"run_id":run_id,"source_path":str(source),"snapshot_path":str(snapshot),"log_directory":str(log),
 "source_bytes":source.stat().st_size,"source_sha256_before":before,"source_sha256_after":after,
 "source_unchanged":before==after,"snapshot_sha256":digest(snapshot),"tasks":tasks}
target=log/"source-environment.json"
target.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps({"manifest":str(target),"snapshot":str(snapshot),"source_unchanged":before==after,"task_count":len(tasks),"baigu_tasks":[t for t in tasks if "百股" in t["name"]]},ensure_ascii=False))
