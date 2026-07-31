# 病害照片操作模型重构设计

## 1. 背景

病害与照片分区的照片处理目前有三个问题。

**一、操作的对象不是真正生效的对象。** 界面按 Word 原文写的照片编号逐条列出，用户操作的是
`defect.photo_references[]`。但决定照片能否正式入库的判定在
`backend-cpp/src/review/ConfirmPlan.cpp:259`，读的全是 `photos[]`：

```
photo.review_status ∈ {已确认, 已修改}
  且 photo.match_status = 已确认
  且 photo.linked_defect_candidate_id 指向该病害
```

`photo_references[].resolution`（pending / matched / relinked / missing / unrelated）不参与任何入库
判定，只随 `build_raw_cells_json` 存进 `source_raw_cells_json` 作为来源证据。用户在界面上忙活的
那套状态机，和最终生效的事实是两回事。

**二、七个动作分散在两个面板，用两套语汇。** 病害详情的照片区有「照片正确 / 原文引用到其他病害 /
确认无关 / 确认缺图 / 撤销处理」，分区底部的未关联照片面板有「关联到病害 / 重置校对状态」。
同一件事（撤销我刚才的判断）在两处叫不同的名字；「照片正确」这个名字没说清确认的是编号对应
关系而非照片质量。

**三、没有补拍照片的入口。** 照片只能来自 docx 抽取。现场补拍、事后重拍的照片无法进入报告。

另有五个照片相关的 reducer action 没有任何组件调用（`photo_confirm_match`、`photo_mark_unrelated`、
`photo_ignore`、`edit_photo_number`、`unconfirm_missing_photo`），合计约 70 行。

## 2. 已确认的设计结论

1. 照片操作收敛为四个动作：确认照片、添加照片、删除照片、确认缺图；
2. 换绑由「删除 + 添加」两步替代，不再提供一步换绑；
3. 照片区以**实际照片**为主体组织，Word 引用但缺失的编号作为占位卡混排在同一列；
4. 「添加照片」有两个来源：从本次导入未归属的照片里挑，以及从本机上传新图；
5. 上传时用户只填照片说明，编号由系统生成；
6. 删除分两种语义：Word 抽出的照片退回未归属清单，人工上传的照片彻底删除（含归档文件）；
7. 分区底部的未关联照片面板降为只读清单，只承担"还有多少图没着落"的全局完整性检查；
8. 上传采用双写：服务端在事务内归档并追加候选到 `parsed_result_json`，同时返回候选给前端
   追加到本地草稿；
9. 分两步实施：第一步纯前端改交互模型，第二步加上传。

## 3. 目标

1. 让用户操作的对象与最终入库的事实一致；
2. 把七个动作收敛为四个，两个面板统一语汇；
3. 支持补拍照片进入报告；
4. 删除不可达的照片 action；
5. 上传不产生无法从界面清理的归档文件；
6. 不改动 `BridgeAnnualInspectionData` 契约。

## 4. 非目标

本期不做：

- 一步换绑；
- 照片裁剪、旋转、压缩等编辑能力；
- 批量上传；
- 修改 Word 抽出照片的编号；
- 删除 Word 抽出的照片的归档文件；
- 历史导入记录的数据迁移。

## 5. 交互模型

### 5.1 卡片派生规则

```
卡片列表 =
  ① photos 中 linked_defect_candidate_id === 本病害的每一张   → 照片卡
  ② photo_references 中，找不到「已挂在本病害且 photo_number 相同」
     照片的每个编号                                          → 占位卡
```

占位卡两态：

- `resolution = missing` → 显示「原报告缺图」，视为已处理，不计入问题；
- 其余 → 显示「待核对」，计入问题，阻断批量确认。

该规则自动处理删除：摘掉一张 Word 照片后，它不再满足规则 ①，其编号随即满足规则 ②，卡片自动
变回「待核对」占位卡。不需要额外的状态字段。

### 5.2 四个动作

| 动作 | 落点 | 可反向 |
| --- | --- | --- |
| 确认照片 | 照片卡 | 是（撤销确认） |
| 添加照片 | 面板级 | —— |
| 删除照片 | 照片卡 | —— |
| 确认缺图 | 占位卡 | 是（撤销缺图） |

撤销不新增按钮位，做成同一按钮的反面：照片卡未确认时显示「确认照片」，已确认时显示
「撤销确认」；占位卡同理。

### 5.3 主动添加即确认

用户从未归属清单里主动挑一张照片挂到病害上，这个动作本身已经表达了"这张是对的"，因此直接置
`match_status = 已确认`。「确认照片」只用于**系统自动挂上**的照片——那些是机器判断的结果，需要
人工点头。

否则手动挑一张要连点两次，冗余。

### 5.4 未归属照片面板

降为只读清单：显示本次导入中 `linked_defect_candidate_id` 为空的照片缩略图与数量，可预览，
不带任何操作按钮。归属操作统一回到病害卡片的「添加照片」。

它仍是一个有价值的全局检查：告诉用户还有多少张图没有着落。

## 6. 数据规则

各动作写入的字段：

**确认照片 / 撤销确认**

- 确认：`photo.match_status = 已确认`，`photo.review_status = 已确认`；若该照片的 `photo_number`
  与本病害某条 Word 引用相同，该引用置 `resolution = matched`、`photo_candidate_id = 该照片`、
  `resolved_defect_candidate_id = 本病害`；
- 撤销：`match_status` 退回 `待校对`（不是 `高置信候选`——那是系统判断的档位，一旦人工干预过
  就不该再宣称是系统的高置信结论），对应引用退回 `pending` 并清空两个目标字段。照片仍挂在本
  病害上，解除归属是「删除照片」的职责。

**添加照片（挑现有）**

`photo.linked_defect_candidate_id = 本病害`，同时按「主动添加即确认」置
`match_status = 已确认`、`review_status = 已确认`；编号能对上 Word 引用时一并置该引用为 `matched`。

**添加照片（上传）**

见第 7 节。

**删除照片**

- Word 抽出的（`source_ref.source_type = "word"`）：`linked_defect_candidate_id = null`、
  `match_status = 未关联`、`review_status = 已修改`；对应的 Word 引用退回 `pending` 并清空两个
  目标字段。照片回到未归属清单，归档文件保持不动。
- 人工上传的（`source_ref.source_type = "manual"`）：调用删除端点，从草稿移除候选并回收归档
  文件。二次确认文案写明「将永久删除」。

**确认缺图 / 撤销缺图**

- 确认：该引用 `resolution = missing`，两个目标字段保持为空（契约要求 `missing` 不带目标）；
- 撤销：退回 `pending`。

### 6.1 reducer 动作收敛

| 新 action | 顶掉的旧 action |
| --- | --- |
| `link_photo_to_defect` | `photo_relink`、`confirm_photo_reference_match` 的挂载部分 |
| `unlink_photo_from_defect` | `photo_reset`、`confirm_unrelated_photo_reference` |
| `confirm_photo`（可反向） | `confirm_photo_reference_match` 的确认部分、`reset_photo_reference_review` |
| `set_photo_reference_missing`（可反向） | `confirm_missing_photo` |
| `add_photo` / `remove_photo` | 上传与删除专用，第二步引入 |

`relink_photo_reference` 移除，换绑改由删除 + 添加两步完成。

同时删除五个零调用的 action：`photo_confirm_match`、`photo_mark_unrelated`、`photo_ignore`、
`edit_photo_number`、`unconfirm_missing_photo`。

## 7. 上传端点

### 7.1 接口

```
POST   /api/import-records/{import_id}/photos
DELETE /api/import-records/{import_id}/photos/{photo_candidate_id}
```

两者共同的前置条件：已登录、持有有效编辑锁、导入记录处于「待校对」、当前不是 `warnings_only`
重开态。最后一条是硬约束：`validate_warnings_only_scope` 明令禁止新增候选，该态下上传入口禁用。

### 7.2 上传请求

`multipart/form-data`，字段：

- `file`：图片文件，必填；
- `defect_candidate_id`：目标病害，必填；
- `caption`：照片说明，可空。

### 7.3 校验顺序

1. multipart 解析成功，且恰好一个名为 `file` 的文件；
2. 大小不超过新配置项 `photo_upload_max_bytes`（默认 20 MB，与 `word_upload_max_bytes` 同一写法）；
3. 按魔数判定格式，复用 `detect_image_extension`，支持 jpg / png / gif / bmp / webp / tiff；同时
   沿用 `extension_matches` 校验扩展名与文件内容一致，改后缀的伪图片被拒；
4. `defect_candidate_id` 存在于本次导入的 `defects[]` 中。

### 7.4 文件与数据库的先后

文件系统与数据库无法真正原子，沿用 `archive_extracted_photos` / `cleanup_archived_photo_batch`
的既有模式：**先写文件，再开事务写库；库写失败则删除已写文件。**

事务内三件事：

1. `insert archived_files` 与 `insert import_record_files`，`file_purpose = '人工补充照片'`
   （该列无枚举约束，不需要迁移）；
2. 把新候选追加进 `parsed_result_json`；
3. 提交。

归档路径复用 `build_import_photo_relative_path`，落在本次导入自己的照片目录下；文件名沿用
`<candidate_id>_<hash 前 12 位><扩展名>`。

### 7.5 新候选

```json
{
  "candidate_id": "manual_photo_0001",
  "photo_number": "补-1",
  "linked_defect_candidate_id": "defect_0042",
  "extracted_file": {
    "temporary_file_name": "IMG_2031.jpg",
    "original_caption": "用户填写的说明",
    "archive_relative_path": "..."
  },
  "match_status": "已确认",
  "source_ref": { "source_type": "manual" },
  "confidence": 1.0,
  "review_status": "已确认",
  "warnings": []
}
```

- `match_status` 与 `review_status` 直接为「已确认」，依据 5.3 的「主动添加即确认」；
- `confidence: 1.0`，人工上传不是机器推断；
- `photo_number` 取本次导入中 `补-` 前缀的最大序号加一，天然避开 Word 的 `2.1-5` 这类编号；
- `candidate_id` 取 `manual_photo_%04d`，同样在本次导入内递增；
- `original_caption` 会在确认入库时作为 `photo_title` 写入 `defect_photos`
  （`backend-cpp/src/review/ConfirmPlan.cpp:282`），即最终报告的图注。

契约无需修改：`source_ref.source_type` 已支持 `"manual"`。

### 7.6 删除端点

仅允许删除 `source_ref.source_type = "manual"` 的候选。对 Word 抽出的照片返回
`photo_not_deletable`——那种"删除"是纯前端操作（改写 `linked_defect_candidate_id`），不经过本端点。

事务：从 `parsed_result_json` 移除候选 → 删除 `import_record_files` 行 → 删除 `archived_files`
行 → 删除归档文件。

### 7.7 双写与草稿一致性

上传成功后，服务端的 `parsed_result_json` 与前端本地草稿各持有一份相同的候选：

- 用户继续编辑并保存 → 前端草稿覆盖服务端，内容一致；
- 用户不保存直接刷新 → 从服务端取回的草稿含该照片，内容一致；
- 不存在"归档有文件、草稿无引用"的幽灵状态。

代价是同一条数据写两处，端点比"只归档"复杂一个事务内 JSON 追加。这是刻意的取舍：只归档的
方案会在"上传后不保存直接刷新"时留下界面永远看不到、也无法清理的归档文件。

## 8. 错误分类

| 错误码 | 含义 | HTTP |
| --- | --- | --- |
| `invalid_photo_file` | 不是支持的图片，或扩展名与内容不符 | 400 |
| `photo_file_too_large` | 超过 `photo_upload_max_bytes` | 413 |
| `defect_candidate_not_found` | 目标病害不在本次导入中 | 404 |
| `import_record_not_editable` | 导入记录不处于待校对 | 409 |
| `photo_not_deletable` | 试图经端点删除 Word 抽出的照片 | 409 |

编辑锁相关错误沿用现有那一套，不新增。

## 9. 已知取舍

1. **换绑变两步。** 原来「原文引用到其他病害」一步完成，现在要先在源病害删除、再到目标病害
   添加。这是收敛动作数量的直接代价，已确认接受。
2. **未归属清单不能直接操作。** 归属统一从病害侧发起，少一个入口，但也少一套并行语汇。
3. **上传的照片不参与 Word 引用核对。** 它没有对应的 `photo_references` 条目，因此不会顶替
   任何 Word 编号，也不会被计入「待核对」。这是正确的：Word 没承诺过这张图。
4. **存量草稿里的 `relinked` 与 `unrelated` 会重新显示为「待核对」。** 新模型不再产生这两个
   `resolution` 值：换绑改为两步，"无关"由"照片不挂在本病害上"表达。按 5.1 的派生规则，历史
   草稿里带这两个值且没有对应已挂照片的引用，会重新出现为「待核对」占位卡，需要人工再处理
   一次。本期不迁移历史数据，这是刻意接受的代价——受影响的只有尚未确认入库的存量草稿。

## 10. 分步实施

### 第一步：交互模型（纯前端）

不触及后端、归档与数据库。

- 按 5.1 实现卡片派生；
- 实现四个动作，其中「添加照片」只提供"从未归属清单挑"；
- 「删除照片」只处理 Word 照片的退回未归属；
- 撤销做成按钮反面；
- 未归属面板降为只读清单；
- 统一两处照片操作的用词；
- 删除五个零调用的 action 与 `relink_photo_reference`。

上传入口在这一步不出现，不做禁用占位按钮。

完成后用户可以用新模型完整走通现有流程，仅补拍照片尚不支持。

### 第二步：上传

- 后端：单文件归档函数、两个端点、`photo_upload_max_bytes` 配置项；
- 前端：两个 API 函数、`add_photo` / `remove_photo` 两个 action、上传对话框；
- 「添加照片」弹窗增加"上传新图"来源。

## 11. 测试策略

### 11.1 第一步（前端单测）

- 卡片派生：有图、缺图、上传图三类同时存在时都出现在列表中且顺序稳定；
- 删除一张 Word 照片后自动变回「待核对」占位卡；
- 主动添加的照片直接为已确认，无需二次点击；
- 确认 ↔ 撤销确认、确认缺图 ↔ 撤销缺图 两个 toggle 均可往返；
- 确认缺图后该占位卡不再计入问题、不再阻断批量确认；
- 四个新 action 均不原地修改传入 state；
- 回归：现有全部前端测试保持通过。

### 11.2 第二步

- C++ 单测（归档函数）：合法图片、改后缀的伪图片、超过大小上限、路径穿越；
- C++ 集成测（隔离 schema）：上传端点在库写失败时必须删除已写文件；删除端点三处清理都到位；
  非待校对与 `warnings_only` 态被拒；
- 前端：上传对话框、各错误码文案、`warnings_only` 态禁用；
- 手工验证：上传一张图 → 出现在草稿 → 刷新后仍在 → 确认入库后写入 `defect_photos`。

## 12. 验收标准

**第一步**

1. 照片区按实际照片组织，缺图以占位卡呈现；
2. 四个动作可覆盖原七个动作的全部能力（换绑除外，改两步）；
3. 两处照片操作用词一致；
4. `tsc -b`、`vitest`、`vite build` 全部通过，现有测试无回归。

**第二步**

1. 可上传 jpg / png / gif / bmp / webp / tiff，改后缀的伪图片被拒；
2. 上传后刷新页面照片仍在；
3. 删除人工上传的照片后，草稿、两张数据库表与归档文件均已清理；
4. Word 抽出的照片无法经端点删除；
5. C++ 编译通过，`scripts/dev/check-backend-tests.ps1` 全部通过。

## 13. 实施前提醒

第二步会向 `archive/` 写入真实文件并写数据库行，不可通过 `git checkout` 回滚。动手前需确认
`archive/` 目录当前规模，必要时先行备份。
