# Bridge Report System

Local web system for bridge inspection report archiving, defect review, historical comparison, and formal Word report generation.

## First Module Scope

This skeleton proves the local service shape:

- C++ Drogon main service on `127.0.0.1:18080`
- Python FastAPI tool service on `127.0.0.1:18081`
- React/Vite frontend on `127.0.0.1:5173`
- PostgreSQL reserved as the future fact database
- `archive/` reserved as the local binary file archive

## Development Order

Read these documents first:

- `PROJECT_CONTEXT.md`
- `docs/superpowers/specs/2026-07-01-bridge-report-system-design.md`
- `docs/superpowers/specs/2026-07-01-modular-technical-doc-review-design.md`
- `docs/superpowers/specs/modules/01-tech-stack-and-project-skeleton.md`

## Module 02 Database Check

Module 02 creates the PostgreSQL core schema and archive metadata foundation.

Set `BRIDGE_REPORT_DATABASE_URL` if your local database differs from the default:

```powershell
$env:BRIDGE_REPORT_DATABASE_URL = "postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
```

If `psql` is not on `PATH`, point `PSQL_EXE` to the local PostgreSQL client:

```powershell
$env:PSQL_EXE = "D:\PostgreSQL\18\bin\psql.exe"
```

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-module02-db.ps1
```

The check applies `database/migrations/002_core_schema_and_archive.sql` and runs the rollback-only smoke test in `database/tests/002_core_schema_smoke.sql`.

## Module 03 Annual Inspection Contract

Module 03 defines the shared `BridgeAnnualInspectionData` candidate JSON used by the Python Word-import tools, C++ backend, and React review workspace.

Artifacts:

- JSON Schema: `contracts/bridge_annual_inspection_data.schema.json`
- Shared samples: `samples/contracts/`
- Python model: `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- C++ validator: `backend-cpp/include/bridge_report/contracts/AnnualInspectionContract.hpp`
- Frontend types and guard: `frontend/src/contracts/annualInspection.ts`

The contract represents candidate data stored in `import_records.parsed_result_json`. Confirmed bridge facts still live in PostgreSQL after user review and C++ backend confirmation.

## Module 04 Word Importer Prototype

Module 04 adds the Python `.docx` importer prototype.

The C++ backend remains responsible for browser upload, file archive records,
import records, database writes, and revision/version decisions. The Python
tool service reads an already archived `.docx` path and writes extracted images
to a temporary directory provided by C++.

Endpoint:

```text
POST http://127.0.0.1:18081/imports/word/parse
```

The endpoint returns a module 03 `BridgeAnnualInspectionData` candidate JSON and
a list of temporary image file names. First-version extraction is limited to:

- second-chapter defect tables
- defect photos matched by photo number
- fourth-chapter condition rating tables

It does not parse formal report body text, generate comparison candidates, write
PostgreSQL, or decide same-year revision behavior.
