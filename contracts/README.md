# Contracts

This directory contains shared JSON contract artifacts used by the C++ backend,
Python tools, and React frontend.

## BridgeAnnualInspectionData

- Current contract version: `1.2`
- Schema: `contracts/bridge_annual_inspection_data.schema.json`
- Source model: `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- Valid sample: `samples/contracts/bridge_annual_inspection_data.valid.json`

Version `1.1` requires every defect candidate to include
`group_review_status` (`待确认` or `已确认`) and the unique string array
`confirmed_missing_photo_numbers`.

Version `1.2` additionally introduces the regulatory defect fields
`defect_scale` (nullable positive integer) and `defect_deduction`
(nullable 0-100 number) on every defect candidate — both fully separate
from the review-hint `severity` — and the required array
`ratings.component_ratings`. Each component rating carries
`source_score` / `calculated_score` / `confirmed_score`,
`score_validation_status` (`一致`, `不一致`, `无法复算`,
`人工接受Word值`, `人工采用复算值`), `score_resolution_reason`,
`deduction_defect_candidate_ids`, and `calculation_details`
(JTG/T H21-2011 4.1.1 ordered deductions). When the status is `不一致`
or `无法复算`, `confirmed_score` and `score_resolution_reason` must be
null until a reviewer makes an explicit choice with a reason.

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
