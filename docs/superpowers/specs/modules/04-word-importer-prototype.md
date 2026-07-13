# 04 Word 导入原型

> 2026-07-13 修订说明：依据 `docs/superpowers/specs/changes/2026-07-13-change-001-component-rating-and-defect-location.md`，导入器输出升级为 `BridgeAnnualInspectionData` 1.2。第二章病害表中的详细位置、标度、病害扣分和构件评分进入候选合同，并根据已提取扣分校验构件评分。本文中与该修订冲突的“标度、病害扣分、构件评分暂不进入契约”及“完全不重新计算评分”不再适用；第四章更高层级评分仍只抽取、不重算。

日期：2026-07-06

## 1. 背景与目标

本模块是桥梁报告系统第四个小模块，负责定义并实施 Python 工具层的 Word 导入原型。

模块 1 已确认工程边界：C++ Drogon 是主服务，Python FastAPI 是本地工具服务，React/Vite 是前端，PostgreSQL 是事实主库。模块 2 已确认文件归档、导入记录、候选 JSON 与正式事实表的边界。模块 3 已落地 `BridgeAnnualInspectionData` JSON 契约，Python、C++ 和前端都围绕这份契约校验候选数据。

本模块要解决的问题是：用户上传 Word 后，系统需要从可信结构化区域抽取病害、照片和技术状况评定，形成可人工校对的年度候选数据。第一版只支持 `.docx`，只抽取可信结构化数据，不解析自然语言正文作为事实。

模块 04 的目标是：

1. 定义 Word 导入原型的业务入口和服务边界。
2. 定义 Python 工具服务读取 `.docx` 的输入和输出。
3. 定义第二章病害检查表的识别和抽取规则。
4. 定义病害图片抽取、照片编号识别和匹配规则。
5. 定义第四章技术状况评定表的抽取规则。
6. 定义 warnings/errors、候选分组和人工校对提示规则。
7. 确保输出符合模块 3 的 `BridgeAnnualInspectionData` 契约。

## 2. 范围

本模块负责：

1. Python 工具层 `.docx` 读取原型。
2. 新桥初始化和已有桥年度导入两个业务入口下的 Word 解析边界。
3. 从正式报告或软件导出报告中抽取第二章结构病害检查表。
4. 从 Word 中抽取嵌入图片到临时输出目录。
5. 通过照片编号匹配病害表行和图片题注。
6. 从第四章抽取全桥评分、结构分部评分等级和评价部件评分。
7. 输出 `BridgeAnnualInspectionData` 候选 JSON。
8. 输出候选对象级 warning 和导入级 warning/error。
9. 通过模块 3 Python Pydantic 模型校验输出。

本模块不负责：

1. `.doc`、PDF、扫描件、图片 OCR 或 WPS 旧格式。
2. 自动刷新 Word 域。用户导入前仍需手动全选并按 F9 刷新照片编号域。
3. 桥梁主数据自动创建或覆盖。
4. 从桥梁基本信息表自动生成正式桥梁档案。
5. 从正式报告自然语言正文抽取病害事实。
6. 抽取历史检测情况正文、对比章节正文、外观检查结论正文或养护建议正文。
7. Word 版式还原、页面布局解析或报告模板解析。
8. 直接写 PostgreSQL。
9. 创建或更新导入记录、归档文件记录和年度检测版本。
10. 同桥同年重复导入、修订版判断、旧版失效或新版设为当前有效。
11. 人工校对页面。
12. 第 N 年与第 N-1 年病害对比算法。

## 3. 上游依赖

本模块依赖以下已确认文档和实现：

1. `PROJECT_CONTEXT.md`
2. `docs/superpowers/specs/2026-07-01-bridge-report-system-design.md`
3. `docs/superpowers/specs/2026-07-01-modular-technical-doc-review-design.md`
4. `docs/superpowers/specs/modules/01-tech-stack-and-project-skeleton.md`
5. `docs/superpowers/specs/modules/02-postgresql-schema-and-file-archive.md`
6. `docs/superpowers/specs/modules/03-bridge-annual-inspection-data-contract.md`
7. `tools-python/bridge_report_tools/contracts/annual_inspection.py`
8. `contracts/bridge_annual_inspection_data.schema.json`

上游已确认原则：

1. PostgreSQL 是结构化事实主库。
2. C++ 主服务是唯一事实写入入口。
3. Python 工具服务不直接写 PostgreSQL。
4. Word 自动解析结果先是候选数据，不直接成为事实。
5. 候选 JSON 使用模块 3 的 `BridgeAnnualInspectionData`。
6. 桥梁基本信息以系统数据库为准，Word 不作为桥梁基础档案权威来源。
7. 所有自动抽取对象必须保留来源、置信度和人工确认状态。
8. 病害检查表中的照片编号列是照片关联主依据。

## 4. 下游影响

本模块会影响这些后续模块：

1. `05-review-workspace`
   - 前端可以根据顶层 warnings/errors 和候选对象 warnings 分组展示校对任务。
2. `06-component-defect-archive`
   - 经人工确认的候选病害会沉淀成构件病害档案。
3. `07-defect-comparison-engine`
   - 当前年度事实确认后，C++ 才能基于数据库历史事实生成对比候选。
4. `08-section-rule-generation`
   - 章节生成读取已确认病害、照片、评分和已确认对比，不读取未确认候选。
5. `09-docx-template-and-builder`
   - 正式报告生成可以使用确认后的结构化事实和归档照片。

本模块确认后，后续模块不能随意改变这些约定：

1. Python 导入器只输出候选 JSON，不写事实库。
2. Word 抽取事实只来自可信结构化区域。
3. 第二章病害表和第四章评定表是第一版的唯一事实抽取区域。
4. 图片匹配是候选关系，人工确认前不能写正式照片事实。
5. 新桥初始化仍需先由系统产生桥梁编号，再挂接候选数据。
6. 修订版判断不属于 Word 导入器；它属于 C++ 主服务和数据库年度版本规则。

## 5. 业务入口

第一版支持两个业务入口。

### 5.1 新桥初始化

当系统中没有这座桥时，用户点击“新建桥梁”。用户可以选择：

1. 手动填写桥梁基本信息、某一年病害和评分。
2. 上传已有报告辅助初始化。

上传报告辅助初始化时，常见输入是正式报告 `.docx`。系统只从正式报告中抽取可信结构化数据：

1. 第二章病害检查表。
2. 病害图片和照片编号。
3. 第四章技术状况评定表。

桥梁基本信息仍由用户填写或确认。Word 中识别到的桥名、路线名、报告名称等只作为校验提示或别名候选，不自动覆盖桥梁主数据。

由于模块 3 契约要求 `selected_bridge_system_number`，新桥初始化的推荐流程是：

```text
用户填写或确认最低限度桥梁信息
  -> C++ 创建桥梁草稿或桥梁记录，生成 QL-xxxx
  -> C++ 归档上传 Word，生成 GDWJ-xxxx
  -> C++ 创建导入记录，生成 DRJL-xxxx
  -> C++ 调 Python 解析 .docx
  -> Python 输出历史基线候选 JSON 和临时图片
  -> 用户校对病害、图片和评分
  -> C++ 确认后写入该桥某一年的历史基线事实
```

模块 04 在该入口下使用：

```json
{
  "source_type": "正式Word",
  "file_role": "历史基线资料",
  "data_role": "历史基线"
}
```

### 5.2 已有桥年度导入

当系统中已有这座桥时，用户进入桥梁档案或年度检测任务，上传软件导出的非正式报告 `.docx`。

推荐流程：

```text
用户选择已有桥梁
  -> 用户创建或选择年度检测任务
  -> C++ 归档上传 Word，生成 GDWJ-xxxx
  -> C++ 创建导入记录，生成 DRJL-xxxx
  -> C++ 调 Python 解析 .docx
  -> Python 输出当前年度候选 JSON 和临时图片
  -> 用户校对病害、图片和评分
  -> C++ 确认后写入该年度事实
  -> C++ 后续读取数据库上一年事实生成对比候选
```

模块 04 在该入口下使用：

```json
{
  "source_type": "软件导出Word",
  "file_role": "当前年度检测资料",
  "data_role": "当前年度"
}
```

### 5.3 修订版边界

同桥同年再次导入、用户选择作为修订版、旧版标为已被修订、新版成为当前有效，这些属于 C++ 主服务和数据库年度版本规则。

Python 导入器不判断修订版，也不输出 `data_role = 修订版`。对 Python 来说，每次调用都是解析当前传入的 `.docx` 并输出候选 JSON。

## 6. Python 服务边界

### 6.1 调用关系

前端只直接调用 C++ 主服务，不直接调用 Python 工具服务。

Python 工具服务不接收浏览器上传文件。C++ 先负责上传、归档和创建导入记录，再把本地 `.docx` 路径和导入上下文传给 Python。

```text
前端上传 Word
  -> C++ 接收文件
  -> C++ 写入归档目录
  -> C++ 创建导入记录
  -> C++ 调 Python /imports/word/parse
  -> Python 读取归档 .docx
  -> Python 抽取结构化候选和临时图片
  -> C++ 保存 parsed_result_json
```

### 6.2 请求结构

Python API 第一版建议为：

```text
POST /imports/word/parse
```

请求示例：

```json
{
  "docx_path": "archive/bridges/QL-000001_绕阳河二号桥/2026/imports/DRJL-000001/input/GDWJ-000001_报告.docx",
  "temporary_photo_output_dir": "archive/bridges/QL-000001_绕阳河二号桥/2026/imports/DRJL-000001/temp-photos",
  "rule_profile": "辽宁国省干线",
  "import_mode": "已有桥年度导入",
  "source_type": "软件导出Word",
  "file_role": "当前年度检测资料",
  "data_role": "当前年度",
  "selected_bridge_system_number": "QL-000001",
  "selected_bridge_name": "绕阳河二号桥",
  "inspection_year": 2026,
  "inspection_date": "2026-05-18",
  "report_number": "Q202605001-JZ-024",
  "project_name": "绕阳河二号桥2026年度定期检测",
  "archived_file_system_number": "GDWJ-000001",
  "import_record_system_number": "DRJL-000001"
}
```

字段说明：

| 字段 | 来源 | 用途 |
| --- | --- | --- |
| `docx_path` | C++ | 已归档 `.docx` 本地路径，Python 只读 |
| `temporary_photo_output_dir` | C++ | Python 写临时图片文件的目录 |
| `rule_profile` | C++/前端 | 用户点选的解析规则；第一版支持 `辽宁国省干线` |
| `import_mode` | C++/前端 | API 请求内的业务入口提示，不写入契约 |
| `source_type` | C++/前端 | 写入 `import_context.source_type` |
| `file_role` | C++/前端 | 写入 `import_context.file_role` |
| `data_role` | C++/前端 | 写入 `inspection.data_role` |
| `selected_bridge_system_number` | C++ | 写入 `bridge_check.selected_bridge_system_number` |
| `selected_bridge_name` | C++/前端 | 用于和 Word 中识别到的桥名做校验 |
| `inspection_year` | 用户/系统 | 写入 `inspection.inspection_year` |
| `inspection_date` | 用户/系统 | 写入 `inspection.inspection_date` |
| `report_number` | 用户/系统 | 写入 `inspection.report_number` |
| `project_name` | 用户/系统 | 写入 `inspection.project_name` |
| `archived_file_system_number` | C++ | 写入 `import_context.archived_file_system_number` |
| `import_record_system_number` | C++ | 写入 `import_context.import_record_system_number` |

`report_number`、`inspection_date` 和 `project_name` 以系统输入为准。Word 中即使识别到同类信息，也只用于校验，不自动覆盖系统值。

`rule_profile` 由 C++ 根据用户选择传入。第一版支持 `辽宁国省干线`，Python 不自动判断模板类型。

### 6.3 响应结构

响应示例：

```json
{
  "data": {
    "contract": {},
    "import_context": {},
    "bridge_check": {},
    "inspection": {},
    "defects": [],
    "photos": [],
    "ratings": {},
    "comparison_candidates": [],
    "report_text_candidates": [],
    "warnings": [],
    "errors": []
  },
  "temporary_photo_files": [
    "photo_0001.jpg",
    "photo_0002.jpg"
  ]
}
```

`data` 必须通过 `BridgeAnnualInspectionData` 1.2 Pydantic 模型校验。`temporary_photo_files` 是 Python 写入临时目录的文件名清单，便于 C++ 后续归档。

图片 JSON 中：

```json
{
  "extracted_file": {
    "temporary_file_name": "photo_0001.jpg",
    "original_caption": "照片2.1-1 主梁梁底裂缝",
    "archive_relative_path": null
  }
}
```

`archive_relative_path` 第一版由 Python 固定输出 `null`。正式归档路径和图片 `GDWJ` 编号由 C++ 后续处理。

## 7. 第二章病害表解析

### 7.1 表格识别策略

第一版不按固定表格序号硬编码。识别时综合使用：

1. 章节上下文：`第二章`、`结构病害`、`外观检查`、`病害检查`。
2. 表题：`上部结构病害检查表`、`下部结构病害检查表`、`桥面系病害检查表`。
3. 表头列名：`构件`、`部位`、`位置`、`病害`、`数量`、`尺寸`、`照片编号`。

表格序号只写入 `source_ref.table_index` 用于追溯，不作为识别主依据。

辽宁国省干线真实样例还支持以下列名别名：

```text
部件名称 | 构件编号 | 病害位置 | 病害类型 | 病害特征 | 照片编号
```

当前字段映射为：`component_name` 取部件/构件名称类列，`component_alias` 取构件编号类列；详细病害位置写入 `defect_location`；`病害特征` 作为 `measurement_text` 原文保存；`标度`、`病害扣分` 分别写入 `defect_scale`、`defect_deduction`；构件组中的 `构件评分` 写入 `ratings.component_ratings[].source_score`。

### 7.2 结构分部判断

优先从表题判断：

| 表题关键词 | `structure_part` |
| --- | --- |
| 上部结构 | `上部结构` |
| 下部结构 | `下部结构` |
| 桥面系 | `桥面系` |

如果表题缺失，可从附近章节标题或表头上下文推断。推断结果置信度降低，并写对象级 warning。

### 7.3 行到候选对象映射

一行病害检查表记录映射为一条 `defects[]`。

示例行：

```text
上部结构 | 主梁 | 第二跨左幅梁底 | 裂缝 | 1处 | L=0.8m，W=0.12mm | 2.1-1
```

输出示例：

```json
{
  "candidate_id": "defect_0001",
  "structure_part": "上部结构",
  "component_name": "主梁",
  "component_alias": null,
  "defect_type": "裂缝",
  "defect_location": "第二跨左幅梁底",
  "defect_description": "第二跨左幅梁底裂缝",
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
  "photo_numbers": [
    "2.1-1"
  ],
  "severity": null,
  "remark": null,
  "source_ref": {
    "chapter": "第二章",
    "table_title": "上部结构病害检查表",
    "table_index": 3,
    "row_index": 5,
    "raw_row_text": "上部结构 | 主梁 | 第二跨左幅梁底 | 裂缝 | 1处 | L=0.8m，W=0.12mm | 2.1-1"
  },
  "confidence": 0.92,
  "review_status": "待确认",
  "review_note": null,
  "warnings": []
}
```

### 7.4 尺寸解析

尺寸解析原则：

```text
原文必须保留，结构化结果尽量解析。
```

`measurement_text` 必须保留 Word 原文。`measurements[]` 只解析常见且稳定的表达：

| 表达 | 尺寸类型 |
| --- | --- |
| `L=0.8m` | `长度` |
| `W=0.12mm` | `宽度` |
| `S=0.3m2`、`A=0.3m2` | `面积` |
| `S=0.6×0.1m²` | `面积`，按乘积计算 |
| `长度：5m` | `长度` |
| `总面积：1m²` | `总面积` |
| `D=0.15m` | `间距` |
| `3处`、`2条` | `数量` |

如果 `measurement_text` 只是病害描述或状态描述，例如 `基本完好`，不生成结构化尺寸，也不写尺寸解析 warning。已经解析出明确尺寸时，不因为旁边存在“横向裂缝”“勾缝砂浆脱落”等描述性文字而写 warning。

仍有明显数字、单位、乘号等尺寸线索但无法稳定解析时，才写 warning：

```json
{
  "measurement_text": "局部破损，约20左右",
  "measurements": [],
  "warnings": [
    {
      "code": "measurement_parse_low_confidence",
      "message": "尺寸表达未能稳定结构化，请人工确认。",
      "severity": "warning",
      "target_candidate_id": "defect_0001"
    }
  ]
}
```

### 7.5 照片编号解析

病害表中的照片编号列是照片关联主依据。支持识别：

1. `2.1-1`
2. `照片2.1-1`
3. `2.1-1、2.1-2`
4. `2.1-1/2.1-2`

解析结果写入 `defects[].photo_numbers`，只保存编号本体，例如 `2.1-1`。

如果病害行照片编号为空，病害仍保留，`photo_numbers=[]`，并写 warning。

## 8. 图片抽取和匹配

### 8.1 抽取目标

第一版同时抽取 Word 中的嵌入图片，但不还原页面版式。图片抽取只服务于病害校对，不直接成为正式照片事实。

Python 负责：

1. 从 `.docx` 包内读取嵌入图片。
2. 写入 C++ 提供的临时输出目录。
3. 生成稳定临时文件名，例如 `photo_0001.jpg`。
4. 识别图片题注中的照片编号。
5. 通过照片编号和病害候选匹配。

C++ 负责：

1. 把临时图片移动或复制到正式归档目录。
2. 生成图片归档文件编号 `GDWJ-xxxx`。
3. 确认后写入 `defect_photos`。

### 8.2 题注识别

第一版不还原页面版式，但会优先按图片在 `.docx` 主文档 XML 中的出现顺序定位题注：

1. 找到图片对应的 `a:blip`。
2. 优先读取图片所在的单图表格单元格文本。
3. 其次读取只包含一张图片的内层表格文本。
4. 如果以上路径没有题注，再回退扫描普通段落题注。

这样可以覆盖辽宁国省干线真实样例中“图片在表格内、题注文字在图片下方”的结构，同时避免把病害检查表中的“照片编号”列误当作图片区题注。

题注识别支持：

1. `照片2.1-1 主梁梁底裂缝`
2. `照片 2.1-1 主梁梁底裂缝`
3. `图2.1-1 主梁梁底裂缝`
4. `2.1-1 主梁梁底裂缝`
5. `照片2.11 主梁梁底裂缝`，规范化为 `2.1-1`，兼容部分 Word XML 文本中连字符丢失的情况。

题注识别只需要确定照片编号和原始题注文本，不需要判断图片在页面上的左右位置、跨页或环绕方式。

### 8.3 匹配状态

| 情况 | `match_status` | 处理 |
| --- | --- | --- |
| 病害表编号与图片题注编号唯一对应 | `高置信候选` | 关联 `linked_defect_candidate_id` |
| 病害表有编号但找不到图片 | 无照片对象或照片对象 `待校对` | 病害对象写 warning |
| 图片题注有编号但病害表未引用 | `未关联` | 照片对象保留，写 warning |
| 编号重复或一对多关系不明确 | `待校对` | 照片对象保留，写 warning |

高置信匹配示例：

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
  "match_status": "高置信候选",
  "source_ref": {
    "chapter": "第二章",
    "table_title": "上部结构病害检查表",
    "photo_area_caption": "照片2.1-1 主梁梁底裂缝"
  },
  "confidence": 0.95,
  "review_status": "待确认",
  "warnings": []
}
```

未引用图片示例：

```json
{
  "candidate_id": "photo_0003",
  "photo_number": "2.1-3",
  "linked_defect_candidate_id": null,
  "extracted_file": {
    "temporary_file_name": "photo_0003.jpg",
    "original_caption": "照片2.1-3 桥面铺装局部破损",
    "archive_relative_path": null
  },
  "match_status": "未关联",
  "confidence": 0.6,
  "review_status": "待确认",
  "warnings": [
    {
      "code": "photo_not_referenced_by_defect",
      "message": "Word 图片区存在照片编号 2.1-3，但病害表未引用。",
      "severity": "warning",
      "target_candidate_id": "photo_0003"
    }
  ]
}
```

## 9. 第四章评分解析

### 9.1 解析范围

第四章只抽取 Word 中已有评分和等级，不重新计算部件、结构分部或全桥评分。第二章构件评分按下述 9.2 规则复算校验。

### 9.2 第二章构件评分校验

同一构件的病害扣分按从大到小排序，依据 JTG/T H21-2011 第 4.1.1 条累计计算构件评分。导入器输出 Word 来源分、未提前舍入的复算分、校验状态、参与计算的病害候选 ID 和计算明细。

无法稳定提取全部扣分时，保留来源分并标记 `无法复算`，不得补造扣分；来源分与复算分按两位小数比较，不一致时标记 `不一致`。这两种状态的 `confirmed_score` 初始为空，由模块 05 显式处理并填写原因。Python 结果仍是候选，C++ 在正式入库前必须独立复核。

第一版抽三层：

1. 全桥综合结果：总评分、综合等级。
2. 结构分部结果：上部结构、下部结构、桥面系的评分、权重、等级。
3. 评价部件结果：评价部件名称、类别编号、评分、分数组成行。

### 9.3 等级规则

等级只存在于：

1. `ratings.overall.overall_grade`
2. `ratings.structure_parts[].grade`

`ratings.evaluation_parts[]` 不设置等级。评价部件只有评分，没有等级。

### 9.4 缺字段规则

辽宁国省干线 `表4.1-2` 当前支持两类布局：

1. 层级行布局：用 `层级` 列区分 `全桥`、`结构分部`、`评价部件`。
2. 矩阵布局：表头包含 `结构`、`类别`、`评价部件`、`构件数量`、`构件评分`、`桥梁部件技术状况评分`、`桥梁结构技术状况评分`、`桥梁结构组成权重`、`等级`、`桥梁总体技术状况评分`、`综合评级`。

矩阵布局中，`overall` 取首个有效行的 `桥梁总体技术状况评分` 和 `综合评级`；`structure_parts[]` 按结构分部去重；`evaluation_parts[]` 按结构、类别、评价部件和部件评分聚合，并把每行的 `构件数量/构件评分` 追加为 `score_rows[]`。

能抽多少抽多少。缺少局部字段时，输出候选 JSON，并写 warning。

示例：

```json
{
  "code": "rating_structure_part_missing",
  "message": "第四章评定表未识别到桥面系结构分部评分，请人工确认。",
  "severity": "warning",
  "target_candidate_id": null
}
```

如果完全找不到第四章评定表，第一版直接判定为解析失败。原因是模块 3 当前契约要求 `ratings.overall` 必填，导入器不能编造总评分或综合等级，也不能输出不符合契约的半成品 JSON。

## 10. 桥梁和年度信息校验

`report_number`、`inspection_date` 和 `project_name` 以系统输入为准。

Word 中如果识别到同类信息：

1. 与系统值一致：不写 warning。
2. 与系统值不一致：写导入级 warning。
3. Word 中缺失：不影响导入。

报告编号不一致示例：

```json
{
  "code": "report_number_mismatch",
  "message": "系统填写报告编号为 Q202605001-JZ-024，Word 中识别到 Q202605001-JZ-025，请人工确认。",
  "severity": "warning",
  "target_candidate_id": null
}
```

检测日期不一致示例：

```json
{
  "code": "inspection_date_mismatch",
  "message": "系统填写检测日期为 2026-05-18，Word 中识别到 2026-05-20，请人工确认。",
  "severity": "warning",
  "target_candidate_id": null
}
```

桥梁名称校验规则：

1. `selected_bridge_system_number` 必须由 C++ 提供。
2. `selected_bridge_name` 由系统提供，用于校验。
3. Word 中识别到的桥名写入 `bridge_check.extracted_bridge_name`。
4. 匹配、不匹配或待人工确认写入 `bridge_check.match_status`。
5. Word 桥名不覆盖系统桥梁名称。

## 11. Warnings、Errors 和校对分组

### 11.1 写入位置

可定位到候选对象的问题，写入该对象自己的 `warnings[]`，并设置 `target_candidate_id`。

无法定位到具体对象的问题，写入顶层 `warnings[]` 或 `errors[]`，`target_candidate_id=null`。

### 11.2 前端分组

模块 04 不新增分组字段。前端后续可根据现有字段计算：

| 分组 | 规则 |
| --- | --- |
| 导入级问题 | 顶层 `warnings[]` 或 `errors[]` |
| 需审查候选 | 候选对象 `warnings[]` 非空 |
| 普通候选 | 候选对象 `warnings[]` 为空 |

校对顺序建议：

```text
先看导入级问题
  -> 再看需审查候选
  -> 最后快速扫普通候选
```

### 11.3 常用 warning/error code

| code | 位置 | 含义 |
| --- | --- | --- |
| `unsupported_file_type` | 顶层 error | 输入不是 `.docx` |
| `docx_open_failed` | 顶层 error | 文件打不开或损坏 |
| `required_context_missing` | 顶层 error | 必填上下文缺失 |
| `temporary_photo_output_unwritable` | 顶层 error | 临时图片目录不可写 |
| `contract_validation_failed` | 顶层 error | 输出 JSON 不符合契约 |
| `bridge_name_mismatch` | `bridge_check.warnings` 或顶层 warning | Word 桥名和系统桥名不一致 |
| `defect_tables_not_found` | 顶层 error | 未识别到第二章病害表 |
| `defect_table_low_confidence` | 顶层 warning | 表格识别置信度低 |
| `defect_row_missing_required_cell` | 病害对象 warning | 病害行缺少关键单元格 |
| `measurement_parse_low_confidence` | 病害对象 warning | 尺寸无法稳定结构化 |
| `photo_number_missing` | 病害对象 warning | 病害行缺少照片编号 |
| `photo_number_unmatched` | 病害对象 warning | 病害照片编号找不到对应图片 |
| `photo_not_referenced_by_defect` | 照片对象 warning | 图片未被病害表引用 |
| `photo_number_duplicate` | 照片对象 warning | 图片编号重复 |
| `rating_table_not_found` | 顶层 warning/error | 未识别到第四章评定表 |
| `rating_structure_part_missing` | 顶层 warning | 结构分部评分缺失 |
| `rating_evaluation_part_missing` | 顶层 warning | 评价部件评分缺失 |

## 12. 失败与可校对边界

### 12.1 直接解析失败

以下情况不返回可用候选 JSON，导入记录应进入 `解析失败`：

1. 输入不是 `.docx`。
2. 文件打不开或损坏。
3. 调用方缺少必填上下文，例如桥梁编号、年份、报告编号、检测日期、归档文件编号或导入记录编号。
4. 临时图片输出目录不可写。
5. 完全未识别到第四章技术状况评定表。
6. 生成的 JSON 无法通过 `BridgeAnnualInspectionData` 校验。

### 12.2 解析完成但进入待校对

以下情况返回候选 JSON，导入记录可以进入 `待校对`：

1. Word 中桥名和系统选择桥名不一致。
2. 第二章某个分部病害表没找到。
3. 第四章评分表已找到，但局部字段没找到。
4. 病害尺寸解析不稳定。
5. 病害照片编号找不到对应图片。
6. 图片区有照片但病害表没有引用。
7. 某些表头列名不完全匹配，只能低置信抽取。

如果完全未识别到第二章病害表，但第四章评分表可解析，第一版仍返回合法候选 JSON：`defects=[]`、`photos=[]`，并在顶层 `errors[]` 写入 `defect_tables_not_found`。这样用户能在导入记录中看到明确原因，后续也便于根据真实样例优化识别规则。

## 13. 数据契约输出

Python 导入器必须输出模块 3 的 `BridgeAnnualInspectionData`。顶层结构固定：

```json
{
  "contract": {},
  "import_context": {},
  "bridge_check": {},
  "inspection": {},
  "defects": [],
  "photos": [],
  "ratings": {},
  "comparison_candidates": [],
  "report_text_candidates": [],
  "warnings": [],
  "errors": []
}
```

第一版约定：

1. `comparison_candidates=[]`。对比候选由 C++ 在年度事实确认后基于数据库历史事实生成。
2. `report_text_candidates=[]`。不抽正式报告正文或模板文字。
3. `review_status` 默认 `待确认`。
4. 所有候选对象携带 `source_ref`、`confidence` 和 `warnings`。
5. 生成后必须调用模块 3 Python Pydantic 模型校验。

## 14. 技术实现建议

第一版建议使用：

1. `python-docx` 读取段落、表格、单元格文本和基本文档顺序。
2. Python 标准库 `zipfile` 读取 `.docx` 包内媒体文件。
3. Python 标准库 `pathlib` 管理本地路径。
4. 模块 3 Pydantic 模型校验输出。

内部文件职责建议：

| 文件 | 职责 |
| --- | --- |
| `tools-python/bridge_report_tools/importers/word_importer.py` | 对外导入器入口，协调解析流程 |
| `tools-python/bridge_report_tools/importers/word_context.py` | API 请求上下文模型 |
| `tools-python/bridge_report_tools/importers/docx_reader.py` | `.docx` 文档读取和表格/段落序列化 |
| `tools-python/bridge_report_tools/importers/defect_tables.py` | 第二章病害表识别和行解析 |
| `tools-python/bridge_report_tools/importers/photo_extractor.py` | 图片抽取、题注识别和照片编号匹配 |
| `tools-python/bridge_report_tools/importers/rating_tables.py` | 第四章评分表识别和解析 |
| `tools-python/bridge_report_tools/importers/measurements.py` | 尺寸原文解析 |

这些文件名是实施建议。最终实施计划可根据实际代码量调整，但应保持职责清晰，避免把所有解析逻辑堆进 FastAPI 路由。

## 15. 测试与验收标准

### 15.1 测试资料策略

当前仓库没有真实 `.docx` 样例。第一版测试可以先用代码动态生成最小 `.docx` 夹具：

1. 包含第二章病害检查表。
2. 包含照片题注和嵌入图片。
3. 包含第四章评分表。

后续拿到真实样例后，再加入非公开本地验收或脱敏样例。

### 15.2 单元测试

至少覆盖：

1. `.docx` 后缀检查。
2. 缺少必填上下文时返回解析失败。
3. 能按表题和表头识别上部结构病害表。
4. 能从病害行抽取构件、位置、病害类型、数量、尺寸和照片编号。
5. 能保留 `measurement_text` 并解析常见 `L`、`W`、`D`、面积乘积、中文长度/面积标签和数量。
6. 能抽取嵌入图片到临时目录。
7. 能用段落题注或表格单元格中的图片下方题注照片编号匹配病害候选。
8. 病害引用照片编号但图片缺失时写对象级 warning。
9. 图片未被病害表引用时写照片对象 warning。
10. 能抽取全桥评分、结构分部评分等级、评价部件评分和第二章构件评分。
11. 能抽取详细位置、病害标度和病害扣分，并保持 `severity` 与标度语义分离。
12. 能按 JTG/T H21-2011 第 4.1.1 条校验构件评分，且计算过程不提前舍入。
13. 评价部件不会输出 `grade`。
14. 输出能通过 `BridgeAnnualInspectionData.model_validate()`。

### 15.3 API 测试

至少覆盖：

1. `POST /imports/word/parse` 接受合法请求并返回 `data`。
2. 响应中的 `temporary_photo_files` 和 `photos[].extracted_file.temporary_file_name` 一致。
3. 非 `.docx` 请求返回错误。
4. 损坏 `.docx` 请求返回错误。

### 15.4 验收标准

模块 04 第一版完成后应满足：

1. 能解析新桥初始化输入中的正式报告 `.docx` 的第二章病害表、图片和第四章评分。
2. 能解析已有桥年度导入输入中的软件导出报告 `.docx` 的第二章病害表、图片和第四章评分。
3. 能输出合法 `BridgeAnnualInspectionData`。
4. 能保留病害原始行、表格序号、行号和表题。
5. 能保留尺寸原文并尽量结构化常见尺寸。
6. 能抽取图片到临时目录，并通过照片编号生成图片候选。
7. 能把可定位问题写入对象级 warnings。
8. 能把导入级问题写入顶层 warnings/errors。
9. 不写数据库。
10. 不生成对比候选。
11. 不抽正式报告正文作为事实或文本候选。
12. 真实辽宁样例保持 25 条病害、31 个照片候选和 36 个 Word 图片，并得到 `1-1#板=65`、`1-2#板=65`、`2-1#板=55.81`、上部承重构件 `86.62`、全桥 `85.61`。

## 16. 风险与取舍

### 16.1 正式报告格式差异

新桥初始化常用正式报告，但正式报告可能经过人工排版和修改，格式比软件导出报告更不稳定。

取舍：第一版只从正式报告中抽第二章病害表、图片和第四章评分，不抽正文。识别失败时写 warning/error，由人工校对或后续优化识别规格。

### 16.2 图片题注和图片关系不稳定

`.docx` 中图片和题注的 XML 关系可能复杂，尤其是浮动图片、文本框或表格内图片。

取舍：第一版不还原版式，但会读取图片所在单元格或单图内层表格中的题注文本，并按照片编号匹配。能匹配就输出高置信候选，不确定就输出待校对。

### 16.3 评分表缺失和契约必填冲突

模块 3 当前契约要求 `ratings.overall` 必填。如果第四章评分表完全找不到，导入器需要选择符合契约的处理方式。

取舍：第一版把第四章评分表完全缺失作为解析失败，不编造评分，不输出不合法 JSON。若以后要允许“无评分但病害可校对”的导入，需要通过变更提案调整模块 3 契约。

### 16.4 新桥初始化和桥梁主数据

用户希望通过上传报告辅助新建桥梁，但 Word 不应直接成为桥梁主数据权威来源。

取舍：第一版要求系统先产生桥梁编号，Word 解析结果只挂接候选病害、图片和评分。桥梁基本信息由用户填写或确认。

### 16.5 修订版不在导入器处理

同桥同年重复导入确实是业务需求，但它不是 Word 解析问题。

取舍：模块 04 不判断修订版。C++ 主服务根据同桥同年正式事实和用户选择处理版本关系。

## 17. Codex 技术把关意见

本模块可以进入用户评审。

把关结论：

1. 模块范围已经收敛到 `.docx` 可信结构化抽取，适合第一版实施。
2. 新桥初始化和已有桥年度导入两个入口贴合真实工作流，同时不破坏 Python 不写库的边界。
3. 正式报告进入新桥初始化场景是合理的，但只抽结构化表格和图片，避免把自然语言正文变成事实来源。
4. 修订版从模块 04 移出是合理的，它属于 C++ 和数据库年度版本管理。
5. 图片抽取纳入第一版有助于后续校对页面，但按照片编号候选匹配，不追求版式还原，范围可控。
6. warnings/errors 分组规则能直接服务模块 05 校对体验，不需要修改模块 3 契约。
7. 最大技术风险是第四章评分缺失与模块 3 `ratings` 必填之间的契约约束，实施计划中要优先处理。

用户需要重点确认：

1. 是否接受新桥初始化时先创建桥梁草稿或桥梁记录，再调用 Python 解析。
2. 是否接受正式报告第一版只抽第二章病害表、图片和第四章评分。
3. 是否接受桥梁基本信息由用户填写或确认，Word 只做校验提示。
4. 是否接受修订版不进入模块 04，由后续 C++/数据库流程处理。
5. 是否接受第一版不还原 Word 版式，只按结构化表格、照片编号和题注匹配。

## 18. 变更记录

| 日期 | 变更 | 原因 | 影响模块 |
| --- | --- | --- | --- |
| 2026-07-06 | 创建模块 04 Word 导入原型设计 | 明确第一版 `.docx` 导入范围、两类业务入口、图片抽取和评分抽取规则 | `04-word-importer-prototype`、`05-review-workspace` |
| 2026-07-06 | 将新桥初始化纳入模块 04 使用场景 | 用户确认系统没有桥梁时可通过上传报告辅助初始化 | 桥梁新建流程、导入流程 |
| 2026-07-06 | 明确正式报告只作为新桥初始化的结构化事实候选来源 | 用户确认新建桥梁上传解析的报告通常是正式报告 | Word 导入、人工校对 |
| 2026-07-06 | 明确修订版不属于 Python 导入器 | 修订版是同桥同年版本管理问题，不影响 Word 解析算法 | C++ 主服务、数据库年度版本 |
| 2026-07-07 | 新增规则模块化和辽宁国省干线规则方向 | 用户确认规则由前端点选，Python 按 `rule_profile` 解析；辽宁国省干线只抽表2.1-1/2.2-1/2.3-1、照片2.x-x、表4.1-1/4.1-2 | Word 导入、规则模块 |
| 2026-07-07 | 补充真实辽宁样例适配细节 | 当前实现已支持真实病害表头、矩阵评分表、表格下方照片题注、紧凑照片编号和扩展尺寸解析 | Word 导入、规则模块、校对提示 |
| 2026-07-13 | 输出升级为合同 1.2，并加入第二章构件评分校验 | 模块 06 需要标度、扣分、详细位置和可追溯构件分 | 模块 03、04、05、06 |
