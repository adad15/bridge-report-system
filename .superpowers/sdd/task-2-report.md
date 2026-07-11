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
