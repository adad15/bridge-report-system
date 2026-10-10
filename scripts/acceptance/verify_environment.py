"""Assert that the running acceptance services and database are isolated."""
import hashlib,json,os,subprocess,sys,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[2]
m=json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"));run=Path(m["run_directory"]).resolve();log=Path(m["log_directory"])
assert run.parent==root/"runtime"/"acceptance"
cfg=json.loads(Path(m["config_path"]).read_text(encoding="utf-8"))
for value in [cfg["archive"]["root"],cfg["temporary_storage"]["root"],cfg["temporary_storage"]["report_root"]]:
 assert Path(value).resolve().is_relative_to(run)
assert cfg["postgres"]["host"]=="127.0.0.1"
assert cfg["postgres"]["database"]==m["database"]=="bridge_report_acceptance"
assert cfg["postgres"]["port"]==m["ports"]["postgres"]
def get(port,path):
 with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}",timeout=5) as response:return json.load(response)
health={k:get(m["ports"][k],"/health") for k in ["backend","python"]}
for k,h in health.items():
 assert h["launch_id"]==m["run_id"] and h["port"]==m["ports"][k]
 assert h["source_digest"]
assert Path(health["backend"]["archive_root_absolute"]).resolve()==run/"archive"
assert health["backend"]["python_tools_base_url"]==f"http://127.0.0.1:{m['ports']['python']}"
sys.path.insert(0,str(root/"tools-python"))
from bridge_report_tools.runtime_identity import capture_identity
assert health["python"]["source_digest"]==capture_identity()["source_digest"]
pg=cfg["postgres"];env=os.environ.copy()
for key in ("PGOPTIONS","PGSERVICE","PGSERVICEFILE"):env.pop(key,None)
env.update(PGHOST=pg["host"],PGPORT=str(pg["port"]),PGDATABASE=pg["database"],PGUSER=pg["user"],PGPASSWORD=pg["password"])
query="select json_build_object('database',current_database(),'port',inet_server_port(),'schema',current_schema(),'other_sessions',(select count(*) from pg_stat_activity where datname=current_database() and pid<>pg_backend_pid()))"
data=subprocess.check_output([str(Path(m["postgres_bin"])/"psql.exe"),"-X","-w","-tA","-c",query],env=env,creationflags=subprocess.CREATE_NO_WINDOW).decode().strip()
identity=json.loads(data)
assert identity["database"]==m["database"] and identity["port"]==m["ports"]["postgres"] and identity["schema"]=="public"
assert identity["other_sessions"]>0
for source in m["sources"]:
 assert hashlib.sha256(Path(source["path"]).read_bytes()).hexdigest()==source["sha256"]
summary={"passed":True,"health":health,"database_identity":identity,"source_hashes_unchanged":True}
(log/"environment-verified.json").write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps({"passed":True,"database_identity":identity},ensure_ascii=False))
