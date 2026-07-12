from bridge_report_tools.importers.defect_tables import derive_quantity_text


def test_derives_only_explicit_quantity_expressions() -> None:
    assert derive_quantity_text("1处蜂窝、麻面，S=0.6×0.1m²") == "1处"
    assert derive_quantity_text("多条横向裂缝,L=1.2m") == "多条"
    assert derive_quantity_text("勾缝砂浆脱落,长度：5m") is None
    assert derive_quantity_text(None) is None
