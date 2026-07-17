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

For module 06 (contract 1.2 component ratings and defect-thread archive), run the
combined check instead. It applies migrations 002 and 003 in order — both are
idempotent and safe to re-run — and executes both rollback-only smoke tests:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-module06-db.ps1
```

Migration `003_component_rating_validation_and_thread_binding.sql` adds the
component-score validation columns (`source_score`, `calculated_score`,
`score_validation_status`, `score_resolution_reason`, `calculation_details_json`)
to `condition_ratings`, enforces one component-level rating per inspection
version per component, and clears legacy `severity` values that were previously
mis-written into `defect_observations.scale`. Confirmed 1.1-era rows stay
readable with the new columns as `NULL`; nothing is backfilled.

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

Page entry (React Router path, reached from the selected annual workspace):

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
GET  /api/bridges/{bridge_id}/overview                           # bridge archive overview
GET  /api/inspection-years/{inspection_year_id}/workspace        # annual workspace summary
POST /api/bridges/{bridge_id}/inspection-years                   # create a current annual inspection
POST /api/inspection-years/{inspection_year_id}/import-records/word # archive one Word source
```

The five action buttons on the review page map onto these endpoints:

```text
保存草稿           -> PUT  .../review-draft
批量确认普通候选   -> reducer batch_confirm_normal, then PUT .../review-draft with the updated draft
入库前检查         -> POST .../preflight-confirm (unlocks 确认年度事实入库 when can_confirm=true)
确认年度事实入库   -> POST .../confirm (opens a revision-confirmation dialog first when the latest
                      preflight reports requires_revision_confirmation=true)
取消导入           -> POST .../cancel, then navigate back to the selected annual workspace
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
   page navigated back to the selected `/bridges/:bridgeId/inspections/:inspectionYearId` workspace.
8. Re-ran the seed script to restore the sample bridge to a single clean pending
   import record for the next developer.

## Module 06.5 Bridge-centered Workspace

Module 06.5 reorganizes the existing review and archive capabilities around a
bridge archive. After login, `/` redirects to `/bridges`; selecting a bridge
opens its overview rather than a database-oriented detail page.

Frontend routes:

```text
/bridges                                            # searchable bridge archive list
/bridges/:bridgeId                                  # latest conclusion, pending work, history, defect summary
/bridges/:bridgeId/inspections                      # redirects to the latest current annual inspection
/bridges/:bridgeId/inspections/:inspectionYearId    # year rail + annual workspace
/bridges/:bridgeId/components                       # read-first component defect archive
/bridges/:bridgeId/defect-threads/review            # nested thread cleanup action, not a top-level tab
/bridges/:bridgeId/inspections/:inspectionYearId/imports/:importRecordId/review
                                                    # existing full-width review workbench
```

An annual workspace can create a non-conflicting year, upload one `.docx`
source (`软件导出Word` or `正式Word`), call the existing Python parse endpoint,
and then enter the full-width review workbench. The selected bridge and year
come from PostgreSQL context, not from the Word file. Failed parsing keeps the
archived Word and exposes a retry action without another upload. Word uploads
default to a 256 MiB limit (`archive.word_upload_max_bytes`) and responses never
expose archive paths.

Administrators can also use `更多 → 删除年度` in the annual workspace. This is
an irreversible C1 deletion: selecting any revision permanently removes every
version for the same bridge and year. The warning dialog first loads a live
impact preview, shows affected facts/files, requires a reason and the exact
confirmation text, and remains disabled while an import record has an active
edit lock. Shared archive files are retained; exclusive files are removed after
the database transaction through a retryable cleanup queue, while the deletion
audit is kept permanently.

```text
GET    /api/inspection-years/{inspection_year_id}/deletion-impact  # admin-only live preview
DELETE /api/inspection-years/{inspection_year_id}                  # admin-only C1 permanent deletion
```

The bridge archive list also provides administrator-only bridge maintenance.
Administrators can create a bridge with its name and optional route/location
identity, or select up to 100 bridges for a live whole-archive deletion preview.
Batch deletion commits each bridge independently, blocks only bridges with an
active edit lock or changed impact token, and permanently retains a compact
audit snapshot. Exclusive files are processed immediately and by a durable
cleanup coordinator at startup and every five minutes; shared files remain.

```text
POST   /api/bridges                    # admin-only bridge creation
POST   /api/bridges/deletion-impact    # admin-only batch impact preview
DELETE /api/bridges                    # admin-only per-bridge atomic batch deletion
```

Each import card in the annual workspace now also has an administrator-only
`删除导入记录` action. It permanently deletes only an unconfirmed import in
`已上传`, `解析中`, `解析失败`, `待校对`, or `已取消`; it never cascades into formal
annual facts. The dialog previews candidate counts and file impact, requires a
reason plus the exact `永久删除 DRJL-xxxxxx` text, and is blocked by an active
edit lock, a formal-fact reference, a read-only status, or a stale impact token.
The deletion audit keeps actor/reason/impact snapshots. Exclusive archived
photos, the temporary Word, and a registered parse work directory enter a
durable cleanup queue; shared files remain. A late Python result observes
`import_record_deleted` and cannot recreate the record.

```text
GET    /api/import-records/{import_record_id}/deletion-impact  # admin-only live preview
DELETE /api/import-records/{import_record_id}                  # admin-only permanent deletion
```

Review candidates display an import-local disease sequence number. Items in
`需要处理` use business labels and navigate to the exact disease, linked or
unlinked photo, field, or rating with a temporary highlight. A disease row with
no photo number is normal and produces no warning; a referenced photo number
that cannot be matched remains a missing-photo warning.

## Module 06 Component Defect Archive

Module 06 organizes confirmed annual facts into a read-only component defect
archive with human-curated defect threads. The main pages are read-first: the
only writes are creating a defect thread and binding/rebinding an observation
to one — annual defect facts themselves are never modified here, and no
progress/repair conclusions are produced (those belong to module 07).

Contract 1.2 (see `contracts/README.md`) is the prerequisite: defect candidates
carry `defect_scale` / `defect_deduction`, and `ratings.component_ratings[]`
carries the Word source score, the JTG/T H21-2011 4.1.1 recalculated score, and
the reviewer-confirmed final score. The shared scoring fixture lives in
`samples/scoring/component_score_cases.json` and is consumed by the Python,
C++, and TypeScript implementations of the same pure function.

Frontend routes:

```text
/bridges/:bridgeId/components                      # A1 layout: component list + archive detail
/bridges/:bridgeId/components/:componentId         # same page with a component selected
/bridges/:bridgeId/defect-threads/review           # unbound observations, thread suggestions, bind/create
```

C++ API endpoints:

```text
GET /api/bridges/{bridge_id}/components                          # components with current-valid formal defects
GET /api/bridge-components/{component_id}/defect-archive         # thread-first archive (threads > yearly observations)
GET /api/bridge-components/{component_id}/defect-archive/revisions # superseded revisions, read-only
GET /api/bridges/{bridge_id}/unbound-defect-observations         # thread review page data source
GET /api/defect-observations/{observation_id}/thread-suggestions # same-component suggestions (never auto-bind)
GET /api/defect-observations/{observation_id}/evidence           # raw row, table, import/file numbers
GET /api/defect-photos/{defect_photo_id}/content                 # controlled formal photo content
POST /api/defect-threads                                          # create thread + bind first observation
PUT  /api/defect-observations/{observation_id}/defect-thread      # bind / rebind / unbind (confirm_rebind + token)
```

Binding requests carry the observation's `updated_at` text as an optimistic
concurrency token; changing an existing binding requires `confirm_rebind=true`,
and observations referenced by a manually confirmed comparison are rejected
until module 07 revokes the conclusion. Default queries only read current-valid
inspection versions (`is_current` and `已确认`); superseded revisions are shown
through the separate revisions entry and never inherit thread bindings.

Legacy data policy: pending 1.0/1.1 drafts open read-only as
`legacy_pending_reparse` and must be re-parsed to 1.2 via
`POST /api/import-records/{id}/parse-word`; confirmed 1.1-era rating rows stay
readable and the archive marks them as lacking score-validation details instead
of guessing.
