"""Normalize a legacy Word sample into a separate DOCX, preserving the original."""
import hashlib,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"tools-python"))
from bridge_report_tools.reports.field_updater import microsoft_word_updater
manifest=json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
sample=manifest["sources"][1];source=Path(sample["path"])
target=Path(manifest["run_directory"])/"samples"/"dalinghe-normalized.docx"
assert not target.exists(),"Refusing to overwrite a prior conversion"
updater=microsoft_word_updater(timeout_seconds=120)
def convert(invocation):
    app=updater._new_app(invocation);doc=None
    try:
        doc=app.Documents.Open(FileName=str(source),ReadOnly=True,Visible=False,AddToRecentFiles=False)
        doc.SaveAs2(FileName=str(target),FileFormat=16,AddToRecentFiles=False)
    finally:
        if doc is not None:doc.Close(0)
        updater._quit(app)
updater._run_isolated(convert,"acceptance-convert")
assert hashlib.sha256(source.read_bytes()).hexdigest()==sample["sha256"]
sample["normalized_path"]=str(target)
sample["normalized_sha256"]=hashlib.sha256(target.read_bytes()).hexdigest()
Path(sys.argv[1]).write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps(sample,ensure_ascii=False))
