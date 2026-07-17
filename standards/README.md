# 版本化桥梁规范包

本目录只保存可执行规范数据和其来源索引。评分算法由 C++ 后端适配器实现；前端和 Python 不直接加载规则包，也不各自实现评分公式。

## 目录约定

```text
standards/
  technical-condition/<standard-id>/<package-version>/
  maintenance/<standard-id>/<package-version>/
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
