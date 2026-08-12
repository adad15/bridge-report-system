# 03 桥梁年度检测数据 JSON 契约

日期：2026-07-03
当前合同版本：4.0（2026-08-10 修订）

> 2026-08-10 现行修订：运行时契约为 `BridgeAnnualInspectionData 4.0`。4.0 不含顶层 `ratings`，也不含照片级 `match_status` 或 `review_status`；病害组确认是当前照片关系的唯一确认动作。本文中合同 1.2、Word 评分和逐张照片状态的段落仅作历史设计记录，不适用于现行代码、Schema 或样例。

> 2026-07-13 修订说明：本次修订依据 `docs/superpowers/specs/changes/2026-07-13-change-001-component-rating-and-defect-location.md`，把病害详细位置、病害标度、病害扣分和构件评分双值校验纳入合同 1.2。本文中与本修订冲突的“只保存第四章评分、不复算构件评分”“标度/扣分/构件评分不进入合同”等 1.1 约定不再适用；完整 DP 推导规则库和更高层级评分重算仍不在模块 03 范围内。

## 1. 背景与目标

本模块是桥梁报告系统第三个小模块，负责定义 `BridgeAnnualInspectionData` JSON 契约。

模块 1 已确认工程边界：C++ Drogon 是主服务，Python FastAPI 是本地工具服务，React/Vite 是前端，PostgreSQL 是事实主库。模块 2 已确认数据库核心表、文件归档、导入记录、病害、照片、评定和病害对比表。

本模块位于 Word 导入原型、人工校对工作台、病害对比引擎之前。它要解决的问题是：Python 从 Word 抽取出的结果、C++ 保存到 `import_records.parsed_result_json` 的候选数据、前端校对页面读取的数据，以及 C++ 确认入库时使用的数据，必须遵守同一份稳定 JSON 契约。

本模块的目标是：

1. 定义年度检测候选数据 `BridgeAnnualInspectionData` 的顶层结构。
2. 明确软件生成 Word 和正式 Word 第一版可读取的可信区域。
3. 明确病害、尺寸、照片、评定、对比候选的 JSON 字段。
4. 明确来源、置信度、人工校对状态和错误警告的表达方式。
5. 明确候选 JSON 与模块 2 正式业务表之间的边界。
6. 为后续 `04-word-importer-prototype`、`05-review-workspace`、`07-defect-comparison-engine` 和章节生成模块提供稳定数据接口。

## 2. 范围

本模块负责：

1. `BridgeAnnualInspectionData` JSON 契约。
2. `import_records.parsed_result_json` 第一版结构。
3. 第二章结构病害检查表的病害候选结构。
4. 病害尺寸原文和结构化尺寸候选结构。
5. 病害照片编号、抽取图片和匹配状态结构。
6. 第四章总体技术状况评定表结构。
7. 历史病害对比候选结构。
8. 候选对象的来源、置信度、校对状态和 warning/error 结构。
9. 候选 JSON 到正式表的状态流转和入库原则。
10. 正式报告文本抽取的预留扩展口。

本模块不负责：

1. 具体 Word 解析算法。
2. Python 读取 docx 表格和图片的实现。
3. C++ 调 Python 的 HTTP API 具体路由。
4. 前端人工校对页面布局。
5. 病害对比算法。
6. 章节草稿生成规则。
7. AI 润色和 Milvus 检索。
8. 根据病害类型和标度独立推导病害扣分，或重新计算部件、结构分部和全桥评分。
9. 从正式报告自然语言正文中抽取病害事实。

## 3. 上游依赖

本模块依赖以下已确认文档：

1. `PROJECT_CONTEXT.md`
2. `docs/superpowers/specs/2026-07-01-bridge-report-system-design.md`
3. `docs/superpowers/specs/2026-07-01-modular-technical-doc-review-design.md`
4. `docs/superpowers/specs/modules/01-tech-stack-and-project-skeleton.md`
5. `docs/superpowers/specs/modules/02-postgresql-schema-and-file-archive.md`

上游已确认原则：

1. PostgreSQL 是结构化事实主库。
2. C++ 主服务是唯一事实写入入口。
3. Python 工具服务不直接写 PostgreSQL。
4. Word、图片、模板、附件和生成报告放在文件归档目录。
5. Word 自动解析结果先是候选数据，不直接成为事实。
6. 第一阶段候选数据存在 `import_records.parsed_result_json` 中，不单独建候选表。
7. 所有自动抽取结果必须保留来源、置信度和人工确认状态。
8. AI 和自动规则不能创造、修改或判断病害事实。

## 4. 下游影响

本模块会影响这些后续模块：

1. `04-word-importer-prototype`
   - Python 导入器必须输出本模块定义的 `BridgeAnnualInspectionData`。
2. `05-review-workspace`
   - 前端人工校对工作台读取和写回本模块定义的候选对象和校对状态。
3. `06-component-defect-archive`
   - 已确认病害会沉淀为构件病害档案。
4. `07-defect-comparison-engine`
   - 对比候选结构和确认状态由本模块先定义，算法后续细化。
5. `08-section-rule-generation`
   - 章节生成只能读取已确认事实和已确认对比结果，不读取未确认候选。
6. `09-docx-template-and-builder`
   - 正式报告生成依赖已确认年度事实、评分和对比结果。
7. `10-milvus-ai-polish-and-fact-check`
   - 文本扩展口以后可作为 AI 润色参考，但不能作为事实来源。

本模块确认后，后续模块不能随意改变这些约定：

1. JSON key 使用英文 `snake_case`。
2. 业务值、枚举值和报告原文保留中文。
3. 第一版只读取 Word 中可信的结构化表格区域。
4. 软件 Word 和正式 Word 都优先从第二章结构病害检查表抽取病害。
5. 第四章评定保存 Word 已有评分；第二章构件评分同时保存 Word 来源值，并允许依据 Word 已给出的病害扣分按 JTG/T H21-2011 第 4.1.1 条复算校验。
6. 对比候选在年度事实确认入库后生成。
7. 对比候选确认前仍存在 `parsed_result_json`，确认后才写正式对比表。

## 5. 核心设计原则

### 5.1 候选与事实分离

`BridgeAnnualInspectionData` 是候选数据，不是事实数据。

Python 工具服务只负责把 Word 中可信区域转换成候选 JSON。C++ 主服务保存候选 JSON，前端校对候选 JSON，用户确认后，C++ 主服务再写入正式业务表。

### 5.2 正式事实以数据库为准

每一年报告生成时，上一年度事实来自 PostgreSQL，不要求用户在常规流程中上传上一年正式 Word。

常规年度流程是：

```text
第 N 年 Word
  -> 抽取第 N 年病害、照片、评分候选
  -> 人工校对第 N 年事实
  -> C++ 写入第 N 年正式事实
  -> C++ 读取数据库第 N-1 年事实
  -> 生成历史对比候选
  -> 人工确认对比
  -> 写入对比事实
```

正式 Word 在第一版中的主要作用是首次建档或历史补录。若数据库中没有上一年度事实，可以上传上一年正式 Word，从第二章结构病害检查表抽取历史病害候选，人工确认后入库为历史基线。

### 5.3 第一版可信读取区域

软件生成 Word 和正式 Word 第一版都只读取可信结构化区域：

1. 第二章结构病害检查表。
2. 第四章总体技术状况评定表。

第一版不读取这些区域：

1. 封面。
2. 目录。
3. 检测依据。
4. 模板说明文字。
5. 自然语言结论段。
6. 历年检测情况正文。
7. 与最近一次检查结果对比正文。
8. 外观检查结论正文。

这些文字在当前样例中多为模板文字或自然语言总结，不作为事实来源，也不作为第一版报告生成参考。

### 5.4 预留正式报告文本扩展口

后续如果需要从正式报告中特定章节抽取文本，使用 `report_text_candidates` 扩展口。第一版该数组默认为空。

以后启用时，`report_text_candidates` 只能作为章节生成参考、人工复核参考或 AI 润色参考，不能直接创建病害事实，不能覆盖数据库事实。

### 5.5 来源、置信度和校对状态

自动抽取对象必须携带：

1. `source_ref`：说明来自哪份文件、哪个章节、哪张表、哪一行。
2. `confidence`：自动识别置信度，范围 0 到 1。
3. `warnings`：对象级警告。

默认按业务对象标注来源和置信度。病害等需要业务决策的对象另有 `review_status`；照片候选没有独立确认状态。必要时，病害类型、构件名称、照片编号、尺寸文本等易错字段可以扩展字段级来源。

## 6. 顶层 JSON 结构

`import_records.parsed_result_json` 第一版保存一份 `BridgeAnnualInspectionData`：

```json
{
  "contract": {},
  "import_context": {},
  "bridge_check": {},
  "inspection": {},
  "defects": [],
  "photos": [],
  "comparison_candidates": [],
  "report_text_candidates": [],
  "warnings": [],
  "errors": []
}
```

### 6.1 contract

记录契约版本、生成工具和解析器版本。

```json
{
  "name": "BridgeAnnualInspectionData",
  "version": "4.0",
  "generated_at": "2026-07-03T10:30:00+08:00",
  "producer": "python-tools",
  "parser_name": "word_table_importer",
  "parser_version": "0.1.0"
}
```

### 6.2 import_context

记录本次导入上下文。

```json
{
  "source_type": "软件导出Word",
  "file_role": "当前年度检测资料",
  "archived_file_system_number": "GDWJ-000001",
  "import_record_system_number": "DRJL-000001"
}
```

`source_type` 第一版支持：

1. `软件导出Word`
2. `正式Word`

后续预留：

1. `Excel病害表`
2. `图片包`
3. `JSON导入`
4. `接口同步`

### 6.3 bridge_check

只做桥梁校验，不自动修改桥梁档案。

```json
{
  "selected_bridge_system_number": "QL-000001",
  "extracted_bridge_name": "绕阳河二号桥",
  "match_status": "匹配",
  "warnings": []
}
```

桥梁基础信息以数据库为准。第一版导入前由用户选择桥梁，Word 中识别到的桥名、路线名只作为校验候选。

### 6.4 inspection

记录年度检测任务信息。

```json
{
  "inspection_year": 2026,
  "inspection_date": "2026-05-12",
  "report_number": "Q202605001-JZ-024",
  "project_name": "绕阳河二号桥定期检测",
  "data_role": "当前年度"
}
```

第一版桥梁、年份、报告编号、项目名称可以主要由用户在系统里选择或填写。Word 中识别到的信息只作为校验候选。

`data_role` 可取：

1. `当前年度`
2. `历史基线`
3. `修订版`

## 7. 通用对象

### 7.1 source_ref

`source_ref` 说明候选对象来自哪里。

```json
{
  "chapter": "第二章",
  "table_title": "上部结构病害检查表",
  "table_index": 3,
  "row_index": 5,
  "column_name": "病害描述",
  "raw_row_text": "主梁 | 梁底 | 裂缝 | L=0.8m，W=0.12mm | 2.1-1"
}
```

`raw_row_text` 是校对证据，不是报告正文参考。

### 7.2 warning

warning 用于表达可继续流程但需要人工关注的问题。

```json
{
  "code": "photo_number_unmatched",
  "message": "病害行引用照片编号 2.1-3，但未在图片区找到对应图片。",
  "severity": "warning",
  "target_candidate_id": "defect_0001"
}
```

`severity` 可取：

1. `info`
2. `warning`
3. `error`

### 7.3 review_status

病害使用：

1. `待确认`
2. `已确认`
3. `已修改`
4. `已忽略`

对比候选使用：

1. `待确认`
2. `已确认`
3. `已修改`
4. `已拒绝`

入库时只处理 `已确认` 和 `已修改`。`已忽略`、`已拒绝` 不写正式表。

## 8. defects 病害候选

`defects[]` 中一条记录对应第二章结构病害检查表的一行。

```json
{
  "candidate_id": "defect_0001",
  "structure_part": "上部结构",
  "component_name": "主梁",
  "component_alias": "1#孔主梁",
  "defect_type": "裂缝",
  "defect_location": "梁底",
  "defect_scale": 2,
  "defect_deduction": 35.0,
  "defect_description": "梁底存在横向裂缝",
  "quantity_text": "1处",
  "measurement_text": "L=0.8m，W=0.12mm",
  "measurements": [
    {
      "dimension_type": "长度",
      "value": 0.8,
      "unit": "m",
      "source_text": "L=0.8m"
    },
    {
      "dimension_type": "宽度",
      "value": 0.12,
      "unit": "mm",
      "source_text": "W=0.12mm"
    }
  ],
  "photo_numbers": ["2.1-1"],
  "group_review_status": "待确认",
  "confirmed_missing_photo_numbers": [],
  "severity": null,
  "remark": null,
  "source_ref": {
    "chapter": "第二章",
    "table_title": "上部结构病害检查表",
    "table_index": 3,
    "row_index": 5,
    "raw_row_text": "主梁 | 梁底 | 裂缝 | L=0.8m，W=0.12mm | 2.1-1"
  },
  "confidence": 0.92,
  "review_status": "待确认",
  "review_note": null,
  "warnings": []
}
```

字段说明：

1. `candidate_id` 是当前 JSON 内部唯一 ID，用于前端编辑和照片关联。
2. `structure_part` 取 `上部结构`、`下部结构`、`桥面系`、`全桥`、`其他`。
3. `component_name` 和 `component_alias` 先作为候选构件名称。人工确认后，可沉淀为 `bridge_components`。
4. `defect_type` 可由表格列直接读取；若由描述推断，必须降低置信度并写 warning。
5. `measurement_text` 必须保留原文。
6. `measurements[]` 尽量结构化解析，解析失败时可为空。
7. `photo_numbers[]` 只保存病害行中的照片编号；图片文件匹配放在 `photos[]`。
8. `group_review_status` 必填，只允许 `待确认` 或 `已确认`；解析器新建候选时写入 `待确认`。
9. `confirmed_missing_photo_numbers[]` 必填，只允许唯一字符串；解析器新建候选时写入空数组。
10. `defect_location` 保存当年报告的详细位置原文，第一版不拆分结构化位置字段。
11. `defect_scale` 和 `defect_deduction` 分别表示规范病害标度和 Word 病害扣分；不得从 `severity` 取值。

尺寸解析原则：

```text
原文必须保留，结构化结果尽量解析。
```

如果尺寸无法稳定结构化：

```json
{
  "measurement_text": "局部破损，约 20cm×30cm",
  "measurements": [],
  "warnings": [
    {
      "code": "measurement_parse_low_confidence",
      "message": "尺寸表达未能稳定结构化，请人工确认。",
      "severity": "warning"
    }
  ]
}
```

## 9. photos 照片候选

`photos[]` 记录照片编号、抽取图片文件候选和病害候选之间的当前关联关系。照片候选不维护独立确认状态。

```json
{
  "candidate_id": "photo_0001",
  "photo_number": "2.1-1",
  "linked_defect_candidate_id": "defect_0001",
  "extracted_file": {
    "temporary_file_name": "photo_0001.jpg",
    "original_caption": "照片2.1-1 主梁梁底裂缝",
    "archive_relative_path": null
  },
  "source_ref": {
    "chapter": "第二章",
    "table_title": "上部结构病害检查表",
    "row_index": 5,
    "photo_area_caption": "照片2.1-1 主梁梁底裂缝"
  },
  "confidence": 0.95,
  "warnings": []
}
```

照片规则：

1. 病害检查表中的照片编号列是主依据。
2. Word 图片区标题或说明作为校验依据。
3. 图片抽取失败时，不能丢掉病害表中的照片编号。
4. 自动关联不确定时，通过 `warnings[]`、`confidence` 和来源证据提示，不新增照片状态。
5. 用户可添加、删除、上传或重新关联照片；确认病害组时，同时确认该组当前照片关系。
6. 年度事实入库时，与已确认病害关联且归档完整的照片写入 `defect_photos`；未关联照片跳过并保留提示。

## 10. ratings 技术状况评定（历史设计，4.0 已删除）

以下内容描述已废弃的合同 1.2。现行 4.0 不读取或存储 Word 评分；病害事实确认后，由系统依据桥梁锁定的规范包和构件台账独立生成正式 assessment run。

根据表 4.1-2，总体技术状况评定表的层级是：

```text
评分最小单元：评价部件
等级最小单元：上部结构、下部结构、桥面系
全桥层级：桥梁总体技术状况评分 + 综合评级
```

因此本模块不使用 `component_types` 表示评分层级，而使用 `evaluation_parts` 表示评价部件评分项。

```json
{
  "ratings": {
    "overall": {
      "total_score": 85.61,
      "overall_grade": "2类",
      "source_ref": {
        "chapter": "第四章",
        "table_title": "总体技术状况评定表"
      },
      "confidence": 0.95,
      "review_status": "待确认"
    },
    "structure_parts": [
      {
        "structure_part": "上部结构",
        "structure_score": 87.45,
        "weight": 0.4,
        "grade": "2",
        "source_ref": {},
        "confidence": 0.95,
        "review_status": "待确认"
      },
      {
        "structure_part": "下部结构",
        "structure_score": 86.61,
        "weight": 0.4,
        "grade": "2",
        "source_ref": {},
        "confidence": 0.95,
        "review_status": "待确认"
      },
      {
        "structure_part": "桥面系",
        "structure_score": 79.93,
        "weight": 0.2,
        "grade": "3",
        "source_ref": {},
        "confidence": 0.95,
        "review_status": "待确认"
      }
    ],
    "evaluation_parts": [
      {
        "structure_part": "上部结构",
        "category_no": 1,
        "evaluation_part": "上部承重构件",
        "part_score": 86.62,
        "score_rows": [
          {
            "component_count": 1,
            "component_score": 55.81
          },
          {
            "component_count": 2,
            "component_score": 65
          },
          {
            "component_count": 13,
            "component_score": 100
          }
        ],
        "source_ref": {},
        "confidence": 0.95,
        "review_status": "待确认"
      },
      {
        "structure_part": "上部结构",
        "category_no": 2,
        "evaluation_part": "上部一般构件",
        "part_score": 82.29,
        "score_rows": [],
        "source_ref": {},
        "confidence": 0.95,
        "review_status": "待确认"
      },
      {
        "structure_part": "上部结构",
        "category_no": 3,
        "evaluation_part": "支座",
        "part_score": 100,
        "score_rows": [],
        "source_ref": {},
        "confidence": 0.95,
        "review_status": "待确认"
      }
    ],
    "component_ratings": [
      {
        "component_ref": {
          "structure_part": "上部结构",
          "component_name": "上部承重构件",
          "component_alias": "2-1#板"
        },
        "source_score": 55.81,
        "calculated_score": 55.8076118446,
        "confirmed_score": 55.81,
        "score_validation_status": "一致",
        "score_resolution_reason": null,
        "deduction_defect_candidate_ids": ["defect_0001", "defect_0002"],
        "calculation_details": {
          "standard": "JTG/T H21-2011 4.1.1",
          "ordered_deductions": [35.0, 20.0],
          "rounding_scale": 2
        },
        "review_status": "待确认"
      }
    ],
    "warnings": []
  }
}
```

入库映射：

1. `ratings.overall` 写入 `condition_ratings`，`rating_level = 全桥`，`rating_item_name = 全桥`。
2. `ratings.structure_parts[]` 写入 `condition_ratings`，`rating_level = 结构分部`，`rating_item_name = structure_part`，`grade` 有值。
3. `ratings.evaluation_parts[]` 写入 `condition_ratings`，`rating_level = 部件`，`rating_item_name = evaluation_part`，`grade = null`。
4. `ratings.component_ratings[]` 写入 `condition_ratings`，`rating_level = 构件`，绑定 `bridge_component_id`；`score` 保存最终确认分，来源分、复算分、状态和计算明细分别保存。

`evaluation_parts[]` 不设置等级。等级最小单元是 `structure_parts[]` 中的上部结构、下部结构和桥面系。

### 10.1 合同 1.2：病害详细位置、标度与扣分

`defects[]` 在原有字段基础上增加或明确：

```json
{
  "defect_location": "左侧端部",
  "defect_scale": 2,
  "defect_deduction": 35.0,
  "severity": "info"
}
```

- `defect_location` 是年度观测的详细位置原文，进入人工校对。
- `defect_scale` 是规范病害标度，允许为空但不得用 `severity` 代替。
- `defect_deduction` 是 Word 病害表给出的扣分 `DP`，允许为空但不得编造。
- `severity` 仍只表示 warning/error 提示严重程度。

### 10.2 合同 1.2：构件评分候选

`ratings.component_ratings[]` 一条记录对应一个报告年度中的一个具体桥梁构件：

```json
{
  "component_ref": {
    "structure_part": "上部结构",
    "component_name": "上部承重构件",
    "component_alias": "2-1#板"
  },
  "source_score": 55.81,
  "calculated_score": 55.8076118446,
  "confirmed_score": 55.81,
  "score_validation_status": "一致",
  "score_resolution_reason": null,
  "deduction_defect_candidate_ids": ["defect-1", "defect-2"],
  "calculation_details": {
    "standard": "JTG/T H21-2011 4.1.1",
    "ordered_deductions": [35.0, 20.0],
    "rounding_scale": 2
  },
  "review_status": "待确认"
}
```

`score_validation_status` 只能是：`一致`、`不一致`、`无法复算`、`人工接受Word值`、`人工采用复算值`。复算时先按扣分降序排列，按第 4.1.1 条累计扣分公式计算，全程不得提前舍入；任一 `DP=100` 时构件评分为 0。

`defect_scale` 为可空正整数；`defect_deduction`、`source_score`、`calculated_score`、`confirmed_score` 为可空的 0–100 数值。来源分和复算分按各自四舍五入到两位小数后的结果判断是否一致。不一致或无法复算时，解析器必须令 `confirmed_score=null`；用户在模块 05 选择最终分后写入 `confirmed_score`，并在 `score_resolution_reason` 保存必填原因。自动一致时可以预填来源分，`score_resolution_reason=null`。

合同只表达校验事实和证据，不授权 Python 直接写正式表。最终确认值仍由模块 05 人工校对并由 C++ 主服务事务入库。

## 11. comparison_candidates 对比候选

`comparison_candidates[]` 不是 Python 从 Word 中抽取的结果。它由 C++ 在年度事实确认入库后生成。

生成流程：

```text
第 N 年病害确认入库
  -> C++ 读取数据库第 N-1 年病害事实
  -> 生成 comparison_candidates
  -> 写回 import_records.parsed_result_json
  -> 前端人工确认
  -> 写入 defect_comparisons
```

一条对比候选：

```json
{
  "candidate_id": "comparison_0001",
  "previous_defect_observation_system_number": "BHGC-000123",
  "current_defect_observation_system_number": "BHGC-000456",
  "comparison_type": "原病害发展",
  "change_summary": "主梁梁底裂缝宽度由 0.10mm 发展至 0.12mm。",
  "match_basis": {
    "same_component": true,
    "same_defect_type": true,
    "location_similarity": 0.82,
    "measurement_change_detected": true,
    "photo_number_related": false
  },
  "confidence": 0.86,
  "confirmation_status": "待确认",
  "review_note": null,
  "warnings": []
}
```

`comparison_type` 可取：

1. `原病害无明显变化`
2. `原病害发展`
3. `原病害减轻`
4. `原病害修复`
5. `新增病害`
6. `原病害未见`
7. `无法判断`

新增病害：

```json
{
  "previous_defect_observation_system_number": null,
  "current_defect_observation_system_number": "BHGC-000456",
  "comparison_type": "新增病害"
}
```

上一年病害今年未出现：

```json
{
  "previous_defect_observation_system_number": "BHGC-000123",
  "current_defect_observation_system_number": null,
  "comparison_type": "原病害未见"
}
```

对比候选确认前存在：

```text
import_records.parsed_result_json.comparison_candidates
```

确认后写入：

```text
defect_comparisons
```

映射到 `defect_comparisons.comparison_result` 时：

1. `原病害无明显变化` -> `基本无变化`
2. `原病害发展` -> `加重`
3. `原病害减轻` -> `减轻`
4. `原病害修复` -> `已修复`
5. `新增病害` -> `新增`
6. `原病害未见` -> `消失`
7. `无法判断` -> `不确定`

## 12. report_text_candidates 文本扩展口

第一版默认：

```json
"report_text_candidates": []
```

后续如果从正式报告中特定章节抽取文本，可以放入该数组：

```json
{
  "candidate_id": "text_0001",
  "section_key": "previous_inspection_summary",
  "section_title": "1.4.1 历年检测情况",
  "text": "本桥上次检测时主要存在以下病害……",
  "usage": "章节生成参考",
  "source_ref": {
    "file_role": "历史正式报告",
    "paragraph_index": 12
  },
  "review_status": "待确认"
}
```

第一版不启用该抽取。即使后续启用，它也不能直接创建病害事实，不能覆盖数据库事实。

## 13. 状态流转

### 13.1 对象级状态

病害、照片、评分：

```text
待确认
已确认
已修改
已忽略
```

对比候选：

```text
待确认
已确认
已修改
已拒绝
```

### 13.2 导入记录级状态

模块 2 当前 `import_records.import_status` 已有基础状态：

```text
已上传
解析中
待校对
已确认
解析失败
已取消
```

03 设计层面的完整业务状态建议为：

```text
已上传
解析中
解析完成
校对中
年度事实已确认
对比候选已生成
对比确认中
已入库
```

失败状态：

```text
解析失败
校对退回
入库失败
```

模块 2 的数据库状态字段第一版可以先用现有枚举承载粗粒度状态，详细业务进度可存在 `validation_result_json` 或后续迁移扩展。03 只定义业务状态含义，不在本模块修改数据库表。

### 13.3 入库规则

年度事实入库：

```text
defects: 已确认 / 已修改 -> defect_observations
photos: 关联到已入库病害且归档文件完整 -> defect_photos
已忽略 -> 不入库
```

对比事实入库：

```text
comparison_candidates: 已确认 / 已修改 -> defect_comparisons
已拒绝 -> 不入库
```

`年度事实已确认` 不是最终完成。它只表示第 N 年病害和当前照片关系已经入库，接下来还要和第 N-1 年事实做对比，并由系统执行技术状况评定。

最终完整闭环是：

```text
已入库
```

即第 N 年事实和第 N 年相对第 N-1 年的对比事实都完成。

## 14. 完整示例（历史 1.2）

> 本节示例仅用于追溯旧契约。现行 4.0 的权威样例为 `samples/contracts/bridge_annual_inspection_data.v4.valid.json` 和 `samples/contracts/bridge_annual_inspection_data.v4.with-comparison.json`。

```json
{
  "contract": {
    "name": "BridgeAnnualInspectionData",
    "version": "1.2",
    "producer": "python-tools",
    "parser_name": "word_table_importer",
    "parser_version": "0.1.0"
  },
  "import_context": {
    "source_type": "软件导出Word",
    "file_role": "当前年度检测资料",
    "archived_file_system_number": "GDWJ-000001",
    "import_record_system_number": "DRJL-000001"
  },
  "bridge_check": {
    "selected_bridge_system_number": "QL-000001",
    "extracted_bridge_name": "绕阳河二号桥",
    "match_status": "匹配",
    "warnings": []
  },
  "inspection": {
    "inspection_year": 2026,
    "inspection_date": "2026-05-12",
    "report_number": "Q202605001-JZ-024",
    "project_name": "绕阳河二号桥定期检测",
    "data_role": "当前年度"
  },
  "defects": [
    {
      "candidate_id": "defect_0001",
      "structure_part": "上部结构",
      "component_name": "主梁",
      "component_alias": "1#孔主梁",
      "defect_type": "裂缝",
      "defect_location": "梁底",
      "defect_description": "梁底存在横向裂缝",
      "quantity_text": "1处",
      "measurement_text": "L=0.8m，W=0.12mm",
      "measurements": [
        {
          "dimension_type": "长度",
          "value": 0.8,
          "unit": "m",
          "source_text": "L=0.8m"
        },
        {
          "dimension_type": "宽度",
          "value": 0.12,
          "unit": "mm",
          "source_text": "W=0.12mm"
        }
      ],
      "photo_numbers": ["2.1-1"],
      "group_review_status": "待确认",
      "confirmed_missing_photo_numbers": [],
      "source_ref": {
        "chapter": "第二章",
        "table_title": "上部结构病害检查表",
        "row_index": 5,
        "raw_row_text": "主梁 | 梁底 | 裂缝 | L=0.8m，W=0.12mm | 2.1-1"
      },
      "confidence": 0.92,
      "review_status": "待确认",
      "warnings": []
    }
  ],
  "photos": [
    {
      "candidate_id": "photo_0001",
      "photo_number": "2.1-1",
      "linked_defect_candidate_id": "defect_0001",
      "extracted_file": {
        "temporary_file_name": "photo_0001.jpg",
        "original_caption": "照片2.1-1 主梁梁底裂缝",
        "archive_relative_path": null
      },
      "match_status": "高置信候选",
      "confidence": 0.95,
      "review_status": "待确认",
      "warnings": []
    }
  ],
  "ratings": {
    "overall": {
      "total_score": 85.61,
      "overall_grade": "2类",
      "confidence": 0.95,
      "review_status": "待确认"
    },
    "structure_parts": [
      {
        "structure_part": "上部结构",
        "structure_score": 87.45,
        "weight": 0.4,
        "grade": "2",
        "review_status": "待确认"
      }
    ],
    "evaluation_parts": [
      {
        "structure_part": "上部结构",
        "category_no": 1,
        "evaluation_part": "上部承重构件",
        "part_score": 86.62,
        "score_rows": [
          {
            "component_count": 1,
            "component_score": 55.81
          }
        ],
        "review_status": "待确认"
      }
    ],
    "warnings": []
  },
  "comparison_candidates": [],
  "report_text_candidates": [],
  "warnings": [],
  "errors": []
}
```

## 15. 验收标准

本模块设计完成后，需要满足：

1. 能表达软件 Word 第二章病害检查表的候选病害。
2. 能表达正式 Word 第二章病害检查表的历史基线病害候选。
3. 能表达病害尺寸原文和结构化尺寸。
4. 能表达照片编号、抽取图片、当前病害关联、来源、置信度和警告，不维护照片级确认状态。
5. 能表达表 2.x-1 的详细位置、病害标度和系统评定所需病害事实，不导入 Word 评分。
6. 能表达第 N 年事实确认后生成的历史对比候选。
7. 能明确候选数据和正式表之间的入库边界。
8. 能保留来源、置信度、必要的业务对象校对状态、warning 和 error。
9. 不抽取模板正文作为事实或参考。
12. 为正式报告文本抽取保留扩展口。

## 16. 暂缓事项

第一版暂缓：

1. 正式报告自然语言章节抽取。
2. 章节文本候选的详细字段和使用规则。
3. 从病害类型和标度推导 DP 的完整规则库，以及部件、结构分部和全桥评分重新计算。
4. 病害对比算法细节。
5. 前端校对工作台布局。
6. JSON Schema 文件和代码生成。
7. TypeScript/C++/Python 三端类型定义生成。
8. 多来源合并策略。
9. AI 辅助识别和润色。

## 17. 决策记录

| 日期 | 决策 | 原因 | 影响 |
| --- | --- | --- | --- |
| 2026-07-03 | 03 定义完整候选数据契约，不只定义 Python 返回值 | 后续校对、入库、对比都需要同一份候选结构 | `03-bridge-annual-inspection-data-contract`、`04-word-importer-prototype`、`05-review-workspace` |
| 2026-07-03 | 常规年度不要求上传上一年正式 Word | 上一年事实应来自 PostgreSQL，避免重复依赖 Word | 年度导入、病害对比 |
| 2026-07-03 | 正式 Word 第一版只聚焦第二章病害检查表和第四章评定表 | 正式报告格式与软件 Word 接近，但正文自然语言不稳定 | 历史基线导入 |
| 2026-07-03 | 第一版不抽取模板正文，不把章节文字作为事实或参考 | 非正式软件报告中大量文字是模板内容，没有事实价值 | 报告生成、AI 辅助 |
| 2026-07-03 | 预留 `report_text_candidates` 扩展口 | 后续可能从正式报告抽取特定章节文本 | 模块 8、模块 10 |
| 2026-07-03 | JSON key 使用英文 `snake_case`，业务值使用中文 | 跨语言代码稳定，同时保留桥检业务原文 | Python、C++、前端 |
| 2026-07-03 | 病害尺寸保留原文并尽量结构化 | 兼顾可靠校对和后续尺寸变化对比 | 病害校对、病害对比 |
| 2026-07-03 | 表 4.1-2 评定结构建模为 `overall`、`structure_parts`、`evaluation_parts` | 表 4.1-2 中等级最小单元是结构分部，评分行按评价部件聚合 | 技术状况评定、正式表入库 |
| 2026-07-03 | 对比候选在年度事实确认入库后生成 | 对比引擎处理事实对事实，避免未校对候选污染匹配 | 病害对比模块 |
| 2026-07-13 | 合同升级为 1.2，加入详细位置、病害标度、病害扣分和构件评分双值校验 | 为模块 06 提供可信构件年度事实，并修正 `severity` 与标度混用风险 | 模块 03、04、05、06 |
| 2026-07-14 | 合同 1.2 四端实施完成；`component_ratings[]` 每项在"至少包含"字段外补充 `candidate_id`（`component_rating_%04d`）与 `warnings[]`，用于前端编辑定位与对象级警告；三条状态不变量（不一致/无法复算 ⇒ 最终分与原因为空；人工状态 ⇒ 最终分与原因必填；一致 ⇒ 原因为空）在 Pydantic、C++、TypeScript 三端一致强制 | 实施合同 1.2 | Python、C++、前端、Schema |
