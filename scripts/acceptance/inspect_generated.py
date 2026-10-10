"""Read-only validation and PDF export of the downloaded application report."""
import hashlib,json,math,re,sys
from pathlib import Path
from zipfile import ZipFile
from xml.etree import ElementTree as ET
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"tools-python"))
from bridge_report_tools.reports.field_updater import microsoft_word_updater
manifest=json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
log=Path(manifest["log_directory"])
source=log/"small-generated.docx"
before=hashlib.sha256(source.read_bytes()).hexdigest()
with ZipFile(source) as z:
    assert z.testzip() is None
    tree=ET.fromstring(z.read("word/document.xml"))
    ns={"w":"http://schemas.openxmlformats.org/wordprocessingml/2006/main"}
    text="".join(tree.itertext())
    visible="".join(t.text or "" for t in tree.findall(".//w:t",ns))
    assert "验收小样桥" in visible and "2026" in visible
    assert "94.02" in visible and "2类" in visible
    assert "{{" not in visible and "[[BLOCK:" not in visible
    media=[n for n in z.namelist() if n.startswith("word/media/") and not n.endswith("/")]
    assert len(media)==2,media
    tables=len(tree.findall(".//w:tbl",ns))
    fields=[n.text for n in tree.findall(".//w:instrText",ns)]
result=json.loads((log/"small-assessment.json").read_text(encoding="utf-8"))["result"]
# Independent fixed-fixture oracle, no invocation of the application evaluator.
component=100-25-20/(100*math.sqrt(2))*75
category=(100+component)/2-(100-component)/10
upper=category*.7+100*.18+100*.12
overall=upper*.4+100*.4+100*.2
assert math.isclose(result["overall_score"],overall,abs_tol=1e-10)
assert result["final_grade"]==2
actual=result["structure_parts"][0]["categories"][0]
assert math.isclose(actual["score"],category,abs_tol=1e-10)
assert sorted(d["deduction"] for c in actual["components"] for d in c["defects"])==[20,25]
updater=microsoft_word_updater(timeout_seconds=90)
def export(invocation):
    import win32process
    app=updater._new_app(invocation)
    doc=None
    try:
        image=win32process.GetModuleFileNameEx(invocation._process.handle,0)
        doc=app.Documents.Open(FileName=str(source),ReadOnly=True,AddToRecentFiles=False,Visible=False)
        pages=int(doc.ComputeStatistics(2))
        toc=[doc.TablesOfContents(i).Range.Text for i in range(1,doc.TablesOfContents.Count+1)]
        doc.ExportAsFixedFormat(str(log/"small-generated.pdf"),17)
        return {"process_image":image,"version":str(app.Version),"pages":pages,"toc":toc}
    finally:
        if doc is not None: doc.Close(0)
        updater._quit(app)
office=updater._run_isolated(export,"acceptance-export")
assert hashlib.sha256(source.read_bytes()).hexdigest()==before
for sample in manifest["sources"]:
    assert hashlib.sha256(Path(sample["path"]).read_bytes()).hexdigest()==sample["sha256"]
evidence={"sha256":before,"tables":tables,"media":len(media),"fields":fields,"office":office,
          "oracle":{"component":component,"category":category,"superstructure":upper,"overall":overall,"grade":2},"source_hashes_unchanged":True}
(log/"report-inspection.json").write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps(evidence,ensure_ascii=False,indent=2))
