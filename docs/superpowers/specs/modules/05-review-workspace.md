# 05 人工校对工作台

日期：2026-07-07

> 2026-07-10 修订说明：病害与照片联合校对、A3 图片布局、契约 1.1、图片归档和最终确认事务加固，以 `docs/superpowers/specs/2026-07-10-review-workspace-hardening-design.md` 为准。本文保留模块 05 的总体目标、业务入口、年度修订和正式事实映射；与新设计冲突的旧页面及确认规则不再适用。

## 1. 背景与目标

本模块是桥梁报告系统第五个小模块，负责把模块 04 输出的 `BridgeAnnualInspectionData` 候选 JSON 变成可人工校对、可保存草稿、可确认入库的年度事实。

模块 04 已完成 `.docx` Word 导入原型：Python 工具服务读取第二章病害表、病害照片和第四章评分表，输出模块 03 定义的 `BridgeAnnualInspectionData`。模块 05 接在模块 04 后面，但不继续解析 Word，也不调用 Python。它只读取 C++ 主服务已保存到 `import_records.parsed_result_json` 的候选数据。

模块 05 的目标是：

1. 提供按桥梁和年度进入导入记录校对页的业务入口。
2. 展示普通候选、带 warning/error 候选和导入级 warning/error。
3. 允许用户编辑病害、照片和评分的核心业务字段。
4. 允许保存校对草稿，只写回 `import_records.parsed_result_json`。
5. 允许批量确认普通候选。
6. 在确认前做后端兜底校验。
7. 由 C++ 主服务把已确认或已修改的候选写入 PostgreSQL 正式事实表。
8. 同桥同年已有正式事实时，必须显式作为修订版确认，不允许静默覆盖。

## 2. 范围

本模块负责：

1. 前端人工校对工作台第一版页面。
2. 读取单条导入记录的候选 JSON。
3. 病害、照片、评分核心字段校对。
4. warning/error 分组展示。
5. 普通候选批量确认。
6. 保存校对草稿。
7. 入库前检查。
8. 年度事实确认入库。
9. 旧年度事实修订版确认入口。
10. C++ 到 PostgreSQL 正式事实表的入库映射。

本模块不负责：

1. Word 继续解析、规则识别或图片抽取优化。
2. 直接调用 Python 工具服务。
3. 从正式报告自然语言正文抽取事实。
4. 历史病害对比候选生成。
5. 历史病害对比确认页。
6. 完整图片管理、裁剪、重传和批量重命名。
7. 全量 JSON 编辑器。
8. AI 判断、修改或创造病害事实。
9. 完整多人账号权限和审签流。

## 3. 上游依赖

本模块依赖以下文档和实现：

1. `PROJECT_CONTEXT.md`
2. `docs/superpowers/specs/modules/02-postgresql-schema-and-file-archive.md`
3. `docs/superpowers/specs/modules/03-bridge-annual-inspection-data-contract.md`
4. `docs/superpowers/specs/modules/04-word-importer-prototype.md`
5. `contracts/bridge_annual_inspection_data.schema.json`
6. `frontend/src/contracts/annualInspection.ts`
7. `database/migrations/002_core_schema_and_archive.sql`

上游已确认原则：

1. PostgreSQL 是结构化事实主库。
2. C++ 主服务是唯一事实写入入口。
3. Python 工具服务不直接写 PostgreSQL。
4. 自动解析结果先是候选数据，不直接成为事实。
5. 候选 JSON 保存于 `import_records.parsed_result_json`。
6. `BridgeAnnualInspectionData` 是前端校对和后端入库的共同契约。
7. 候选对象必须保留来源、置信度、校对状态和 warning/error。
8. 同桥同年可以有多条导入记录，但只能有一份当前有效年度检测数据。

## 4. 下游影响

本模块完成后，后续模块可以依赖正式事实表，而不是未校对候选 JSON。

下游模块包括：

1. `06-component-defect-archive`
   - 读取已确认的构件、病害、尺寸和照片事实，形成构件病害档案。
2. `07-defect-comparison-engine`
   - 在第 N 年事实确认后，读取数据库第 N 年和第 N-1 年事实，生成历史病害对比候选。
3. `08-section-rule-generation`
   - 读取已确认年度事实和已确认对比结果，生成章节草稿。
4. `09-docx-template-and-builder`
   - 读取已确认事实、照片和评分，装配正式 Word。

模块 05 不生成 `comparison_candidates`。入库成功后，系统只提示下一步进入历史病害对比模块。

## 5. 业务入口

模块 05 第一版入口按桥梁组织：

```text
桥梁列表
  -> 桥梁详情
  -> 年度检测任务
  -> 导入记录
  -> 校对工作台
```

校对页绑定一条导入记录：

```text
import_record_id
```

一条导入记录对应一份 Word 解析结果，也对应一份待校对候选 JSON。

推荐前端路径：

```text
/bridges/:bridgeId/inspections/:inspectionYearId/imports/:importRecordId/review
```

第一版不单独做全系统待校对任务中心。以后可以增加待校对列表，但主流程仍以桥梁和年度为单位。

## 6. 核心流程

### 6.1 当前年度事实入库

```text
用户选择桥梁
  -> 创建或选择年度检测任务
  -> 上传当前年度软件导出 Word
  -> 模块 04 解析并保存候选 JSON
  -> 模块 05 打开校对工作台
  -> 用户处理 warning/error 项
  -> 用户批量确认普通候选
  -> 保存校对草稿
  -> 入库前检查
  -> 确认年度事实入库
  -> 后续进入历史病害对比模块
```

### 6.2 往年正式报告建库

往年正式报告建库走同一套校对和入库流程。

```text
用户新建或选择桥梁
  -> 创建 2025 年年度检测任务
  -> 上传 2025 年正式 Word
  -> 模块 04 解析第二章病害、照片和第四章评分
  -> 模块 05 校对
  -> 确认为 2025 年正式事实

用户创建 2026 年年度检测任务
  -> 上传 2026 年非正式软件 Word
  -> 模块 04 解析
  -> 模块 05 校对
  -> 确认为 2026 年正式事实
  -> 后续对比模块读取 2025 和 2026 年事实
```

桥梁基本信息仍以系统数据库为准。正式 Word 可以辅助历史事实建库，但不自动覆盖桥梁基础档案。

### 6.3 保存草稿

保存草稿只写回：

```text
import_records.parsed_result_json
```

保存草稿不写：

```text
defect_observations
defect_measurements
defect_photos
condition_ratings
```

这样用户中途校对不完整时，不会污染正式事实表。

### 6.4 确认入库

确认入库由 C++ 主服务执行。后端重新读取 `parsed_result_json`，重新做契约校验和入库前检查，然后在事务中写正式表。

确认入库只处理：

```text
review_status = 已确认
review_status = 已修改
```

不入库：

```text
review_status = 已忽略
```

阻止入库：

```text
review_status = 待确认
```

## 7. 页面结构

页面第一版按“问题优先 + 业务分区”组织。

### 7.1 顶部导入概览

顶部展示：

```text
桥梁名称
检测年度
导入记录编号
来源类型
解析规则 profile
导入状态
Word 文件
病害候选数量
照片候选数量
评分项数量
需要处理数量
已确认数量
已忽略数量
```

导入级 `warnings[]` 和 `errors[]` 放在顶部明显区域。

### 7.2 左侧校对导航

左侧按任务分组：

```text
需要处理
普通病害
病害照片
技术状况评定
来源证据
原始 JSON
```

每个分组显示数量。`需要处理` 永远放第一位，用于收拢带 warning/error 的候选。

### 7.3 中间主校对区

`需要处理` 显示问题清单：

```text
类型
对象编号
问题说明
严重程度
处理状态
```

`普通病害` 显示可编辑病害表格：

```text
结构部位
构件
位置
病害类型
数量
尺寸原文
照片编号
校对状态
备注
```

`病害照片` 显示照片列表或缩略图表格：

```text
照片编号
题注
关联病害
匹配状态
缩略图
校对状态
```

`技术状况评定` 显示：

```text
全桥总分
全桥等级
结构分部评分和等级
评价部件评分
校对状态
备注
```

第一版允许改评分值和等级，但不自动重算总分。

### 7.4 右侧证据面板

右侧显示当前选中对象的证据：

```text
candidate_id
Word 原始行文本
来源章节
表名
行号
解析置信度
warnings/errors
关联照片缩略图
照片题注
```

证据字段默认只读。

### 7.5 操作按钮

页面提供：

```text
保存草稿
批量确认普通候选
入库前检查
确认年度事实入库
取消导入
```

入库按钮必须依赖后端检查结果。仍有阻断错误时，不允许入库。

## 8. 可编辑字段

### 8.1 病害候选

第一版允许编辑：

```text
structure_part
component_name
component_alias
defect_location
defect_type
defect_description
quantity_text
measurement_text
photo_numbers
review_status
review_note
```

第一版只让用户编辑 `measurement_text`，系统根据尺寸原文重新结构化 `measurements[]`。用户不直接编辑每个结构化尺寸对象。

第一版只读：

```text
candidate_id
source_ref
confidence
raw_row_text
warnings
errors
```

### 8.2 照片候选

第一版允许编辑：

```text
photo_number
linked_defect_candidate_id
match_status
review_status
```

用户可以：

```text
确认匹配
取消匹配
标记未关联
标记忽略
修改照片编号
```

第一版不做图片裁剪、重传和复杂排序。

`extracted_file.original_caption` 是从 Word 中读取到的原始题注，第一版作为证据只读。若后续需要人工改照片标题或照片说明，应先扩展模块 03 契约，再进入实施。

### 8.3 评分候选

第一版允许编辑：

```text
全桥总分
全桥等级
结构分部评分
结构分部等级
评价部件评分
review_status
```

第一版不重新计算评分，不判断评分是否符合规范公式。

## 9. 分组和批量确认规则

### 9.1 需要处理

进入 `需要处理` 的对象包括：

```text
有对象级 warning/error 的病害
有对象级 warning/error 的照片
尺寸表达存在明显尺寸线索但未能结构化的病害
病害引用照片编号但未匹配到图片的记录
图片存在但未被任何病害引用的记录
导入级 warning/error 影响到的候选
```

### 9.2 普通候选

普通候选需满足：

```text
无对象级 warning/error
没有导入级 error 影响它
必填字段完整
照片关系明确
评分字段完整
```

普通候选可以批量确认。批量确认只把符合条件的对象改为 `已确认`，不会处理有问题的候选。

### 9.3 入库前兜底

确认入库前，后端必须重新检查：

```text
是否仍有待确认候选
已确认或已修改病害是否缺核心字段
病害照片关系是否明确
评分是否有全桥总分和等级
导入记录与桥梁、年度是否一致
同桥同年是否已有当前有效事实
```

## 10. C++ API 设计

### 10.1 获取校对数据

```http
GET /api/import-records/{import_record_id}/review
```

返回：

```text
导入记录基础信息
桥梁信息
年度检测信息
parsed_result_json
统计信息
是否已有同桥同年当前有效事实
```

### 10.2 保存校对草稿

```http
PUT /api/import-records/{import_record_id}/review-draft
```

请求体是修改后的 `BridgeAnnualInspectionData`。

后端处理：

```text
校验 JSON 契约
确认 import_record_id 与 JSON import_context 一致
更新 import_records.parsed_result_json
保持 import_records.import_status = 待校对
```

### 10.3 入库前检查

```http
POST /api/import-records/{import_record_id}/preflight-confirm
```

返回示例：

```json
{
  "can_confirm": false,
  "blocking_errors": [
    {
      "code": "candidate_pending_review",
      "message": "仍有 2 条病害候选处于待确认状态。",
      "target_candidate_id": "defect_0005"
    }
  ],
  "warnings": [],
  "requires_revision_confirmation": false
}
```

### 10.4 确认年度事实入库

```http
POST /api/import-records/{import_record_id}/confirm
```

请求体：

```json
{
  "confirm_revision": false,
  "confirmation_note": "人工校对完成"
}
```

如果同桥同年已有当前有效事实，必须传：

```json
{
  "confirm_revision": true,
  "confirmation_note": "作为修订版确认入库"
}
```

否则后端拒绝入库。

## 11. 入库规则

### 11.1 事务边界

确认入库必须在一个数据库事务中完成：

```text
重新读取导入记录
重新校验 BridgeAnnualInspectionData
执行入库前检查
处理同桥同年修订规则
写年度检测版本
写构件和构件别名
写病害观测
写病害尺寸
写病害照片
写技术状况评定
更新导入记录状态
提交事务
```

任何一步失败，事务回滚，`import_records.import_status` 不改成 `已确认`。

### 11.2 年度检测版本

如果同桥同年没有当前有效年度数据：

```text
使用当前 import_records.inspection_year_id
或创建该桥该年的 inspection_years 记录
设置 inspection_years.status = 已确认
设置 inspection_years.is_current = true
```

如果同桥同年已有当前有效年度数据：

```text
未显式 confirm_revision -> 拒绝
显式 confirm_revision -> 旧 inspection_years.is_current = false
旧 inspection_years.status = 已被修订
新 inspection_years.version_number = 旧版本号 + 1
新 inspection_years.revision_source_inspection_id = 旧版本 id
新 inspection_years.is_current = true
新 inspection_years.status = 已确认
```

### 11.3 构件沉淀

每条确认入库的病害需要绑定一个 `bridge_components` 记录。

第一版规则：

```text
按 bridge_id + structure_part + component_name + component_alias 查找已有构件
找到则复用
找不到则创建
component_alias 可写入 component_aliases
source_type = 导入识别
is_manually_confirmed = true
```

### 11.4 病害入库

```text
defects[] 中 已确认 / 已修改
  -> defect_observations
```

主要字段映射：

```text
inspection_year_id
bridge_id
bridge_component_id
source_import_record_id
source_file_id
source_page_number
source_table_title
source_row_index
structure_part
defect_type
defect_location
defect_description
quantity_text
severity
raw_text
extraction_confidence
review_status
review_note
```

候选状态映射到正式表状态：

```text
已确认 -> 已确认
已修改 -> 已修改
```

### 11.5 尺寸入库

```text
defect.measurements[]
  -> defect_measurements
```

如果 `measurements[]` 为空但 `measurement_text` 有值：

```text
写一条 measurement_type = 未识别尺寸
raw_text = measurement_text
is_auto_parsed = false
is_manually_confirmed = true
```

### 11.6 照片入库

```text
photos[] 中 已确认 / 已修改 且关联到已入库病害
  -> defect_photos
```

如果病害引用照片编号但没有匹配到图片：

```text
不写 defect_photos
在 defect_observations.review_note 或导入结果中保留说明
```

图片文件归档路径由 C++ 文件归档模块处理。模块 05 不依赖 Python 临时路径作为正式事实路径。

### 11.7 评分入库

```text
ratings.overall
ratings.structure_parts[]
ratings.evaluation_parts[]
  -> condition_ratings
```

第一版只保存 Word 或人工校对后的评分和等级，不重新计算评分。

## 12. 状态流转

### 12.1 导入记录状态

第一版沿用模块 02 现有枚举：

```text
已上传
解析中
待校对
已确认
解析失败
已取消
```

使用方式：

```text
解析完成 -> 待校对
保存草稿 -> 待校对
确认入库成功 -> 已确认
入库失败 -> 保持待校对
取消导入 -> 已取消
```

### 12.2 候选对象状态

候选对象使用模块 03 状态：

```text
待确认
已确认
已修改
已忽略
```

第一版页面不把 `已驳回` 作为候选状态。数据库正式表若使用 `已驳回`，仅用于后续事实审查扩展。

### 12.3 入库后状态

入库成功表示年度事实已确认，但不表示完整报告流程完成。

后续仍需：

```text
生成历史病害对比候选
人工确认历史病害对比
生成章节草稿
生成正式 Word
```

## 13. 错误处理

后端错误分为两类。

阻断错误：

```text
候选 JSON 不符合契约
导入记录不存在
导入记录与桥梁或年度不一致
仍有待确认候选
已确认或已修改病害缺必填字段
评分缺全桥总分或等级
同桥同年已有当前有效事实但未 confirm_revision
数据库事务写入失败
```

非阻断警告：

```text
有病害无照片
有未引用照片被忽略
部分尺寸无法结构化但保留原文
评分明细不完整但全桥评分和等级存在
```

阻断错误不允许入库。非阻断警告允许用户确认后继续。

## 14. 测试建议

第一版测试覆盖：

1. C++ 契约校验拒绝非法 `BridgeAnnualInspectionData`。
2. 保存草稿只更新 `parsed_result_json`，不写正式表。
3. 普通候选批量确认只处理无 warning/error 且必填完整的对象。
4. 待确认候选阻止入库。
5. 已忽略候选不入库。
6. 已确认病害写入 `defect_observations`。
7. 尺寸结构化结果写入 `defect_measurements`。
8. 有尺寸原文但无结构化尺寸时写 `未识别尺寸`。
9. 已确认照片写入 `defect_photos`。
10. 已确认评分写入 `condition_ratings`。
11. 同桥同年已有当前有效事实且未确认修订时拒绝入库。
12. 同桥同年修订确认后旧版本失效、新版本生效。
13. 入库事务失败时不留下半截事实。
14. 前端能按 `需要处理` 和普通候选分组显示。

## 15. 验收标准

模块 05 第一版完成时，应满足：

1. 能从桥梁年度导入记录进入校对页。
2. 能读取并展示 `import_records.parsed_result_json`。
3. 能展示导入级 warning/error 和候选对象 warning/error。
4. 能编辑病害核心字段。
5. 能编辑照片匹配状态。
6. 能编辑评分核心字段。
7. 能批量确认普通候选。
8. 能保存校对草稿。
9. 后端能执行入库前检查并返回可读错误。
10. 能确认入库并写入病害、尺寸、照片、评分正式表。
11. 同桥同年已有事实时，不显式确认修订则不能入库。
12. 模块 05 不生成历史病害对比候选。

## 16. 设计取舍

### 16.1 不做全量 JSON 编辑器

用户校对的是业务事实，不是 JSON 结构。第一版只暴露病害、照片和评分的核心业务字段。候选编号、来源引用、置信度和原始文本作为证据只读。

### 16.2 尺寸只编辑原文

用户编辑 `measurement_text`，系统重新生成 `measurements[]`。这样既能保留报告原文，又避免第一版页面变成复杂结构化尺寸编辑器。

### 16.3 普通候选可批量确认

没有 warning/error 且必填完整的候选可以批量确认。后端入库前仍重新检查，避免未处理问题混入正式库。

### 16.4 不生成历史对比候选

模块 05 聚焦年度事实入库。历史病害对比依赖第 N 年和第 N-1 年正式事实，应放到后续模块，避免模块 05 同时承担校对和匹配算法。

### 16.5 修订版必须显式确认

同桥同年已有当前有效事实时，系统不静默覆盖。用户必须选择作为修订版确认入库，旧版本才会标为已被修订。

## 17. 变更记录

| 日期 | 变更 | 原因 | 影响模块 |
| --- | --- | --- | --- |
| 2026-07-07 | 创建模块 05 人工校对工作台设计 | 明确候选 JSON 到正式事实表的人工确认闭环 | `05-review-workspace`、`06-component-defect-archive`、`07-defect-comparison-engine` |
| 2026-07-09 | 模块 05 实施完成：五个操作按钮接线、修订版确认弹窗、只读态、端到端手工验收通过 | 完成设计到可用页面的闭环，保存草稿/批量确认/入库前检查/确认入库/取消导入全部接后端，含修订版路径 | `05-review-workspace` |
| 2026-07-10 | 确认病害与照片联合校对及确认链路加固设计 | 保留普通病害全部字段，采用 A3 逐张照片校对，增加契约 1.1、图片归档和事务安全要求 | `03-contract`、`04-word-importer`、`05-review-workspace` |
