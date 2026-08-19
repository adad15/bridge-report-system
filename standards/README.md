# 版本化桥梁规范包

本目录只保存可执行规范数据和其来源索引。评分算法由 C++ 后端适配器实现；前端和 Python 不直接加载规则包，也不各自实现评分公式。

## 目录约定

```text
standards/
  technical-condition/<standard-id>/<package-version>/
  maintenance/<standard-id>/<package-version>/
  rating-tree/<tree-code>/<package-version>/
```

每个包必须有 `manifest.json`，并通过 `entry_files` 显式列出参与摘要和加载的 JSON 文件。入口文件只能使用包内相对路径，不能引用其他规则包。

清单必填字段：

- `standard_family`：`technical_condition` 或 `maintenance`；
- `standard_id`、`standard_code`、`standard_name`、`official_edition`；
- `package_version`：本仓库结构化规则包版本，不等同于官方标准版本；
- `contract_version`：加载器接口版本，当前为 `1`；
- `algorithm_id`：C++ 适配器注册 ID；
- `effective_date`；
- `entry_files`；
- `content_checksum`。
- `status`：已发布包当前固定为 `active`，项目可用性由后续注册状态管理。

## 摘要规则

`content_checksum` 使用 `sha256:<hex>`。计算输入由 `manifest.json` 和排序后的 `entry_files` 组成：

1. JSON 先解析再按稳定对象键顺序紧凑序列化；
2. `manifest.json` 计算时删除 `content_checksum`，并排序 `entry_files`；
3. 文件按规范化相对文件名排序；
4. 文件名和内容使用长度前缀拼接后计算 SHA-256。

因此仅改变缩进、换行、对象键顺序或 `entry_files` 顺序不会改变摘要；数组顺序和数据值变化会改变摘要。

## 当前技术状况包

当前新建台账使用的最新技术状况包为 `technical-condition/jtg-t-h21-2011/1.0.4`。该版本从 1.0.3 派生，只把 `h21.component.lower.riverbed` 的 `generatable` 从 `false` 调整为 `true`，使河床能够作为可选的全桥级单一条目进入构件台账生成流程。评分权重、病害指标、扣分规则、标度和等级边界均未改变。

1.0.3 及更早版本保持不可变；既有项目继续按其锁定版本重现。`generatable` 只控制台账生成准入，不表示类别是否参与技术状况评定。

## 通用引用约定

入口文件可以声明：

```json
{
  "definitions": [
    {
      "id": "component.deck",
      "references": []
    }
  ]
}
```

同一包内所有 `definitions[].id` 必须唯一，`references` 必须指向同包定义。`OTHER-PACKAGE::rule.id`、`package://...` 和 `standard://...` 形式的跨包引用会被拒绝。

清单结构分别见 `schemas/technical-condition-package.schema.json` 和 `schemas/maintenance-package.schema.json`。

## 有效评定树扩展包

`rating-tree` 包只描述单位业务层级、显示名称、适用桥型与构件、受控别名和来源关系，不保存另一套扣分值、权重、公式或等级边界。参与评分的叶节点必须以 `inherit_h21` 或 `reference_h21` 明确引用 H21 稳定指标；运行时编译器从锁定的 H21 包取得标度文字与扣分表，并将 JTG 5120 检查养护来源合并为可追踪信息。

当前 `organization-bridge/1.0.1` 只包含桥梁分支。涵洞、隧道和涵洞 JTG 5120 分支未进入该版本；每个构件分支末尾的“其他病害（暂不计分）”是不可选择的 `placeholder`，不会参与评分。规则包和编译后的已发布树均只读，管理员也没有在线编辑接口。

`organization-bridge/1.0.3` 在 1.0.2 的树与来源之上新增 `matching-rules.json`，即病害分层确定性匹配的受控规则包：

- `aliases.json` 的受控别名与 `matching-rules.json` 的受控关键词都随版本不可变发布，页面运行时不可编辑；
- 每条关键词规则声明目标节点、适用桥型与构件、正向关键词、可选排除关键词和是否允许自动绑定（`auto_bind`）；
- 装载时校验目标节点存在且可选择、规则适用范围不超出目标节点自身范围、正向关键词非空、`rule_id` 唯一、规则不引用其他评定树版本，并拒绝同一适用范围内指向不同节点的冲突自动规则；
- `auto_bind` 为假的规则只能产生候选，必须由人工确认后才写入病害。

当前 `organization-bridge/2.0.3` 是来源软件桥梁评定树的结构快照，仅纳入第 5 至 10 节。它与 2.0.2 使用相同的树、来源映射和评分语义，区别是包版本升级并将 H21 来源锁定为 1.0.4，以配套河床生成准入发布：

- `tree.json` 保存来源分组、病害名称和显式 `display_number`，页面不再根据节点 ID 猜编号；
- `source-index-map.json` 以“来源分组 ID + 来源指标 ID”为首选精确映射，以“来源分组编号 + 来源指标编号”为兼容回退；病害文字、别名和关键词不参与自动匹配；
- 同一来源指标可以出现在不同分组中，显示编号保持来源软件原值。例如 `9.1.2` 下复用的指标仍显示 `9.1.1-1`，不会被改写为 `9.1.2-1`；
- 能与 H21 对应的节点只引用 H21 标度和扣分；没有评分依据的“其他病害”等节点使用 `non_scoring`，仍可选择但不扣分；
- `9.1.1-10 墩身水损害` 和 `6.2.1` 下的 `9.2.1-10 水损害` 使用来源软件的四级判定文字，并引用 H21 四级扣分曲线（`0/25/40/50`）；页面分别标明判定来源与扣分参照；
- 同级病害按完整显示编号自然升序排列。例如墩身病害按 `9.1.1-1` 至 `9.1.1-11` 显示，不沿用来源软件把新增指标置顶的内部顺序；
- 来源快照、人工修正、构件适用范围和评分例外保存在 `standards/source-material/datacheck-bridge-tree/`，生成包不得手工修改。

在 `tools-python` 目录执行以下命令可校验已提交的最新包与生成源完全一致：

```powershell
uv run python -m bridge_report_tools.rating_tree.package_generator --check
```

需要从新的离线数据库重新提取脱敏结构时，先生成快照，再审阅修正和映射文件，最后重新生成包：

```powershell
uv run python -m bridge_report_tools.rating_tree.source_snapshot --source-db <数据库文件> --output-dir ../standards/source-material/datacheck-bridge-tree
uv run python -m bridge_report_tools.rating_tree.package_generator
```

清单与节点字段见 `schemas/rating-tree-extension-package.schema.json`。其 `content_checksum` 与规范包使用完全相同的稳定 JSON 摘要约定。
