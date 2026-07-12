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
a list of temporary image file names.

Current rule profile support:

- `辽宁国省干线`

The parse request must include:

```json
{
  "rule_profile": "辽宁国省干线"
}
```

The rule profile is selected by the user workflow and passed by C++; Python
does not auto-detect report templates. Under the `辽宁国省干线` profile,
extraction is limited to:

- defect tables `表2.1-1`、`表2.2-1`、`表2.3-1`
- defect photos `照片2.1-x`、`照片2.2-x`、`照片2.3-x` matched by photo number
- rating tables `表4.1-1`、`表4.1-2` (no fallback to `附录1` or body text)

It does not parse formal report body text, generate comparison candidates, write
PostgreSQL, or decide same-year revision behavior.

## Module 05 Review Workspace

Module 05 adds the human review workbench that turns a module 03
`BridgeAnnualInspectionData` candidate JSON (already saved by the C++ backend into
`import_records.parsed_result_json`) into confirmed annual facts in PostgreSQL. The
frontend never talks to the Python tool service directly; it only calls the C++
main backend.

Page entry (React Router path, reached by clicking an import record row on the
bridge detail page):

```text
/bridges/:bridgeId/inspections/:inspectionYearId/imports/:importRecordId/review
```

C++ API endpoints used by the review workspace:

```text
GET  /api/import-records/{import_record_id}/review              # load candidate JSON + statistics
POST /api/import-records/{import_record_id}/parse-word          # call Python, archive photos, persist draft
GET  /api/import-records/{import_record_id}/photos/{candidate_id}/content # controlled archived photo content
PUT  /api/import-records/{import_record_id}/review-draft         # save edited draft (stays 待校对)
POST /api/import-records/{import_record_id}/preflight-confirm    # blocking-error/warning check before import
POST /api/import-records/{import_record_id}/confirm              # write defect/measurement/photo/rating facts
POST /api/import-records/{import_record_id}/cancel               # cancel a pending import record
GET  /api/bridges                                                # bridge list (navigation)
GET  /api/bridges/{bridge_id}/inspection-years                   # inspection years for a bridge
GET  /api/bridges/{bridge_id}/import-records                     # import records for a bridge
```

The five action buttons on the review page map onto these endpoints:

```text
保存草稿           -> PUT  .../review-draft
批量确认普通候选   -> reducer batch_confirm_normal, then PUT .../review-draft with the updated draft
入库前检查         -> POST .../preflight-confirm (unlocks 确认年度事实入库 when can_confirm=true)
确认年度事实入库   -> POST .../confirm (opens a revision-confirmation dialog first when the latest
                      preflight reports requires_revision_confirmation=true)
取消导入           -> POST .../cancel, then navigate back to the bridge detail page
```

Seed sample data (idempotent; deletes and reinserts the sample bridge/year/import
record by bridge name):

```powershell
$env:PSQL_EXE = "D:\PostgreSQL\18\bin\psql.exe"   # only if psql is not on PATH
powershell -ExecutionPolicy Bypass -File scripts/dev/seed-module05-review-sample.ps1
```

The seed also creates a deterministic PNG, its `archived_files` row, and the
`import_record_files` attachment link. Override the default archive root with
`BRIDGE_REPORT_ARCHIVE_ROOT` when the C++ service uses a different local path.

Start the complete local stack in this order, from the repository root, using a
separate PowerShell window for each long-running service:

```powershell
# 1. PostgreSQL must already be running and migrations applied.
powershell -ExecutionPolicy Bypass -File scripts/dev/start-python-tools.ps1
powershell -ExecutionPolicy Bypass -File scripts/dev/start-cpp-backend.ps1
powershell -ExecutionPolicy Bypass -File scripts/dev/start-frontend.ps1
powershell -ExecutionPolicy Bypass -File scripts/dev/check-health.ps1
```

Default URLs are Python `127.0.0.1:18081`, C++ `127.0.0.1:18080`, and frontend
`127.0.0.1:5173`.

### Manual end-to-end verification performed

The Liaoning trunk-road real Word fixture was verified through the production
C++ -> Python -> archive -> PostgreSQL path. Baseline results: 25 defects, 31
photo candidates, 36 temporary Word images, 31 archived photos, and 15 rating
items. The first controlled photo-content request returned HTTP 200. The fixture
under `test-inputs/` and runtime archive files are intentionally not committed.

Run the environment-gated real Word parser regression with:

```powershell
$env:BRIDGE_REPORT_REAL_WORD_PATH = "D:\path\to\liaoning-report.docx"
Set-Location tools-python
uv run pytest -q tests/importers/test_real_word_regression.py
```

Without the variable, the test is reported as skipped and does not require the
private report fixture.

With PostgreSQL, the C++ backend (`127.0.0.1:18080`), and the Vite dev server
(`127.0.0.1:5173`) running:

1. Seeded the sample bridge/year/import record with the script above, then opened
   the review page for that import record.
2. Edited a defect's `defect_location` field (its `review_status` auto-flipped to
   `已修改`) and clicked 保存草稿; confirmed via `psql` that
   `import_records.parsed_result_json` reflected the new text and status.
3. Clicked 批量确认普通候选; confirmed via `psql` that the still-`待确认` photo and
   rating candidates flipped to `已确认` while the manually edited (`已修改`) defect
   was left untouched (batch confirm only targets `待确认` candidates by design).
4. Clicked 入库前检查; the result panel reported `can_confirm=true` with no blocking
   errors, and the 确认年度事实入库 button unlocked.
5. Clicked 确认年度事实入库; the page switched to read-only and showed the written
   counts. Confirmed via `psql` that `defect_observations`, `defect_measurements`,
   `defect_photos`, and `condition_ratings` were populated and that
   `inspection_years.status = '已确认'` with `version_number = 1`.
6. Seeded a second import record for the same bridge and year (its own placeholder
   `inspection_years` row, `is_current=false`). After 批量确认普通候选 and 入库前检查,
   the panel reported `requires_revision_confirmation=true`. Clicking 确认年度事实入库
   opened the revision dialog instead of confirming directly; submitting with the
   checkbox unchecked was blocked client-side, and a direct `POST .../confirm` with
   `confirm_revision:false` was independently rejected by the backend with
   `409 revision_confirmation_required`. Checking 作为修订版确认, filling in a note,
   and submitting succeeded: `psql` showed the original `inspection_years` row
   transitioned to `已被修订` (`is_current=false`) and a new row was created with
   `version_number = 2`, `is_current=true`, `revision_source_inspection_id` pointing
   at the old row, and the placeholder row removed.
7. Seeded a third throwaway import record and verified 取消导入: after confirming the
   browser prompt, `psql` showed `import_records.import_status = '已取消'` and the
   page navigated back to `/bridges/:bridgeId`.
8. Re-ran the seed script to restore the sample bridge to a single clean pending
   import record for the next developer.
