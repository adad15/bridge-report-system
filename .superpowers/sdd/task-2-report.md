# Module 05 Task 2 Report

## Status

Implemented deterministic BridgeAnnualInspectionData 1.0 read compatibility.

## Changes

- Added the pure `normalize_review_contract` adapter and exact compatibility labels:
  - `native_1_1`
  - `upgraded_1_0`
  - `legacy_read_only`
- A 1.0 response is normalized only in memory: the clone gets version `1.1`, missing defect `group_review_status` becomes `待确认`, and missing `confirmed_missing_photo_numbers` becomes an empty array.
- `待校对` 1.0 records are labeled `upgraded_1_0`; other 1.0 records are labeled `legacy_read_only`.
- The review GET route uses the normalized clone for statistics and response assembly and never persists it.
- Added `contract_compatibility` to the C++ response builder and frontend `ReviewResponse` type.
- Registered the new C++ source and test in CMake.

## TDD Evidence

- RED: Debug build failed because `ContractCompatibility.hpp` was absent and `build_review_response` still accepted four arguments.
- GREEN: Debug build succeeded; focused compatibility tests passed 4/4.
- Focused C++ regression passed 10/10, including compatibility, response assembly, and effective inspection-year tests.
- Frontend API test passed 10/10.
- Frontend full test suite passed 117/117 across 10 files.
- Frontend production build passed.

## Concerns

- Full C++ regression currently reports 37 failures out of 145 tests. The failures are in the unchanged Task1 contract-validation path: `AnnualInspectionContract.cpp` still requires `contract.version == "1.0"`, while the HEAD fixtures and frontend contract are 1.1. Task 2 files and focused tests pass; this existing cross-module baseline mismatch was not modified because it is outside the Task 2 brief's allowed files.
- Twenty database-dependent C++ tests remain skipped because no database test environment is configured.

## Review Fix

Date: 2026-07-11

### Resolved Findings

- Terminal records are read-only on first load. `deriveReviewSession` derives the state from `import_status` and `contract_compatibility`, and `ReviewWorkspacePage` feeds that value to every existing action and edit-disable path.
- Confirmed and cancelled records use distinct read-only banner text. A `legacy_read_only` response is read-only regardless of import status.
- Frontend API coverage now includes normalized `upgraded_1_0` and `legacy_read_only` responses.
- The C++ contract validator now requires exact version `1.1`; no Task 4 enum validation was added.
- Cancelled 1.0 records are covered as `legacy_read_only`.

### RED Evidence

- `npm run test -- --run src/review/reviewSession.test.ts src/api/reviewApi.test.ts`
  - Exit 1: `reviewSession.test.ts` could not load the missing `./reviewSession` module.
  - `reviewApi.test.ts` passed 12/12, including the two new compatibility response cases.
- `cmake --build --preset vs2022-x64-debug`
  - Exit 0: the newly added C++ tests compiled.
- `ctest --preset vs2022-x64-debug -R "RejectsLegacy10Version|MarksCancelled10" --output-on-failure`
  - Exit 1: `RejectsLegacy10Version` failed because the validator returned `ok() == true` for version 1.0.
  - `MarksCancelled10AsLegacyReadOnly` passed; the Minor finding required coverage only.

### GREEN And Regression Evidence

- `npm run test -- --run src/review/reviewSession.test.ts src/api/reviewApi.test.ts`
  - Exit 0: 2 files passed, 16/16 tests passed.
- `cmake --build --preset vs2022-x64-debug`
  - Exit 0: backend library, executable, and test executable built successfully.
- `ctest --preset vs2022-x64-debug -R "AnnualInspectionContract|ContractCompatibility" --output-on-failure`
  - Exit 0: 14/14 tests passed.
- `ctest --preset vs2022-x64-debug --output-on-failure`
  - Exit 0: 147 tests discovered, 0 failed, 20 database-gated tests skipped, 127 passed.
- `npm run test -- --run`
  - Exit 0: 11 files passed, 123/123 tests passed.
- `npm run build`
  - Exit 0: TypeScript and Vite production build passed; 59 modules transformed.

### Files

- `backend-cpp/src/contracts/AnnualInspectionContract.cpp`
- `backend-cpp/tests/test_annual_inspection_contract.cpp`
- `backend-cpp/tests/test_contract_compatibility.cpp`
- `frontend/src/api/reviewApi.test.ts`
- `frontend/src/pages/ReviewWorkspacePage.tsx`
- `frontend/src/review/reviewSession.ts`
- `frontend/src/review/reviewSession.test.ts`
- `.superpowers/sdd/task-2-report.md`

### Second Fix Commit

The exact hash is appended after commit creation because a Git commit cannot contain its own stable object hash.
