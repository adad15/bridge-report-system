# Contracts

This directory contains shared JSON contract artifacts used by the C++ backend,
Python tools, and React frontend.

## BridgeAnnualInspectionData

- Schema: `contracts/bridge_annual_inspection_data.schema.json`
- Source model: `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- Valid sample: `samples/contracts/bridge_annual_inspection_data.valid.json`

`BridgeAnnualInspectionData` is candidate data stored in
`import_records.parsed_result_json`. It is not the formal source of truth.
After review and confirmation, the C++ backend writes the confirmed data to
PostgreSQL.

## Regenerate

Run the schema exporter from the repository root:

```powershell
cd tools-python
uv run python -m bridge_report_tools.contracts.export_schema
```
