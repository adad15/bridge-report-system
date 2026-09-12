from __future__ import annotations

import zipfile
from pathlib import Path

import pytest

from bridge_report_tools.reports.errors import ReportTemplateError
from bridge_report_tools.reports.package_guard import PackageLimits, inspect_package
from tests.reports.template_fixtures import build_template


RELS_HEADER = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
    '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
)
OFFICE_RELS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
DOCUMENT_RELS_ENTRY = "word/_rels/document.xml.rels"


def _rewrite(
    source: Path,
    target: Path,
    add: dict[str, bytes] | None = None,
    replace: dict[str, bytes] | None = None,
    drop: set[str] | None = None,
) -> Path:
    with zipfile.ZipFile(source) as src, zipfile.ZipFile(
        target, "w", zipfile.ZIP_DEFLATED
    ) as dst:
        for info in src.infolist():
            if info.filename in (drop or set()):
                continue
            payload = (replace or {}).get(info.filename)
            dst.writestr(
                info.filename, payload if payload is not None else src.read(info.filename)
            )
        for name, payload in (add or {}).items():
            dst.writestr(name, payload)
    return target


def _rels(*relationships: str) -> bytes:
    return (RELS_HEADER + "".join(relationships) + "</Relationships>").encode("utf-8")


def _codes(path: Path, limits: PackageLimits | None = None) -> list[str]:
    inspection = inspect_package(path, limits) if limits else inspect_package(path)
    return [issue.code for issue in inspection.issues]


def test_plain_template_package_is_safe(tmp_path: Path) -> None:
    inspection = inspect_package(build_template(tmp_path / "t.docx"))

    assert inspection.is_safe
    assert inspection.issues == []
    assert inspection.entry_count > 0


def test_non_zip_file_is_rejected(tmp_path: Path) -> None:
    path = tmp_path / "fake.docx"
    path.write_bytes(b"not a zip at all")

    with pytest.raises(ReportTemplateError) as excinfo:
        inspect_package(path)

    assert excinfo.value.code == "template_not_docx"


def test_zip_without_document_xml_is_rejected(tmp_path: Path) -> None:
    source = build_template(tmp_path / "t.docx")
    broken = _rewrite(source, tmp_path / "broken.docx", drop={"word/document.xml"})

    with pytest.raises(ReportTemplateError) as excinfo:
        inspect_package(broken)

    assert excinfo.value.code == "template_not_docx"


def test_missing_file_is_rejected(tmp_path: Path) -> None:
    with pytest.raises(ReportTemplateError) as excinfo:
        inspect_package(tmp_path / "nope.docx")

    assert excinfo.value.code == "template_file_missing"


@pytest.mark.parametrize("entry", ["../evil.xml", "/etc/passwd", "word/../../evil.xml"])
def test_path_traversal_entries_are_rejected(tmp_path: Path, entry: str) -> None:
    source = build_template(tmp_path / "t.docx")
    hostile = _rewrite(source, tmp_path / "hostile.docx", add={entry: b"<x/>"})

    assert "template_entry_path_unsafe" in _codes(hostile)


def test_macro_project_is_rejected(tmp_path: Path) -> None:
    source = build_template(tmp_path / "t.docx")
    macro = _rewrite(source, tmp_path / "macro.docx", add={"word/vbaProject.bin": b"\x00\x01"})

    assert "template_macro_rejected" in _codes(macro)


def test_macro_enabled_content_type_is_rejected(tmp_path: Path) -> None:
    source = build_template(tmp_path / "t.docx")
    content_types = zipfile.ZipFile(source).read("[Content_Types].xml").decode("utf-8")
    macro_types = content_types.replace(
        "wordprocessingml.document.main+xml", "ms-word.document.macroEnabled.main+xml"
    )
    macro = _rewrite(
        source,
        tmp_path / "macro.docx",
        replace={"[Content_Types].xml": macro_types.encode("utf-8")},
    )

    assert "template_macro_rejected" in _codes(macro)


def test_embedded_object_is_rejected(tmp_path: Path) -> None:
    source = build_template(tmp_path / "t.docx")
    embedded = _rewrite(
        source, tmp_path / "ole.docx", add={"word/embeddings/oleObject1.bin": b"\x00"}
    )

    assert "template_embedded_object_rejected" in _codes(embedded)


def test_external_image_relationship_is_rejected(tmp_path: Path) -> None:
    source = build_template(tmp_path / "t.docx")
    hostile = _rewrite(
        source,
        tmp_path / "external.docx",
        replace={
            DOCUMENT_RELS_ENTRY: _rels(
                f'<Relationship Id="rId9" Type="{OFFICE_RELS}/image" '
                f'Target="http://example.invalid/logo.png" TargetMode="External"/>'
            )
        },
    )

    assert "template_external_relationship" in _codes(hostile)


def test_external_hyperlink_is_only_a_warning(tmp_path: Path) -> None:
    source = build_template(tmp_path / "t.docx")
    linked = _rewrite(
        source,
        tmp_path / "link.docx",
        replace={
            DOCUMENT_RELS_ENTRY: _rels(
                f'<Relationship Id="rId9" Type="{OFFICE_RELS}/hyperlink" '
                f'Target="https://example.invalid/spec" TargetMode="External"/>'
            )
        },
    )

    inspection = inspect_package(linked)

    assert inspection.is_safe
    assert [issue.code for issue in inspection.issues] == ["template_external_hyperlink"]


def test_attached_template_relationship_is_rejected_even_when_internal(tmp_path: Path) -> None:
    source = build_template(tmp_path / "t.docx")
    hostile = _rewrite(
        source,
        tmp_path / "attached.docx",
        replace={
            DOCUMENT_RELS_ENTRY: _rels(
                f'<Relationship Id="rId9" Type="{OFFICE_RELS}/attachedTemplate" '
                f'Target="styles.dotx"/>'
            )
        },
    )

    assert "template_relationship_rejected" in _codes(hostile)


def test_compression_bomb_entry_is_rejected(tmp_path: Path) -> None:
    source = build_template(tmp_path / "t.docx")
    bomb = _rewrite(source, tmp_path / "bomb.docx", add={"word/pad.xml": b"\x00" * (2 << 20)})

    assert "template_compression_ratio_suspicious" in _codes(bomb)


def test_archive_larger_than_limit_is_rejected(tmp_path: Path) -> None:
    source = build_template(tmp_path / "t.docx")

    with pytest.raises(ReportTemplateError) as excinfo:
        inspect_package(source, PackageLimits(max_archive_bytes=16))

    assert excinfo.value.code == "template_too_large"


def test_entry_count_limit_is_enforced(tmp_path: Path) -> None:
    source = build_template(tmp_path / "t.docx")

    assert "template_entry_count_exceeded" in _codes(
        source, PackageLimits(max_entry_count=1)
    )


def test_uncompressed_size_limit_is_enforced(tmp_path: Path) -> None:
    source = build_template(tmp_path / "t.docx")

    assert "template_uncompressed_size_exceeded" in _codes(
        source, PackageLimits(max_total_uncompressed_bytes=16)
    )
