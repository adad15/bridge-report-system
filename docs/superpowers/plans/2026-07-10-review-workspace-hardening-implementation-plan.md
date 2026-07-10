# Module 05 Defect Photo Review Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把模块 05 加固为可查看真实归档照片、逐张校对照片、按病害组确认，并且只用事务内最新草稿安全写入正式事实表的完整闭环。

**Architecture:** Python 继续只解析 `.docx` 并输出候选和临时图片；C++ 主服务负责契约兼容、图片归档、数据库关联、图片内容接口和唯一正式入库事务；React 前端以“病害字段表格 + A3 照片区”为一个病害组完成校对。`BridgeAnnualInspectionData` 升级为 1.1，病害组状态和人工确认缺图成为前后端共同约束。

**Tech Stack:** Python 3.12、FastAPI、Pydantic 2、python-docx、C++20、Drogon、JsonCpp、PostgreSQL、OpenSSL SHA-256、React 18、TypeScript 5.5、Vite 5、Vitest 2、Testing Library。

## Global Constraints

- 第一版输入仍只支持 `.docx`，不增加 `.doc`。
- Python 工具层不写 PostgreSQL，也不决定正式归档路径。
- C++ 主服务是 `archived_files`、`import_record_files` 和正式事实表的唯一写入口。
- 病害页面必须保留 `结构部位、构件、位置、病害类型、数量、尺寸原文、照片编号、校对状态、备注`。
- 照片逐张处理；病害字段、实际照片和缺图编号全部处理后，才能确认病害组。
- “确认缺图”只能用于不存在照片候选的引用编号，不能绕过损坏或丢失的归档文件。
- 只有 `import_status = 待校对` 可编辑；`已确认` 和 `已取消` 始终只读。
- 正式 `defect_photos` 必须拥有非空 `archived_file_id`。
- 最终确认必须在事务锁内重新读取最新 JSON、重新校验、重新预检和构造写计划。
- 只有 Drogon 事务提交回调报告成功后，HTTP 才能返回确认成功。
- 不实现图片裁剪、旋转、重新上传、拖动排序、跨年度病害对比或报告生成。

---

## File Map

### Shared contract

- `tools-python/bridge_report_tools/contracts/annual_inspection.py`：1.1 Pydantic 真源。
- `tools-python/bridge_report_tools/importers/defect_tables.py`：解析器创建 1.1 病害候选。
- `contracts/bridge_annual_inspection_data.schema.json`：由 Pydantic 导出的共享 Schema。
- `samples/contracts/*.json`：1.1 合法、非法和历史对比样例。
- `frontend/src/contracts/annualInspection.ts`：TypeScript 类型和运行时守卫。
- `backend-cpp/src/contracts/AnnualInspectionContract.cpp`：C++ 严格入库边界。

### C++ services

- Create `backend-cpp/include/bridge_report/review/ContractCompatibility.hpp`
- Create `backend-cpp/src/review/ContractCompatibility.cpp`
- Create `backend-cpp/include/bridge_report/archive/ExtractedPhotoArchive.hpp`
- Create `backend-cpp/src/archive/ExtractedPhotoArchive.cpp`
- Create `backend-cpp/include/bridge_report/db/WordImportRepository.hpp`
- Create `backend-cpp/src/db/WordImportRepository.cpp`
- Create `backend-cpp/include/bridge_report/http/WordImportRoutes.hpp`
- Create `backend-cpp/src/http/WordImportRoutes.cpp`
- Create `backend-cpp/include/bridge_report/db/CommitLatch.hpp`
- Create `backend-cpp/src/db/CommitLatch.cpp`

### Frontend review domain

- Create `frontend/src/review/defectPhotoGroups.ts`
- Create `frontend/src/review/components/DefectPhotoGroup.tsx`
- Create `frontend/src/review/reviewSession.ts`
- Modify `frontend/src/review/reviewDraft.ts`
- Modify `frontend/src/review/components/DefectsSection.tsx`
- Delete `frontend/src/review/components/PhotosSection.tsx`
- Modify `frontend/src/pages/ReviewWorkspacePage.tsx`

---

### Task 1: Upgrade the shared candidate contract to 1.1

**Files:**
- Modify: `tools-python/bridge_report_tools/contracts/annual_inspection.py`
- Modify: `tools-python/bridge_report_tools/importers/defect_tables.py`
- Modify: `tools-python/tests/test_annual_inspection_contract.py`
- Modify: `tools-python/tests/importers/test_word_importer.py`
- Regenerate: `contracts/bridge_annual_inspection_data.schema.json`
- Modify: `samples/contracts/bridge_annual_inspection_data.valid.json`
- Modify: `samples/contracts/bridge_annual_inspection_data.invalid-evaluation-part-grade.json`
- Modify: `samples/contracts/bridge_annual_inspection_data.with-comparison.json`
- Modify: `frontend/src/contracts/annualInspection.ts`
- Modify: `frontend/src/contracts/annualInspection.test.ts`
- Modify: `contracts/README.md`
- Modify: `docs/superpowers/specs/modules/03-bridge-annual-inspection-data-contract.md`

**Interfaces:**
- Produces: `DefectGroupReviewStatus = "待确认" | "已确认"`.
- Produces: required `DefectCandidate.group_review_status` and `DefectCandidate.confirmed_missing_photo_numbers`.
- Produces: exact `contract.version = "1.1"` for all new parser output.

- [ ] **Step 1: Write failing Python and TypeScript contract tests**

Add assertions that 1.0 is rejected, 1.1 requires both fields, and duplicate or wrong-typed values fail:

```python
def test_defect_group_review_fields_are_required() -> None:
    payload = valid_payload()
    payload["contract"]["version"] = "1.1"
    payload["defects"][0].pop("group_review_status", None)
    with pytest.raises(ValidationError):
        BridgeAnnualInspectionData.model_validate(payload)

def test_new_parser_payload_starts_group_pending() -> None:
    result = parse_word_import(make_request())
    assert all(item.group_review_status == "待确认" for item in result.data.defects)
    assert all(item.confirmed_missing_photo_numbers == [] for item in result.data.defects)
```

```ts
expect(isBridgeAnnualInspectionData({ ...validData, contract: { ...validData.contract, version: "1.0" } })).toBe(false);
expect(isBridgeAnnualInspectionData(validData)).toBe(true);
expect(isBridgeAnnualInspectionData({
  ...validData,
  defects: [{ ...validData.defects[0], group_review_status: "非法状态" }],
})).toBe(false);
```

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```powershell
Set-Location tools-python
uv run pytest tests/test_annual_inspection_contract.py tests/importers/test_word_importer.py -q
Set-Location ..\frontend
npm run test -- --run src/contracts/annualInspection.test.ts
```

Expected: Python fails because the model is still 1.0 and lacks the new fields; TypeScript fails because `ContractInfo.version` is still 1.0.

- [ ] **Step 3: Implement the 1.1 model and update every tracked sample**

Use required fields, not Pydantic defaults, so missing data fails validation:

```python
DefectGroupReviewStatus = Literal["待确认", "已确认"]

class ContractInfo(ContractModel):
    name: Literal["BridgeAnnualInspectionData"]
    version: Literal["1.1"]
    generated_at: datetime
    producer: str
    parser_name: str
    parser_version: str

class DefectCandidate(ContractModel):
    # existing fields stay unchanged
    group_review_status: DefectGroupReviewStatus
    confirmed_missing_photo_numbers: list[str] = Field(json_schema_extra={"uniqueItems": True})

    @field_validator("confirmed_missing_photo_numbers")
    @classmethod
    def require_unique_missing_numbers(cls, value: list[str]) -> list[str]:
        if len(value) != len(set(value)):
            raise ValueError("confirmed_missing_photo_numbers must be unique")
        return value
```

Create parsed defects with:

```python
group_review_status="待确认",
confirmed_missing_photo_numbers=[],
```

Mirror the exact fields in TypeScript:

```ts
export type DefectGroupReviewStatus = "待确认" | "已确认";

export interface ContractInfo {
  name: "BridgeAnnualInspectionData";
  version: "1.1";
  // existing fields
}

export interface DefectCandidate {
  // existing fields
  group_review_status: DefectGroupReviewStatus;
  confirmed_missing_photo_numbers: string[];
}
```

The runtime guard must verify the version, enum, string array, and uniqueness:

```ts
const missing = getRequiredArray(value, "confirmed_missing_photo_numbers");
return value.group_review_status === "待确认" || value.group_review_status === "已确认"
  ? missing !== null && missing.every((item) => typeof item === "string") && new Set(missing).size === missing.length
  : false;
```

- [ ] **Step 4: Regenerate Schema and run contract suites GREEN**

Run:

```powershell
Set-Location tools-python
uv run python -m bridge_report_tools.contracts.export_schema
uv run pytest -q
Set-Location ..\frontend
npm run test -- --run src/contracts/annualInspection.test.ts
npm run build
```

Expected: all Python tests pass, the generated Schema requires the two new fields, TypeScript tests and build pass.

- [ ] **Step 5: Commit the contract upgrade**

```powershell
git add tools-python contracts samples frontend/src/contracts docs/superpowers/specs/modules/03-bridge-annual-inspection-data-contract.md
git commit -m "feat(contract): upgrade annual inspection data to 1.1"
```

---

### Task 2: Add deterministic 1.0 read compatibility

**Files:**
- Create: `backend-cpp/include/bridge_report/review/ContractCompatibility.hpp`
- Create: `backend-cpp/src/review/ContractCompatibility.cpp`
- Create: `backend-cpp/tests/test_contract_compatibility.cpp`
- Modify: `backend-cpp/src/http/ReviewRoutes.cpp`
- Modify: `backend-cpp/include/bridge_report/review/ReviewModels.hpp`
- Modify: `backend-cpp/src/review/ReviewModels.cpp`
- Modify: `backend-cpp/tests/test_review_models.cpp`
- Modify: `backend-cpp/CMakeLists.txt`
- Modify: `frontend/src/api/reviewApi.ts`
- Modify: `frontend/src/api/reviewApi.test.ts`

**Interfaces:**
- Produces: `normalize_review_contract(Json::Value, std::string_view import_status) -> ContractCompatibilityResult`.
- Produces: response field `contract_compatibility: "native_1_1" | "upgraded_1_0" | "legacy_read_only"`.
- Consumes: 1.1 contract from Task 1.

- [ ] **Step 1: Write compatibility tests**

```cpp
TEST(ContractCompatibilityTest, UpgradesPending10WithoutPersistingConfirmation) {
    auto data = load_sample();
    data["contract"]["version"] = "1.0";
    for (auto& defect : data["defects"]) {
        defect.removeMember("group_review_status");
        defect.removeMember("confirmed_missing_photo_numbers");
    }

    const auto result = normalize_review_contract(data, "待校对");
    EXPECT_EQ(result.compatibility, ContractCompatibility::Upgraded10);
    EXPECT_EQ(result.data["contract"]["version"].asString(), "1.1");
    EXPECT_EQ(result.data["defects"][0]["group_review_status"].asString(), "待确认");
    EXPECT_TRUE(result.data["defects"][0]["confirmed_missing_photo_numbers"].empty());
}

TEST(ContractCompatibilityTest, MarksConfirmed10AsLegacyReadOnly) {
    const auto result = normalize_review_contract(make_10_data(), "已确认");
    EXPECT_EQ(result.compatibility, ContractCompatibility::LegacyReadOnly);
}
```

- [ ] **Step 2: Run focused C++ tests and verify RED**

```powershell
Set-Location backend-cpp
cmake --preset vs2022-x64-debug
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug -R "ContractCompatibility|ReviewModels"
```

Expected: compile fails because `ContractCompatibility.hpp` and the new response field do not exist.

- [ ] **Step 3: Implement the pure adapter and GET response field**

Define:

```cpp
enum class ContractCompatibility { Native11, Upgraded10, LegacyReadOnly };

struct ContractCompatibilityResult {
    Json::Value data;
    ContractCompatibility compatibility{ContractCompatibility::Native11};
};

ContractCompatibilityResult normalize_review_contract(
    Json::Value data,
    std::string_view import_status
);

std::string_view contract_compatibility_name(ContractCompatibility value);
```

For any 1.0 response clone, set version to 1.1 and add pending/empty fields. Use `upgraded_1_0` only for `待校对`; use `legacy_read_only` for terminal records. `ReviewRoutes.cpp` passes the normalized clone to statistics and `build_review_response`; it never writes this clone to PostgreSQL.

Change the response builder signature to accept the exact compatibility label:

```cpp
Json::Value build_review_response(
    const ImportRecordDetail& detail,
    const Json::Value& parsed_result,
    const ReviewStatistics& statistics,
    bool has_current_annual_facts,
    std::string_view contract_compatibility
);
```

- [ ] **Step 4: Verify C++ and frontend API tests GREEN**

```powershell
Set-Location backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug -R "ContractCompatibility|ReviewModels"
Set-Location ..\frontend
npm run test -- --run src/api/reviewApi.test.ts
npm run build
```

Expected: response carries a valid compatibility string and `fetchReview` accepts normalized 1.1 data.

- [ ] **Step 5: Commit compatibility support**

```powershell
git add backend-cpp frontend/src/api
git commit -m "feat(review): adapt legacy 1.0 drafts for display"
```

---

### Task 3: Implement defect-group and per-photo state transitions

**Files:**
- Create: `frontend/src/review/defectPhotoGroups.ts`
- Create: `frontend/src/review/defectPhotoGroups.test.ts`
- Modify: `frontend/src/review/reviewDraft.ts`
- Modify: `frontend/src/review/reviewDraft.test.ts`
- Modify: `frontend/src/review/grouping.ts`
- Modify: `frontend/src/review/grouping.test.ts`
- Modify: `frontend/src/review/components/ReviewActionBar.tsx`

**Interfaces:**
- Produces: `buildDefectPhotoGroup(data, candidateId) -> DefectPhotoGroup | null`.
- Produces: `canConfirmDefectPhotoGroup(data, candidateId) -> { ok: boolean; reasons: string[] }`.
- Produces reducer actions for confirming/unconfirming missing photos, relinking, resolving, resetting, and confirming a group.

- [ ] **Step 1: Write reducer and selector tests for every state transition**

```ts
it("confirms one matched photo and invalidates its group", () => {
  const next = reviewDraftReducer(makeData(), { type: "photo_confirm_match", candidateId: "photo_0001" });
  expect(next.photos[0]).toMatchObject({ match_status: "已确认", review_status: "已确认" });
  expect(next.defects[0].group_review_status).toBe("待确认");
});

it("relinks a photo and invalidates both groups", () => {
  const next = reviewDraftReducer(makeTwoGroupData(), {
    type: "photo_relink",
    candidateId: "photo_0001",
    defectCandidateId: "defect_0002",
  });
  expect(next.photos[0]).toMatchObject({
    linked_defect_candidate_id: "defect_0002",
    match_status: "已确认",
    review_status: "已修改",
  });
  expect(next.defects.map((item) => item.group_review_status)).toEqual(["待确认", "待确认"]);
});

it("does not confirm a group until missing references are acknowledged", () => {
  const result = canConfirmDefectPhotoGroup(makeMissingPhotoData(), "defect_0001");
  expect(result.ok).toBe(false);
  expect(result.reasons).toContain("missing_photo_confirmation_required");
});
```

- [ ] **Step 2: Run the focused reducer tests and verify RED**

```powershell
Set-Location frontend
npm run test -- --run src/review/defectPhotoGroups.test.ts src/review/reviewDraft.test.ts src/review/grouping.test.ts
```

Expected: missing module/action failures.

- [ ] **Step 3: Implement immutable group selectors and actions**

Use these action shapes:

```ts
| { type: "photo_confirm_match"; candidateId: string }
| { type: "photo_relink"; candidateId: string; defectCandidateId: string }
| { type: "photo_mark_unrelated"; candidateId: string; note: string }
| { type: "photo_ignore"; candidateId: string }
| { type: "photo_reset"; candidateId: string }
| { type: "confirm_missing_photo"; defectCandidateId: string; photoNumber: string }
| { type: "unconfirm_missing_photo"; defectCandidateId: string; photoNumber: string }
| { type: "confirm_defect_group"; defectCandidateId: string }
| { type: "batch_confirm_normal_ratings" };
```

`confirm_defect_group` calls `canConfirmDefectPhotoGroup`; invalid attempts are no-ops. A successful action sets `group_review_status = 已确认` and only changes disease `review_status` from `待确认` to `已确认`; it preserves `已修改`.

When `photo_numbers` changes, intersect `confirmed_missing_photo_numbers` with the new array and reset the group. Remove disease/photo batch confirmation from the old `batch_confirm_normal`; the action bar label becomes `批量确认普通评分`.

The remaining photo transitions are exact: `photo_mark_unrelated` and `photo_ignore` both clear `linked_defect_candidate_id`; unrelated sets `match_status = 未关联, review_status = 已确认`, ignored sets `review_status = 已忽略`; `photo_reset` sets `match_status = 待校对, review_status = 待确认`. Every transition invalidates the disease group that owned the photo before the action, and relink also invalidates the new owner.

- [ ] **Step 4: Run reducer suite and frontend build GREEN**

```powershell
npm run test -- --run src/review/defectPhotoGroups.test.ts src/review/reviewDraft.test.ts src/review/grouping.test.ts
npm run build
```

Expected: all state transitions pass and TypeScript exhaustiveness checks compile.

- [ ] **Step 5: Commit the review-domain state machine**

```powershell
git add frontend/src/review
git commit -m "feat(frontend): add defect photo group state machine"
```

---

### Task 4: Make C++ contract validation, preflight, and confirm plans strict

**Files:**
- Modify: `backend-cpp/src/contracts/AnnualInspectionContract.cpp`
- Modify: `backend-cpp/tests/test_annual_inspection_contract.cpp`
- Modify: `backend-cpp/src/review/PreflightReport.cpp`
- Modify: `backend-cpp/tests/test_preflight_report.cpp`
- Modify: `backend-cpp/include/bridge_report/review/ConfirmPlan.hpp`
- Modify: `backend-cpp/src/review/ConfirmPlan.cpp`
- Modify: `backend-cpp/tests/test_confirm_plan.cpp`
- Modify: `backend-cpp/tests/support/review_fixtures.hpp`

**Interfaces:**
- Consumes: contract 1.1 fields from Task 1.
- Produces: `PhotoPlan.archive_relative_path`.
- Produces blocking codes `group_confirmation_required`, `missing_photo_confirmation_required`, `photo_archive_missing`, and `contract_validation_failed`.

- [ ] **Step 1: Add failing validation, preflight, and plan tests**

```cpp
TEST(AnnualInspectionContractTest, RejectsInvalidReviewEnumsAndDuplicateIds) {
    auto data = load_valid_sample();
    data["defects"][0]["review_status"] = "随便通过";
    data["defects"].append(data["defects"][0]);
    const auto result = validate_bridge_annual_inspection_data(data);
    EXPECT_FALSE(result.ok());
}

TEST(PreflightReportTest, MissingPhotoNeedsExplicitAcknowledgement) {
    auto data = confirmed_data();
    data["defects"][0]["photo_numbers"].append("2.1-99");
    data["defects"][0]["group_review_status"] = "已确认";
    const auto report = build_preflight_report(data, base_context());
    EXPECT_TRUE(has_issue(report.blocking_errors, "missing_photo_confirmation_required"));
}

TEST(ConfirmPlanTest, CarriesArchivedPhotoPath) {
    const auto plan = build_confirm_plan(confirmed_data());
    ASSERT_EQ(plan.photos.size(), 1u);
    EXPECT_EQ(plan.photos[0].archive_relative_path, "photos/2.1-1.jpg");
}
```

- [ ] **Step 2: Run focused C++ tests and verify RED**

```powershell
Set-Location backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug -R "AnnualInspectionContract|PreflightReport|ConfirmPlan"
```

Expected: enum/group/path assertions fail.

- [ ] **Step 3: Implement strict validation and group-aware preflight**

The validator must check exact 1.1 version, required strings/arrays, unique IDs inside each collection, valid enums, linked disease existence, safe relative archive paths, and missing-number constraints. Every confirmed missing number must belong to the disease's `photo_numbers` and must not currently have a photo candidate with the same number linked to that disease.

Preflight rules:

```cpp
if (group_review_status != "已确认") {
    add_issue(blocking, "group_confirmation_required", message, defect_id);
}
if (!missing_numbers_are_acknowledged(defect, data)) {
    add_issue(blocking, "missing_photo_confirmation_required", message, defect_id);
}
if (resolved_photo_requires_file(photo) && archive_relative_path_of(photo).empty()) {
    add_issue(blocking, "photo_archive_missing", message, photo_id);
}
```

Extend `PhotoPlan`:

```cpp
struct PhotoPlan {
    std::string candidate_id;
    std::string defect_candidate_id;
    std::string photo_number;
    std::optional<std::string> photo_title;
    std::string archive_relative_path;
};
```

Only include diseases with `group_review_status = 已确认` and disease status `已确认/已修改`; only include linked photos with resolved review/match status and non-empty archive path.

- [ ] **Step 4: Run the full C++ unit suite GREEN**

```powershell
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug
```

Expected: all non-database tests pass; PostgreSQL tests skip when their environment gate is absent.

- [ ] **Step 5: Commit strict backend review rules**

```powershell
git add backend-cpp
git commit -m "feat(backend): enforce defect photo group confirmation"
```

---

### Task 5: Archive extracted photos safely on the filesystem

**Files:**
- Create: `backend-cpp/include/bridge_report/archive/ExtractedPhotoArchive.hpp`
- Create: `backend-cpp/src/archive/ExtractedPhotoArchive.cpp`
- Create: `backend-cpp/tests/test_extracted_photo_archive.cpp`
- Modify: `backend-cpp/include/bridge_report/archive/ArchivePaths.hpp`
- Modify: `backend-cpp/src/archive/ArchivePaths.cpp`
- Modify: `backend-cpp/tests/test_archive_paths.cpp`
- Modify: `backend-cpp/CMakeLists.txt`
- Modify: `backend-cpp/vcpkg.json`

**Interfaces:**
- Produces: `archive_extracted_photos(const Json::Value&, const PhotoArchiveContext&) -> ArchivedPhotoBatch`.
- Produces: `cleanup_archived_photo_batch(const std::filesystem::path&, const ArchivedPhotoBatch&)`.
- Produces: safe root resolution shared by archive writes and photo reads.

- [ ] **Step 1: Write filesystem tests using a temporary directory**

```cpp
TEST(ExtractedPhotoArchiveTest, CopiesCandidatePhotoAndRewritesJson) {
    TempDirectory temp;
    write_bytes(temp.staging() / "photo_0001.jpeg", jpeg_fixture());
    auto response = python_response_with_photo("photo_0001", "photo_0001.jpeg");

    const auto batch = archive_extracted_photos(response, make_context(temp));

    ASSERT_EQ(batch.files.size(), 1u);
    EXPECT_TRUE(std::filesystem::exists(temp.archive() / batch.files[0].storage_relative_path));
    EXPECT_FALSE(batch.files[0].sha256.empty());
    EXPECT_EQ(batch.data["photos"][0]["extracted_file"]["archive_relative_path"].asString(),
              batch.files[0].storage_relative_path.generic_string());
}

TEST(ExtractedPhotoArchiveTest, RejectsPathTraversalAndMissingFiles) {
    EXPECT_THROW(archive_one("../outside.jpg"), PhotoArchiveError);
    EXPECT_THROW(archive_one("missing.jpg"), PhotoArchiveError);
}
```

- [ ] **Step 2: Build and verify RED**

```powershell
Set-Location backend-cpp
cmake --preset vs2022-x64-debug
cmake --build --preset vs2022-x64-debug
```

Expected: compile fails because `ExtractedPhotoArchive` is absent.

- [ ] **Step 3: Implement path checks, SHA-256, copy, rewrite, and cleanup**

Define:

```cpp
struct PhotoArchiveContext {
    std::filesystem::path staging_root;
    std::filesystem::path archive_root;
    std::string bridge_system_number;
    std::string bridge_name;
    int inspection_year{0};
    std::string import_record_system_number;
    std::string import_name;
};

struct ArchivedPhotoFile {
    std::string candidate_id;
    std::string original_file_name;
    std::string current_file_name;
    std::filesystem::path storage_relative_path;
    std::string file_extension;
    std::uintmax_t file_size_bytes{0};
    std::string sha256;
};

struct ArchivedPhotoBatch {
    Json::Value data;
    std::vector<ArchivedPhotoFile> files;
};
```

Normalize the staging path with `weakly_canonical` and require it to remain under the staging root. `detect_image_format` checks JPEG/PNG/GIF/BMP/WebP/TIFF magic bytes and requires them to agree with `.jpg/.jpeg/.png/.gif/.bmp/.webp/.tif/.tiff`; renamed text or binary files are rejected. Compute SHA-256 with OpenSSL EVP and name the target from `candidate_id + first 12 hash characters + extension`. On any exception remove files copied by the current call.

- [ ] **Step 4: Run archive tests GREEN**

```powershell
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug -R "ArchivePaths|ExtractedPhotoArchive"
```

Expected: valid photos are copied and rewritten; traversal, absolute path, missing file and unsupported extension cases pass by rejecting input.

- [ ] **Step 5: Commit filesystem archiving**

```powershell
git add backend-cpp
git commit -m "feat(backend): archive extracted defect photos"
```

---

### Task 6: Add the C++ Word parse orchestration endpoint

**Files:**
- Create: `backend-cpp/include/bridge_report/db/WordImportRepository.hpp`
- Create: `backend-cpp/src/db/WordImportRepository.cpp`
- Create: `backend-cpp/include/bridge_report/http/WordImportRoutes.hpp`
- Create: `backend-cpp/src/http/WordImportRoutes.cpp`
- Create: `backend-cpp/include/bridge_report/db/CommitLatch.hpp`
- Create: `backend-cpp/src/db/CommitLatch.cpp`
- Create: `backend-cpp/tests/test_commit_latch.cpp`
- Create: `backend-cpp/tests/test_word_import_repository.cpp`
- Create: `backend-cpp/tests/test_word_import_routes.cpp`
- Modify: `backend-cpp/src/main.cpp`
- Modify: `backend-cpp/CMakeLists.txt`

**Interfaces:**
- Produces: `POST /api/import-records/{import_record_id}/parse-word`.
- Produces: `WordImportRepository::load_context`, `persist_parse_result`, and `mark_parse_failed`.
- Produces: `CommitLatch::callback()` and `CommitLatch::wait()` so filesystem cleanup follows a known database commit result.
- Consumes: Python `POST /imports/word/parse` and `ArchivedPhotoBatch` from Task 5.

- [ ] **Step 1: Write repository and request-builder tests**

```cpp
TEST(WordImportRoutesTest, BuildsPythonRequestFromTrustedDatabaseContext) {
    const auto request = build_python_word_request(context(), body(), "D:/staging/photos");
    EXPECT_EQ(request["selected_bridge_system_number"].asString(), "QL-000001");
    EXPECT_EQ(request["import_record_system_number"].asString(), "DRJL-000001");
    EXPECT_EQ(request["report_number"].asString(), "TEST-001");
    EXPECT_EQ(request["temporary_photo_output_dir"].asString(), "D:/staging/photos");
}

TEST_F(WordImportRepositoryTest, PersistsPhotosAndJsonInOneTransaction) {
    const auto outcome = repository.persist_parse_result(import_id, batch());
    ASSERT_TRUE(outcome.success);
    EXPECT_EQ(count_rows("archived_files", import_id), 1);
    EXPECT_EQ(count_rows("import_record_files", import_id), 1);
    EXPECT_EQ(load_status(import_id), "待校对");
}

TEST(CommitLatchTest, ReturnsFalseForFailedCommitCallback) {
    CommitLatch latch;
    latch.callback()(false);
    EXPECT_FALSE(latch.wait());
}
```

- [ ] **Step 2: Run tests and verify RED**

```powershell
Set-Location backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug -R "WordImportRepository|WordImportRoutes"
```

Expected: compile fails because repository/routes do not exist.

- [ ] **Step 3: Implement repository transaction and orchestration route**

The public request body contains only user/business choices:

```json
{
  "rule_profile": "辽宁国省干线",
  "import_mode": "已有桥年度导入",
  "file_role": "当前年度检测资料",
  "data_role": "当前年度",
  "inspection_date": "2026-07-07",
  "report_number": "TEST-001",
  "project_name": "2026年度定期检测"
}
```

`load_context` obtains the bridge, year, import system number, source type, main `archived_files` row and safe Word path. The route creates a staging directory under the configured archive work area, sends an async Drogon request to Python, validates the response, archives candidate photos, and calls `persist_parse_result`.

`persist_parse_result` must use one transaction:

```text
lock import_records row
verify status is 已上传 / 解析失败 / 待校对
remove old photo attachment links for a reparse
insert one archived_files row per ArchivedPhotoFile
insert import_record_files(file_role='附件', process_status='处理成功')
update parsed_result_json, importer_name/version, status='待校对'
commit
```

`persist_parse_result` creates its Drogon transaction with a `CommitLatch` callback and reports success only after `wait()` returns true. `PersistParseOutcome` returns obsolete archive rows and paths removed by the transaction. After commit success, the route deletes those old physical files; if persistence fails, it calls `cleanup_archived_photo_batch` for the new files. Always delete the staging directory. Return `temporary_photo_file_count`, `photo_candidate_count`, and `archived_photo_count` on success.

- [ ] **Step 4: Run unit and PostgreSQL integration tests GREEN**

```powershell
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug -R "WordImportRepository|WordImportRoutes"
$env:BRIDGE_REPORT_TEST_DATABASE_URL="postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
ctest --preset vs2022-x64-debug -R "WordImportRepository"
```

Expected: pure request tests pass; DB test passes when the environment variable is set and skips otherwise.

- [ ] **Step 5: Commit C++ parse orchestration**

```powershell
git add backend-cpp
git commit -m "feat(backend): orchestrate Word parsing and photo persistence"
```

---

### Task 7: Serve archived photo content through a controlled API

**Files:**
- Modify: `backend-cpp/include/bridge_report/db/ReviewRepository.hpp`
- Modify: `backend-cpp/src/db/ReviewRepository.cpp`
- Modify: `backend-cpp/include/bridge_report/http/ReviewRoutes.hpp`
- Modify: `backend-cpp/src/http/ReviewRoutes.cpp`
- Modify: `backend-cpp/src/main.cpp`
- Modify: `backend-cpp/tests/test_review_repository.cpp`
- Create: `backend-cpp/tests/test_photo_content.cpp`
- Modify: `backend-cpp/CMakeLists.txt`
- Modify: `frontend/src/api/reviewApi.ts`
- Modify: `frontend/src/api/reviewApi.test.ts`

**Interfaces:**
- Produces: `GET /api/import-records/{import_record_id}/photos/{photo_candidate_id}/content`.
- Produces: `ReviewRepository::get_photo_content_ref(import_id, candidate_id)`.
- Produces frontend `photoContentUrl(baseUrl, importId, candidateId): string`.

- [ ] **Step 1: Write repository/path/API helper tests**

```cpp
TEST_F(ReviewRepositoryTest, ResolvesPhotoOnlyThroughCurrentImportFileLinks) {
    const auto ref = repository.get_photo_content_ref(import_record_id_, "photo_0001");
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->storage_relative_path, "bridges/sample/photos/photo_0001.jpg");
    EXPECT_EQ(ref->content_type, "image/jpeg");
}

TEST(PhotoContentTest, RejectsArchivePathOutsideRoot) {
    EXPECT_FALSE(resolve_photo_content_path("D:/archive", "../secret.jpg").has_value());
}
```

```ts
expect(photoContentUrl("http://127.0.0.1:18080", "a/b", "p 1"))
  .toBe("http://127.0.0.1:18080/api/import-records/a%2Fb/photos/p%201/content");
```

- [ ] **Step 2: Run tests and verify RED**

```powershell
Set-Location backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug -R "ReviewRepository|PhotoContent"
Set-Location ..\frontend
npm run test -- --run src/api/reviewApi.test.ts
```

Expected: missing repository method, route, and frontend helper failures.

- [ ] **Step 3: Implement DB resolution and file response**

Resolve by joining the photo candidate path to this import's `import_record_files` and `archived_files`; do not accept a path from the URL. The route resolves the stored relative path under `archive_root`, checks the file exists, and returns `newFileResponse` with mapped `Content-Type` plus local CORS headers.

Return:

```text
404 photo_candidate_not_found      candidate absent
409 photo_archive_missing          path absent, DB link absent, or file absent
400 unsafe_archive_path            stored path escapes archive root
```

- [ ] **Step 4: Run backend and frontend API suites GREEN**

```powershell
Set-Location backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug -R "ReviewRepository|PhotoContent"
Set-Location ..\frontend
npm run test -- --run src/api/reviewApi.test.ts
npm run build
```

- [ ] **Step 5: Commit the photo content endpoint**

```powershell
git add backend-cpp frontend/src/api
git commit -m "feat(review): serve archived defect photos"
```

---

### Task 8: Rebuild final confirmation around the locked latest draft

**Files:**
- Modify: `backend-cpp/tests/test_commit_latch.cpp`
- Modify: `backend-cpp/include/bridge_report/db/ReviewRepository.hpp`
- Modify: `backend-cpp/src/db/ReviewRepository.cpp`
- Modify: `backend-cpp/src/http/ImportConfirmRoutes.cpp`
- Modify: `backend-cpp/tests/test_review_repository.cpp`
- Modify: `backend-cpp/CMakeLists.txt`

**Interfaces:**
- Changes: `confirm_annual_facts(import_record_id, confirm_revision, confirmation_note)` no longer accepts an external plan/year.
- Produces: `ConfirmOutcome.preflight_details` for a transaction-time preflight rejection.
- Consumes: `CommitLatch::callback()` and `CommitLatch::wait()` from Task 6.

- [ ] **Step 1: Write stale-draft, archive-ID, and commit-result tests**

```cpp
TEST(CommitLatchTest, ReportsCommitFailure) {
    CommitLatch latch;
    latch.callback()(false);
    EXPECT_FALSE(latch.wait());
}

TEST_F(ConfirmAnnualFactsTest, ReadsLatestJsonInsteadOfCallerSnapshot) {
    auto latest = build_confirmed_data();
    latest["defects"][0]["component_name"] = "事务内最新构件";
    ASSERT_TRUE(repository.save_review_draft(import_record_id_, write_json(latest)));

    const auto outcome = repository.confirm_annual_facts(import_record_id_, false, "确认最新草稿");
    ASSERT_TRUE(outcome.success);
    EXPECT_EQ(load_inserted_component_type(), "事务内最新构件");
}

TEST_F(ConfirmAnnualFactsTest, WritesNonNullArchivedFileId) {
    const auto outcome = repository.confirm_annual_facts(import_record_id_, false, "照片确认");
    ASSERT_TRUE(outcome.success);
    EXPECT_FALSE(load_defect_photo_archived_file_id().empty());
}
```

- [ ] **Step 2: Run tests and verify RED**

```powershell
Set-Location backend-cpp
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug -R "CommitLatch|ConfirmAnnualFacts"
```

Expected: signature mismatch, stale external plan behavior, null archive ID, and missing commit latch failures.

- [ ] **Step 3: Move validation, preflight, plan construction, and archive resolution inside the transaction**

The method sequence must be exactly:

```cpp
auto latch = std::make_shared<CommitLatch>();
tx = db_client_->newTransaction(latch->callback());

// SELECT import_status, bridge_id, inspection_year_id, parsed_result_json FOR UPDATE
// parse and validate exact 1.1
// derive effective year from locked row/json
// query current year under the same transaction
// build preflight and return its JSON if blocked
// build ConfirmPlan from the same Json::Value
// resolve each PhotoPlan.archive_relative_path through import_record_files
// write facts and update import status

tx.reset();                    // triggers COMMIT
if (!latch->wait()) {
    return failure("database_commit_failed", "数据库提交失败。");
}
return committed_outcome;
```

`insert_defect_photo` must accept and bind a real UUID:

```sql
insert into defect_photos
(defect_observation_id, archived_file_id, source_import_record_id, photo_number, photo_title, match_status)
values ($1::uuid, $2::uuid, $3::uuid, $4, $5, '已确认')
```

On preflight failure, roll back and return `error_code = preflight_failed` with the full report in `preflight_details`. Remove the old comment that accepted an unconfirmed commit.

- [ ] **Step 4: Run full C++ suite including PostgreSQL GREEN**

```powershell
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug
$env:BRIDGE_REPORT_TEST_DATABASE_URL="postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
ctest --preset vs2022-x64-debug -R "ReviewRepository|ConfirmAnnualFacts"
```

Expected: the latest saved value reaches formal facts, photo IDs are non-null, rollback tests remain clean, and success is produced after commit callback success.

- [ ] **Step 5: Commit transactional confirmation**

```powershell
git add backend-cpp
git commit -m "fix(backend): confirm from locked latest review draft"
```

---

### Task 9: Build the combined disease and A3 photo review UI

**Files:**
- Modify: `frontend/package.json`
- Modify: `frontend/package-lock.json`
- Create: `frontend/src/test/setup.ts`
- Create: `frontend/src/review/components/DefectPhotoGroup.tsx`
- Create: `frontend/src/review/components/DefectPhotoGroup.test.tsx`
- Modify: `frontend/src/review/components/DefectsSection.tsx`
- Modify: `frontend/src/review/components/NeedsAttentionSection.tsx`
- Modify: `frontend/src/review/components/ReviewSidebar.tsx`
- Delete: `frontend/src/review/components/PhotosSection.tsx`
- Modify: `frontend/src/pages/ReviewWorkspacePage.tsx`
- Modify: `frontend/src/styles.css`
- Modify: `frontend/vite.config.ts`

**Interfaces:**
- Consumes: group selectors/actions from Task 3.
- Consumes: `photoContentUrl` from Task 7.
- Produces: one `病害与照片` navigation section with preserved disease table fields and expandable A3 photo review.

- [ ] **Step 1: Install test DOM dependencies and write failing component tests**

```powershell
Set-Location frontend
npm install --save-dev @testing-library/dom@10.4.0 @testing-library/jest-dom@6.6.3 @testing-library/react@16.1.0 @testing-library/user-event@14.6.1 jsdom@25.0.1
```

Configure Vitest with `environment: "jsdom"` and `setupFiles: ["./src/test/setup.ts"]`; the setup file contains `import "@testing-library/jest-dom/vitest";`. Then test:

```tsx
it("keeps all disease fields and shows one active large photo", async () => {
  render(<DefectPhotoGroup {...propsWithTwoPhotos()} />);
  expect(screen.getByLabelText("结构部位")).toBeInTheDocument();
  expect(screen.getByLabelText("构件")).toBeInTheDocument();
  expect(screen.getByLabelText("位置")).toBeInTheDocument();
  expect(screen.getByLabelText("病害类型")).toBeInTheDocument();
  expect(screen.getByLabelText("数量")).toBeInTheDocument();
  expect(screen.getByLabelText("尺寸原文")).toBeInTheDocument();
  expect(screen.getByLabelText("照片编号")).toBeInTheDocument();
  expect(screen.getByLabelText("校对状态")).toBeInTheDocument();
  expect(screen.getByLabelText("备注")).toBeInTheDocument();
  expect(screen.getAllByRole("img", { name: /照片/ }).filter((img) => img.classList.contains("active"))).toHaveLength(1);
});

it("renders blocking errors separately from warnings", () => {
  render(<NeedsAttentionSection items={mixedAttentionItems()} {...attentionProps()} />);
  expect(screen.getByRole("heading", { name: "错误" })).toBeInTheDocument();
  expect(screen.getByRole("heading", { name: "警告" })).toBeInTheDocument();
});
```

- [ ] **Step 2: Run component test and verify RED**

```powershell
npm run test -- --run src/review/components/DefectPhotoGroup.test.tsx
```

Expected: component and test setup are missing.

- [ ] **Step 3: Implement the combined section and A3 layout**

`ReviewSidebar.GroupKey` becomes:

```ts
export type GroupKey = "needs_attention" | "defect_photos" | "ratings" | "source_evidence" | "raw_json";
```

Render the preserved nine-column disease row first. Beneath the selected/expanded row render:

```tsx
<img
  className="defect-photo-stage-image"
  src={photoContentUrl(baseUrl, importRecordId, activePhoto.candidate_id)}
  alt={`照片 ${activePhoto.photo_number}`}
/>
```

Add fixed-aspect stage, title/status line, icon/text actions, thumbnail buttons, missing-photo acknowledgement list, and “确认该病害及照片”. Use `object-fit: contain`; use horizontal overflow for the disease table and thumbnail strip. Stop button/input click propagation so editing does not unintentionally collapse the row.

- [ ] **Step 4: Run component, reducer, build, and viewport verification GREEN**

```powershell
npm run test -- --run
npm run build
```

Then start the frontend and inspect desktop 1440x900 and mobile 390x844 with Playwright/browser screenshots. Expected: no overlap, disease columns remain reachable by horizontal scrolling, one large photo is visible, and thumbnail selection does not resize the layout.

- [ ] **Step 5: Commit the combined review UI**

```powershell
git add frontend
git commit -m "feat(frontend): merge defect and photo review"
```

---

### Task 10: Enforce read-only records and race-safe draft saving

**Files:**
- Create: `frontend/src/review/reviewSession.ts`
- Create: `frontend/src/review/reviewSession.test.ts`
- Modify: `frontend/src/pages/ReviewWorkspacePage.tsx`
- Create: `frontend/src/pages/ReviewWorkspacePage.test.tsx`
- Modify: `frontend/src/pages/BridgeDetailPage.tsx`
- Modify: `frontend/src/review/components/DefectsSection.tsx`
- Modify: `frontend/src/review/components/DefectPhotoGroup.tsx`
- Modify: `frontend/src/review/components/RatingsSection.tsx`
- Modify: `frontend/src/review/components/ReviewActionBar.tsx`

**Interfaces:**
- Produces: `isImportRecordEditable(status): boolean`.
- Produces: `shouldClearDirtyAfterSave(saveRevision, currentRevision): boolean`.
- Changes all editable components to accept `disabled: boolean`.

- [ ] **Step 1: Write pure and page-level failing tests**

```ts
expect(isImportRecordEditable("待校对")).toBe(true);
expect(isImportRecordEditable("已确认")).toBe(false);
expect(isImportRecordEditable("已取消")).toBe(false);
expect(shouldClearDirtyAfterSave(11, 11)).toBe(true);
expect(shouldClearDirtyAfterSave(11, 12)).toBe(false);
```

```tsx
it("renders a confirmed import as fully read-only", async () => {
  mockFetchReview({ import_status: "已确认" });
  render(<ReviewWorkspacePage />);
  expect(await screen.findByText("该导入记录为只读结果")).toBeInTheDocument();
  expect(screen.getAllByRole("textbox").every((item) => item.hasAttribute("disabled"))).toBe(true);
  expect(screen.queryByRole("button", { name: "保存草稿" })).not.toBeEnabled();
});
```

- [ ] **Step 2: Run tests and verify RED**

```powershell
Set-Location frontend
npm run test -- --run src/review/reviewSession.test.ts src/pages/ReviewWorkspacePage.test.tsx
```

Expected: helpers and derived read-only behavior are absent.

- [ ] **Step 3: Implement revision tracking and real disabled controls**

Use refs so an async save compares against the revision captured at request start:

```ts
const draftRevision = useRef(0);

function dispatch(action: ReviewDraftAction): void {
  draftRevision.current += 1;
  rawDispatch(action);
  setPreflight(null);
  setDirty(true);
}

async function handleSaveDraft(value: BridgeAnnualInspectionData): Promise<boolean> {
  const saveRevision = draftRevision.current;
  setBusy(true);
  try {
    await saveReviewDraft(backendBaseUrl, importRecordId, value);
    if (shouldClearDirtyAfterSave(saveRevision, draftRevision.current)) setDirty(false);
    return true;
  } finally {
    setBusy(false);
  }
}
```

Derive read-only from the server record status, not only local confirm success:

```ts
const readOnly = response.import_record.import_status !== "待校对" || confirmResult !== null;
const controlsDisabled = readOnly || busy;
```

Pass `disabled` through every input, select and action button. Keep the no-op dispatch only as a defensive fallback, not as the primary read-only mechanism. In `BridgeDetailPage`, label pending records `进入校对` and terminal records `查看结果`.

- [ ] **Step 4: Run all frontend tests and build GREEN**

```powershell
npm run test -- --run
npm run build
```

Expected: save race, busy controls, terminal read-only records, labels and existing confirm flow all pass.

- [ ] **Step 5: Commit session safety**

```powershell
git add frontend
git commit -m "fix(frontend): make review sessions race-safe and read-only"
```

---

### Task 11: Update fixtures, documentation, and run the full real-Word regression

**Files:**
- Modify: `database/dev/seed_module05_review_sample.sql`
- Modify: `scripts/dev/seed-module05-review-sample.ps1`
- Modify: `samples/contracts/bridge_annual_inspection_data.valid.json`
- Create: `backend-cpp/tests/test_real_word_review_flow.cpp`
- Modify: `backend-cpp/CMakeLists.txt`
- Modify: `README.md`
- Modify: `PROJECT_CONTEXT.md`
- Modify: `docs/superpowers/specs/modules/05-review-workspace.md`

**Interfaces:**
- Produces: a deterministic local sample with one real archived image association.
- Produces: an environment-gated real Word regression named `RealWordReviewFlowTest`.

- [ ] **Step 1: Add a failing end-to-end regression test**

Gate the test on all three variables:

```cpp
const auto database_url = getenv_string("BRIDGE_REPORT_TEST_DATABASE_URL");
const auto word_path = getenv_string("BRIDGE_REPORT_REAL_WORD_PATH");
const auto python_url = getenv_string("BRIDGE_REPORT_PYTHON_TOOLS_URL");
if (!database_url || !word_path || !python_url) {
    GTEST_SKIP() << "real Word regression environment is not configured";
}
```

The test creates an isolated bridge/year/import row, archives the Word as its main file, calls Python with the same request builder used by the route, then runs the same archive service and repository persistence path before asserting:

```cpp
EXPECT_EQ(parsed["defects"].size(), 25u);
EXPECT_EQ(parsed["photos"].size(), 31u);
EXPECT_EQ(temporary_photo_file_count, 36u);
EXPECT_EQ(count_non_empty_archive_paths(parsed["photos"]), 31u);
EXPECT_EQ(count_import_photo_files(import_id), 31);
```

After programmatically resolving photos, acknowledging true missing references and confirming every disease group, save and confirm, then assert formal disease/photo/rating counts match the final 1.1 JSON.

- [ ] **Step 2: Run the gated test and verify its first failure**

```powershell
Set-Location backend-cpp
$env:BRIDGE_REPORT_TEST_DATABASE_URL="postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
$env:BRIDGE_REPORT_REAL_WORD_PATH="D:\vs2022 code\bridge-report-system\test-inputs\word-import\绕阳河二号桥报告b8e246e8-cd4d-4202-8d4d-49c56dd34389.docx"
$env:BRIDGE_REPORT_PYTHON_TOOLS_URL="http://127.0.0.1:18081"
ctest --preset vs2022-x64-debug -R "RealWordReviewFlow" --output-on-failure
```

Expected before fixture/docs completion: failure at missing seeded archive association or end-to-end expected count; without variables the test reports SKIPPED.

- [ ] **Step 3: Update local seed and operating documentation**

The seed script writes a small deterministic PNG under the configured archive root, inserts its `archived_files` and `import_record_files` rows, and rewrites the sample photo's `archive_relative_path` to the same safe path. It must not read or commit anything from `test-inputs/` or `test-output/`.

Document exact startup order and URLs:

```text
1. PostgreSQL
2. Python tools on 127.0.0.1:18081
3. C++ backend on 127.0.0.1:18080
4. React frontend on 127.0.0.1:5173
5. seed-module05-review-sample.ps1
6. open the printed import record from the bridge detail page
```

Update `PROJECT_CONTEXT.md` only with behavior that is actually implemented and verified. Update the module 05 change log with contract 1.1, joined disease/photo review, archive endpoint, and transaction semantics.

- [ ] **Step 4: Run every automated and manual verification gate**

```powershell
Set-Location tools-python
uv run pytest -q

Set-Location ..\backend-cpp
cmake --preset vs2022-x64-debug
cmake --build --preset vs2022-x64-debug
ctest --preset vs2022-x64-debug

$env:BRIDGE_REPORT_TEST_DATABASE_URL="postgresql://bridge_report:bridge_report_dev@127.0.0.1:5432/bridge_report_system"
ctest --preset vs2022-x64-debug --output-on-failure

Set-Location ..\frontend
npm run test -- --run
npm run build
```

Start all services and verify in the browser:

1. Disease fields from the screenshot remain present and editable for a pending import.
2. A3 stage displays the actual image returned by the C++ content route.
3. Thumbnail switching, per-photo decisions, missing-photo acknowledgement and group confirmation work.
4. Errors and warnings are separated under `需要处理`.
5. Saving, preflight and final confirmation complete; reopening the record is read-only.
6. A confirmed `defect_photos` row has a non-null `archived_file_id`.

- [ ] **Step 5: Commit final fixtures and documentation**

```powershell
git add database/dev scripts/dev backend-cpp/tests backend-cpp/CMakeLists.txt README.md PROJECT_CONTEXT.md docs/superpowers/specs/modules/05-review-workspace.md samples/contracts
git commit -m "test: verify module05 real Word review flow"
```

---

## Final Review Checklist

- [ ] Every parser-created disease has both 1.1 group fields.
- [ ] C++ rejects invalid enums, duplicate IDs, dangling photo links and invalid missing-photo acknowledgements.
- [ ] A 1.0 pending draft opens as upgraded but unconfirmed; terminal 1.0 records remain read-only.
- [ ] Extracted candidate images are archived before the review JSON is saved.
- [ ] No absolute Python staging path reaches persisted JSON or the browser.
- [ ] The browser reads photos only from the C++ content endpoint.
- [ ] Disease fields from the original table are still visible and editable.
- [ ] Photo decisions are per-image and any edit invalidates the relevant disease group.
- [ ] Missing-photo acknowledgement cannot bypass a missing archived file.
- [ ] Final confirmation reads and validates the locked latest JSON.
- [ ] Every formal photo has a non-null archive foreign key.
- [ ] HTTP success is sent only after a successful commit callback.
- [ ] Confirmed and cancelled records are disabled in both UI and backend writes.
- [ ] Python, C++, frontend, PostgreSQL integration and real Word regression all pass.
