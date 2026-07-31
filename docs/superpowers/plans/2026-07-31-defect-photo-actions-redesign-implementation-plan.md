# 病害照片操作模型重构实施计划

> **For agentic workers:** 逐任务实施，每完成一项才进入下一项。严格执行"先写失败测试 → 确认失败原因 → 最小实现 → 测试通过 → 只提交本任务相关文件"。工作区已有用户未提交修改，禁止覆盖、清理或顺带提交。

**Goal:** 实现 [病害照片操作模型重构设计](../specs/2026-07-31-defect-photo-actions-redesign-design.md)：把分散在两个面板的七个照片动作收敛为四个，改为围绕**实际照片**而非 Word 引用清单组织；并新增人工上传补拍照片的能力。

**Architecture:** 卡片派生独立成 `defectPhotoCards.ts` 单一真源，复核模型与照片面板共用同一份结果。四个动作对应四个 reducer action。上传走双写：服务端在一个事务里归档文件、写两张归档表、追加候选到 `parsed_result_json`，并把候选返回给前端追加进本地草稿。

**Tech Stack:** React 18 / TypeScript / Vitest；C++20 / Drogon / JsonCpp / GoogleTest；PostgreSQL。

## 实施边界

分两步，第一步不触碰后端、归档与数据库。

不做：

- 一步换绑；
- 照片编辑（裁剪、旋转、压缩）；
- 批量上传；
- 修改 Word 抽出照片的编号；
- 删除 Word 抽出照片的归档文件；
- 历史导入记录的数据迁移；
- 修改 `BridgeAnnualInspectionData` 契约。

## 不得改变的核心决策

1. 卡片以实际照片为主体，Word 引用但缺图的编号作为占位卡混排（设计 §5.1）。
2. 主动添加即确认；「确认照片」只用于系统自动挂上的照片（设计 §5.3）。
3. 撤销做成同一按钮的反面，不新增按钮位（设计 §5.2）。
4. 删除分两种语义：Word 照片退回未归属，人工上传照片彻底删除（设计 §6）。
5. 未归属面板降为只读清单，归属操作只从病害侧发起（设计 §5.4）。
6. 上传采用双写，不接受"只归档"方案（设计 §7.7）。
7. `warnings_only` 重开态禁止上传与删除（`validate_warnings_only_scope` 禁止增删候选）。
8. 先写文件再开事务；库写失败必须删除已写文件（设计 §7.4）。
9. 契约不改，`source_ref.source_type = "manual"` 已存在。

## 实施前工作区约束

工作区已有大量未提交修改，涉及本计划将要改动的文件，至少包含：

- `frontend/src/pages/ReviewWorkspacePage.tsx`
- `frontend/src/review/components/DefectsSection.tsx`
- `frontend/src/review/components/DefectDetailEditor.tsx`
- `frontend/src/review/components/PhotoRelationEditor.tsx`
- `frontend/src/review/components/UnlinkedPhotosPanel.tsx`
- `frontend/src/review/reviewDraft.ts`
- `frontend/src/review/defectPhotoReviewModel.ts`
- `frontend/src/styles.css`

每个任务只提交该任务涉及的文件。禁止 `git add -A`、`git reset --hard`、`git checkout --` 与 `git clean`。

---

# 第一步：交互模型（纯前端）

## Task 0：记录基线

- [ ] 记录 `git status --short` 与当前分支。
- [ ] 跑一遍前端基线，记录通过数。
- [ ] 若已有失败，只记录与隔离，不顺带修复无关问题。

验证：

```bash
git status --short
npx tsc -b
npx vitest run
npm run build
```

提交：无。

## Task 1：卡片派生模型

**新增：**

- `frontend/src/review/defectPhotoCards.ts`
- `frontend/src/review/defectPhotoCards.test.ts`

步骤：

- [ ] 先写失败测试：同一条病害同时有"已挂的 Word 照片""Word 引用但无图""人工上传的照片"时，
      `buildDefectPhotoCards` 返回三张卡且顺序稳定。
- [ ] 实现派生规则（设计 §5.1）：照片卡来自 `photos` 中 `linked_defect_candidate_id` 命中的每一张；
      占位卡来自 `photo_references` 中找不到同编号已挂照片的每个编号。
- [ ] 卡片类型至少携带：`kind`（`photo` / `missing`）、`photoNumber`、`photo`（照片卡才有）、
      `reference`（有对应 Word 引用时才有）、`source`（`word` / `manual`）、`confirmed`、
      `acknowledgedMissing`。
- [ ] 补测试：`resolution = missing` 的占位卡标记为已处理；`relinked` / `unrelated` 的历史值
      按设计 §9.4 重新呈现为待核对。

完成条件：

- 卡片列表可由 `(draft, defect)` 纯函数推出，不依赖任何组件状态；
- 排序确定，重复调用结果一致。

提交建议：

```text
feat(review): derive defect photo cards from actual photos
```

## Task 2：复核模型改用卡片

**修改：**

- `frontend/src/review/defectPhotoReviewModel.ts`
- `frontend/src/review/defectPhotoReviewModel.test.ts`

步骤：

- [ ] 先写失败测试：待核对占位卡计入问题并阻断批量确认；已确认缺图的占位卡不计入问题。
- [ ] 把 `analyzeDefect` 里遍历 `photo_references` 的整段照片校验改为消费 Task 1 的卡片。
- [ ] 保留现有问题码语义，仅改来源；`photo_reference_pending` 改为由待核对占位卡产生。
- [ ] 在 row 上暴露 `photoCards`，供面板复用，避免两处各算一次。

完成条件：

- 照片相关问题只有卡片这一个来源；
- 现有批量确认资格测试不回归。

提交建议：

```text
refactor(review): drive photo problems from the card model
```

## Task 3：四个 reducer action

**修改：**

- `frontend/src/review/reviewDraft.ts`
- `frontend/src/review/reviewDraft.test.ts`

步骤：

- [ ] 先写失败测试：四个新 action 各自的字段写入（设计 §6），且都不原地修改传入 state。
- [ ] 新增 `link_photo_to_defect`：挂载并按"主动添加即确认"置已确认；编号对得上 Word 引用时
      一并置该引用为 `matched`。
- [ ] 新增 `unlink_photo_from_defect`：解除归属、`match_status = 未关联`、对应引用退回 `pending`
      并清空两个目标字段。
- [ ] 新增 `confirm_photo`（带 `confirmed: boolean`）：撤销时 `match_status` 退回 `待校对`，
      照片**仍挂在本病害上**。
- [ ] 新增 `set_photo_reference_missing`（带 `missing: boolean`）。
- [ ] 删除 `relink_photo_reference`、`confirm_photo_reference_match`、
      `confirm_unrelated_photo_reference`、`confirm_missing_photo`、`reset_photo_reference_review`、
      `photo_relink`、`photo_reset`。
- [ ] 删除五个零调用 action：`photo_confirm_match`、`photo_mark_unrelated`、`photo_ignore`、
      `edit_photo_number`、`unconfirm_missing_photo`。
- [ ] 把测旧 action 的用例改接新 action，覆盖不能丢。

完成条件：

- reducer 里与照片相关的 action 恰好四个；
- 穷尽性检查（`_exhaustive`）仍然编译通过。

提交建议：

```text
refactor(review): collapse photo actions into four primitives
```

## Task 4：照片区组件重写

**修改：**

- `frontend/src/review/components/PhotoRelationEditor.tsx`（更名为 `DefectPhotoPanel.tsx`）
- `frontend/src/review/components/DefectDetailEditor.tsx`
- `frontend/src/styles.css`

**新增：**

- `frontend/src/review/components/DefectPhotoPanel.test.tsx`

步骤：

- [ ] 先写失败测试：三类卡片都渲染；照片卡按状态显示「确认照片」或「撤销确认」；占位卡显示
      「确认缺图」或「撤销缺图」；Word 照片的删除二次确认写"退回未归属"，人工照片写"永久删除"。
- [ ] 按卡片列表渲染，标题改为「Word 引用的照片」。
- [ ] 「添加照片」做成面板级按钮，弹出未归属照片选择器（本步只有这一个来源）。
- [ ] 删除按钮按 `source_ref.source_type` 分支：`word` 走 `unlink_photo_from_defect`；
      `manual` 在本步暂不出现（第二步才有人工照片）。
- [ ] 补 CSS：卡片网格、占位卡样式。

完成条件：

- 四个动作全部可用，撤销为按钮反面；
- 面板不再出现 `resolution` 的原始枚举字样。

提交建议：

```text
feat(review): rebuild the defect photo panel around photo cards
```

## Task 5：未归属面板降为只读

**修改：**

- `frontend/src/review/components/UnlinkedPhotosPanel.tsx`
- `frontend/src/review/components/UnlinkedPhotosPanel.test.tsx`

步骤：

- [ ] 先写失败测试：面板不再渲染任何操作按钮；仍显示未归属数量与缩略图。
- [ ] 移除「关联到病害」「重置校对状态」与目标病害下拉。
- [ ] 标题改为「未归属的照片」，元信息改为「系统判断：… · 我的处理：…」，不再直吐字段名。
- [ ] 保留 `reviewTargetId` 锚点，系统评定分区跳转仍需定位。

完成条件：

- 归属操作只剩病害侧一个入口；
- 全局"还有多少图没着落"的计数仍在。

提交建议：

```text
refactor(review): make the unassigned photo panel read-only
```

## Task 6：第一步回归

- [ ] 全量前端测试通过，且不低于 Task 0 记录的基线数。
- [ ] `tsc -b` 与 `vite build` 通过。
- [ ] 人工在浏览器过一遍：确认、添加、删除、确认缺图各走一次，撤销各走一次。
- [ ] 确认存量草稿里 `relinked` / `unrelated` 的记录重新呈现为待核对（设计 §9.4 的已知代价）。

验证：

```bash
npx tsc -b && npx vitest run && npm run build
```

提交：无（前面任务已分别提交）。

---

# 第二步：人工上传

> 本步开始向 `archive/` 写真实文件并写数据库行，**不可通过 git 回滚**。开工前先确认 `archive/`
> 目录规模，必要时备份。

## Task 7：单文件归档函数

**修改：**

- `backend-cpp/include/bridge_report/archive/ExtractedPhotoArchive.hpp`
- `backend-cpp/src/archive/ExtractedPhotoArchive.cpp`
- `backend-cpp/include/bridge_report/config/AppConfig.hpp`
- `backend-cpp/src/config/AppConfig.cpp`
- `backend-cpp/tests/test_extracted_photo_archive.cpp`
- `config/local.example.json`

步骤：

- [ ] 先写失败测试：合法 jpg/png 归档成功；改后缀的伪图片被拒；超过上限被拒；路径穿越被拒。
- [ ] 新增 `archive_uploaded_photo(content, original_file_name, candidate_id, context)`，复用现有
      `detect_image_extension`、`extension_matches`、`sha256`、`build_import_photo_relative_path`
      与原子重命名。
- [ ] 新增配置项 `photo_upload_max_bytes`，默认 20 MB，写法照 `word_upload_max_bytes`。
- [ ] 失败时不留下半个文件。

完成条件：

- 归档函数可独立测试，不依赖数据库与 HTTP。

提交建议：

```text
feat(archive): archive a single uploaded defect photo
```

## Task 8：上传端点

**新增：**

- `backend-cpp/include/bridge_report/http/DefectPhotoRoutes.hpp`
- `backend-cpp/src/http/DefectPhotoRoutes.cpp`
- `backend-cpp/tests/test_defect_photo_routes.cpp`

**修改：**

- `backend-cpp/CMakeLists.txt`
- `backend-cpp/src/main.cpp`

步骤：

- [ ] 先写失败测试（隔离 schema）：非待校对被拒；`warnings_only` 重开态被拒；
      `defect_candidate_id` 不存在被拒；库写失败时已写文件必须被删除。
- [ ] 实现 `POST /api/import-records/{import_id}/photos`，校验顺序照设计 §7.3。
- [ ] 先写文件，再开事务：写 `archived_files` 与 `import_record_files`
      （`file_purpose = '人工补充照片'`），追加候选到 `parsed_result_json`，提交。
- [ ] 事务失败回滚并删除已写文件。
- [ ] 编号与 candidate_id 按设计 §7.5 在本次导入内递增生成。
- [ ] 返回完整候选 JSON。

完成条件：

- 上传后 `parsed_result_json` 立即含该候选，刷新页面照片仍在；
- 任何失败路径都不留孤儿文件。

提交建议：

```text
feat(review): add a defect photo upload endpoint
```

## Task 9：删除端点

**修改：**

- `backend-cpp/src/http/DefectPhotoRoutes.cpp`
- `backend-cpp/tests/test_defect_photo_routes.cpp`

步骤：

- [ ] 先写失败测试：删 Word 抽出的照片返回 `photo_not_deletable`；删人工照片后草稿、两张表与
      归档文件都干净。
- [ ] 实现 `DELETE /api/import-records/{import_id}/photos/{photo_candidate_id}`。
- [ ] 事务顺序照设计 §7.6。

完成条件：

- Word 照片无法经端点删除；
- 人工照片删除后不留任何残留。

提交建议：

```text
feat(review): allow deleting a manually uploaded defect photo
```

## Task 10：前端接上传

**新增：**

- `frontend/src/api/defectPhotoApi.ts`

**修改：**

- `frontend/src/review/reviewDraft.ts`
- `frontend/src/review/components/DefectPhotoPanel.tsx`
- `frontend/src/review/components/DefectPhotoPanel.test.tsx`

步骤：

- [ ] 先写失败测试：上传成功后卡片出现且为已确认；各错误码显示对应文案；
      `warnings_only` 态上传入口禁用。
- [ ] 实现 `uploadDefectPhoto` 与 `deleteUploadedPhoto`（multipart 写法照 `workspaceApi`）。
- [ ] 新增 reducer `add_photo` / `remove_photo`。
- [ ] 「添加照片」弹窗增加"上传新图"来源，表单只有文件与照片说明两项。
- [ ] 人工照片的删除按钮走删除端点，二次确认写"将永久删除"。

完成条件：

- 两个来源在同一个「添加照片」入口下；
- 上传后不保存直接刷新，照片仍在。

提交建议：

```text
feat(review): upload and delete supplementary defect photos
```

## Task 11：全链路回归与验收

- [ ] C++ 编译通过。
- [ ] `scripts/dev/check-backend-tests.ps1` 全部通过。
- [ ] 前端 `tsc -b` + `vitest` + `vite build` 通过。
- [ ] 手工：上传一张图 → 出现在草稿 → 刷新后仍在 → 确认入库 → 查 `defect_photos` 有该行且
      `photo_title` 是填写的说明。
- [ ] 手工：上传一张改后缀的假图片，确认被拒且归档目录无残留。

验证：

```powershell
cmake --build backend-cpp/build/vs-debug --config Debug
powershell -ExecutionPolicy Bypass -File scripts/dev/check-backend-tests.ps1
```

提交：无。

---

## 最终验收清单

**第一步**

1. 照片区按实际照片组织，缺图以占位卡呈现；
2. 四个动作覆盖原七个动作的能力（换绑改两步）；
3. 两处照片操作用词一致，界面不出现 `resolution` 原始枚举；
4. 前端三件套通过，无回归。

**第二步**

1. 可上传 jpg / png / gif / bmp / webp / tiff，伪图片被拒；
2. 上传后刷新仍在；
3. 删除人工照片后草稿、两张表、归档文件均清理；
4. Word 照片无法经端点删除；
5. C++ 编译与 `check-backend-tests.ps1` 全过。
