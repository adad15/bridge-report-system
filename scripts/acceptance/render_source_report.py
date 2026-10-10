"""Export and inspect a downloaded report in the isolated source acceptance run.

Run export with the project's tools Python (pywin32), inspect with a Python
that has pypdfium2 and Pillow. Neither mode touches application databases.
"""
from __future__ import annotations
import argparse
import json
import re
import sys
from pathlib import Path


def read(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def report_paths(manifest_path: Path):
    manifest = read(manifest_path)
    if manifest["database"] != "bridge_report_acceptance" or manifest["ports"]["postgres"] != 25432:
        raise SystemExit("not the isolated source acceptance database")
    log = Path(manifest["log_directory"]).resolve()
    state = read(Path(manifest["run_directory"]) / "scenario.json")
    entry = state["sourceReport"]
    docx = Path(entry["download"]).resolve()
    if docx.parent != log or docx.suffix.lower() != ".docx":
        raise SystemExit("download is outside the acceptance log directory")
    suffix = "-fixed" if entry.get("previousJob") else ""
    return log, docx, docx.with_suffix(".pdf"), log / f"source-report-render{suffix}.json"


def export(docx: Path, pdf: Path):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools-python"))
    from bridge_report_tools.reports.field_updater import microsoft_word_updater

    updater = microsoft_word_updater(timeout_seconds=240)

    def run(invocation):
        app = updater._new_app(invocation)
        document = app.Documents.Open(
            FileName=str(docx), ReadOnly=True, AddToRecentFiles=False, Visible=False)
        try:
            pages = int(document.ComputeStatistics(2))
            document.ExportAsFixedFormat(str(pdf), 17)
        finally:
            document.Close(0)
            updater._quit(app)
        return pages

    pages = updater._run_isolated(run, "source-report-pdf-export")
    print(json.dumps({"pdf": str(pdf), "pages": pages, "bytes": pdf.stat().st_size}, ensure_ascii=False))


def inspect(log: Path, pdf: Path, output: Path):
    import pypdfium2 as pdfium

    document = pdfium.PdfDocument(pdf)
    lengths = []
    selected = {}
    chosen = {0, 1, *range(max(0, len(document) - 4), len(document))}
    for index in range(len(document)):
        page = document[index]
        extracted = page.get_textpage().get_text_range()
        lengths.append(len(extracted.strip()))
        if index in chosen:
            selected[str(index + 1)] = {
                "start": extracted[:280], "end": extracted[-360:],
                "footer": re.findall(r"第\s*\d+\s*页\s*共\s*\d+\s*页", extracted),
            }
            page.render(scale=1.5).to_pil().convert("RGB").save(
                log / f"source-report-fixed-page-{index + 1:03}.png")
    report = {
        "pages": len(document), "min_text_chars": min(lengths),
        "max_text_chars": max(lengths),
        "pages_under_20_chars": [index + 1 for index, size in enumerate(lengths) if size < 20],
        "selected": selected,
    }
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("export", "inspect"))
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    log, docx, pdf, output = report_paths(args.manifest)
    if args.mode == "export":
        export(docx, pdf)
    else:
        inspect(log, pdf, output)


if __name__ == "__main__":
    main()
