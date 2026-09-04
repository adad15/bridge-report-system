# Contracts

This directory contains shared JSON contract artifacts used by the Python tools, C++ backend, and React frontend.

## BridgeAnnualInspectionData

- Current and only accepted runtime contract version: `5.0`
- Schema: `contracts/bridge_annual_inspection_data.schema.json`
- Python model: `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- C++ validator: `backend-cpp/src/contracts/AnnualInspectionContract.cpp`
- TypeScript types and guard: `frontend/src/contracts/annualInspection.ts`
- Valid sample: `samples/contracts/bridge_annual_inspection_data.v5.valid.json`
- Comparison sample: `samples/contracts/bridge_annual_inspection_data.v5.with-comparison.json`

Version `5.0` is candidate data stored in `import_records.parsed_result_json`. It contains source facts and general review decisions, including bridge/inspection context, defects, photos, comparison candidates, report-text candidates, warnings, and errors.

It deliberately does not contain:

- imported Word ratings or deductions;
- component database bindings;
- component-inventory revision bindings;
- rating-tree version or node bindings;
- component/rating match methods or evidence.

Component resolution, target components, expanded defect instances, and rating-tree resolution are authoritative PostgreSQL relation-table state. This prevents an old whole-document draft save from overwriting newer binding decisions.

After review and confirmation, the C++ backend combines the 5.0 source facts with authoritative resolution state and writes formal defect, measurement, and photo facts. Technical-condition scores are calculated by the selected versioned standard evaluator and persisted against a successful formal `assessment_run`.

The `v4` samples remain only as migration history. Runtime validators reject 4.0 and earlier versions and reject fields removed in 5.0 instead of silently normalizing them.

## Regenerate the schema

Run from the repository root:

```powershell
Set-Location tools-python
uv run python -m bridge_report_tools.contracts.export_schema
```

After any contract change, update and verify all of the following in the same change:

1. Python model and tests
2. JSON Schema
3. C++ validator and tests
4. TypeScript types/guard and tests
5. `v5` samples
6. this README, the repository README, `PROJECT_CONTEXT.md`, and module 03
