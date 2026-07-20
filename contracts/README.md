# Contracts

This directory contains shared JSON contract artifacts used by the C++ backend,
Python tools, and React frontend.

## BridgeAnnualInspectionData

- Current and only accepted runtime contract version: `2.0`
- Schema: `contracts/bridge_annual_inspection_data.schema.json`
- Source model: `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- Valid sample: `samples/contracts/bridge_annual_inspection_data.v2.valid.json`
- Comparison sample: `samples/contracts/bridge_annual_inspection_data.v2.with-comparison.json`

Version `2.0` carries only import and review candidates: bridge context,
inspection metadata, defects, photos, comparison candidates, and report-text
candidates. Defects contain the imported `defect_scale` and the fields needed
to associate them with an actual component (`component_name`,
`component_number`, and the nullable database association fields). The
contract deliberately has no `ratings`, Word deduction, imported score, or
reviewer score-choice fields. Runtime validators reject every pre-2.0 version
and reject those removed fields instead of silently normalizing them.

`BridgeAnnualInspectionData` is candidate data stored in
`import_records.parsed_result_json`. It is not the formal source of truth.
After review and confirmation, the C++ backend writes the confirmed defect and
photo facts to PostgreSQL. Technical-condition scores are then calculated by
the selected versioned standard evaluator and persisted as projections linked
to a successful formal `assessment_run`; Word scores are not imported.

## Regenerate

Run the schema exporter from the repository root:

```powershell
cd tools-python
uv run python -m bridge_report_tools.contracts.export_schema
```
