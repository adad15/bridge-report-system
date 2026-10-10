"""Create an isolated, disposable acceptance PostgreSQL cluster and app config."""
import datetime, hashlib, json, os, secrets, shutil, socket, subprocess, sys, uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
RUN=ROOT/"runtime"/"acceptance"/(datetime.datetime.now().strftime("%Y%m%d-%H%M%S")+"-"+uuid.uuid4().hex[:6])
LOG=ROOT/"logs"/"acceptance"/RUN.name
RUN.mkdir(parents=True); LOG.mkdir(parents=True)
PG=Path(os.environ.get("ACCEPTANCE_PG_BIN","D:/PostgreSQL/18/bin"))
for exe in ("initdb.exe","pg_ctl.exe","psql.exe"):
    if not (PG/exe).is_file(): raise SystemExit("Missing PostgreSQL tool: "+exe)
def port(first):
    for candidate in range(first,first+100):
        with socket.socket() as probe:
            try: probe.bind(("127.0.0.1",candidate))
            except OSError: continue
            return candidate
    raise RuntimeError("No free acceptance port")
ports={"postgres":port(25432),"backend":port(28480),"python":port(28580),"frontend":port(25173)}
password=secrets.token_urlsafe(24)
pw=RUN/"init-password.txt"; pw.write_text(password,encoding="utf-8")
flags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0
def run(args,log,env=None):
    with (LOG/log).open("w",encoding="utf-8") as output:
        subprocess.run([str(x) for x in args],stdout=output,stderr=subprocess.STDOUT,env=env,check=True,creationflags=flags)
manifest={"run_id":RUN.name,"run_directory":str(RUN),"log_directory":str(LOG),"ports":ports,"database":"bridge_report_acceptance","postgres_bin":str(PG),"status":"preparing"}
mp=LOG/"environment.json"
mp.write_text(json.dumps(manifest,indent=2),encoding="utf-8")
try:
    run([PG/"initdb.exe","-D",RUN/"postgres-data","-U","bridge_report","--pwfile",pw,"--auth=scram-sha-256","--encoding=UTF8","--locale=C"],"initdb.log")
finally: pw.unlink(missing_ok=True)
run([PG/"pg_ctl.exe","-D",RUN/"postgres-data","-l",LOG/"postgres.log","-o",f"-h 127.0.0.1 -p {ports['postgres']}","-w","start"],"postgres-start.log")
env=os.environ.copy()
for key in ("PGOPTIONS","PGSERVICE","PGSERVICEFILE","BRIDGE_REPORT_TEST_DATABASE_URL","BRIDGE_REPORT_TEST_SCHEMA"):
    env.pop(key,None)
env.update(PGHOST="127.0.0.1",PGPORT=str(ports["postgres"]),PGUSER="bridge_report",PGPASSWORD=password,PGDATABASE="postgres",PGCONNECT_TIMEOUT="5")
run([PG/"psql.exe","-X","-w","-v","ON_ERROR_STOP=1","-c","create database bridge_report_acceptance"],"create-db.log",env)
env["PGDATABASE"]=manifest["database"]
for sql in sorted((ROOT/"database/migrations").glob("*.sql")):
    run([PG/"psql.exe","-X","-w","-v","ON_ERROR_STOP=1","-f",sql],"migration-"+sql.stem+".log",env)
identity=subprocess.check_output([str(PG/"psql.exe"),"-X","-w","-tA","-c","select current_database(),current_schema(),inet_server_port()"],env=env,creationflags=flags).decode().strip()
assert identity==f"bridge_report_acceptance|public|{ports['postgres']}",identity
cfg=json.loads((ROOT/"config/local.example.json").read_text(encoding="utf-8"))
cfg["postgres"].update(host="127.0.0.1",port=ports["postgres"],database=manifest["database"],user="bridge_report",password=password)
cfg["cpp_server"].update(host="127.0.0.1",port=ports["backend"])
cfg["python_tools"].update(host="127.0.0.1",port=ports["python"],base_url=f"http://127.0.0.1:{ports['python']}")
cfg["frontend"].update(host="127.0.0.1",port=ports["frontend"],backend_base_url=f"http://127.0.0.1:{ports['backend']}")
cfg["archive"]["root"]=str(RUN/"archive")
cfg["temporary_storage"].update(root=str(RUN/"word-imports"),report_root=str(RUN/"report-jobs"))
cfg["logs"]["root"]=str(LOG)
cfg["standards"]["root"]=str(ROOT/"standards")
cfg["samples"]["root"]=str(ROOT/"samples")
config=RUN/"config.json";config.write_text(json.dumps(cfg,ensure_ascii=False,indent=2),encoding="utf-8")
samples=[p for p in (ROOT/"docs").glob("*.docx") if "百股" in p.name or "大凌河" in p.name]
manifest.update(status="database_ready",config_path=str(config),database_identity=identity,migrations=len(list((ROOT/"database/migrations").glob("*.sql"))),sources=[{"path":str(p),"bytes":p.stat().st_size,"sha256":hashlib.sha256(p.read_bytes()).hexdigest()} for p in samples])
mp.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps({"environment":str(mp),"identity":identity,"ports":ports,"migrations":manifest["migrations"]},ensure_ascii=False))
