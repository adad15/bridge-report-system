# 01 技术栈与项目骨架

日期：2026-07-02

## 1. 背景与目标

本模块是桥梁报告系统第一阶段的工程底座，只确认技术栈、目录组织、服务边界、本地运行方式、依赖管理和测试方式。

桥梁报告系统后续会包含 C++ 主服务、Python 工具服务、React 前端、PostgreSQL、文件归档、Word 解析、章节生成和 AI/Milvus 辅助能力。如果第一步不先定工程骨架，后续模块很容易在数据库写入边界、Python 职责、文件归档路径和本地启动方式上反复返工。

第一阶段本模块要定下这些结果：

1. 确认 C++ 作为主后端。
2. 确认 Python 作为本地工具服务。
3. 选定前端技术栈。
4. 确定数据库和文件归档位置。
5. 划清 C++、Python、前端、数据库之间的通信边界。
6. 确定本地开发目录结构。
7. 列出第一版暂不处理的工程能力。

## 2. 范围

本模块负责：

1. 技术栈选型。
2. 本地服务边界。
3. 项目目录结构。
4. 本地端口约定。
5. 依赖管理方式。
6. 数据库迁移文件位置。
7. 样例资料和测试资料位置。
8. 工程层面的测试策略。

本模块不负责：

1. PostgreSQL 详细表结构。
2. `BridgeAnnualInspectionData` 字段细节。
3. Word 报告解析规则。
4. 病害校对页面设计。
5. 历史病害对比算法。
6. 章节生成规则。
7. Word 正式报告装配细节。
8. AI 润色提示词和事实校验规则。
9. 多人权限、审签流和部署运维。

## 3. 上游依赖

本模块依赖以下已确认文档：

1. `docs/superpowers/specs/2026-07-01-bridge-report-system-design.md`
2. `docs/superpowers/specs/2026-07-01-modular-technical-doc-review-design.md`

上游已确认原则：

1. 新建独立项目，不放进 `auto_cad`。
2. 第一版是本地网页系统，先单机使用。
3. PostgreSQL 是结构化事实主库。
4. Word、图片、模板、附件和生成报告放在文件归档目录。
5. Python 和 AI 不能创造、修改或判断病害事实。
6. 今年数据导入必须通过 `Importers` 适配器抽象。
7. 所有自动抽取和生成内容保留来源、置信度和人工确认状态。

## 4. 下游影响

本模块会影响这些后续模块：

1. `02-postgresql-schema-and-file-archive`
   - 数据库迁移文件位置、C++ 主服务数据库写入边界、文件归档根目录。
2. `03-bridge-annual-inspection-data-contract`
   - C++ 与 Python 之间的 JSON 契约位置和命名方式。
3. `04-word-importer-prototype`
   - Python 工具服务接口、导入任务目录、输入输出路径。
4. `05-review-workspace`
   - 前端 API 调用方式、C++ 主服务端口、校对状态来源。
5. `09-docx-template-and-builder`
   - C++ 主流程和 Python/工具能力的职责边界。
6. `10-milvus-ai-polish-and-fact-check`
   - Python 工具服务是否承载 AI/Milvus 辅助能力。

本模块确认后，后续模块不能随意改变这些约定：

1. C++ 主服务是唯一的业务主后端。
2. C++ 主服务是 PostgreSQL 事实写入入口。
3. Python 服务是本地工具服务，不直接写事实库。
4. C++ 与 Python 通过本地 HTTP JSON API 通信。
5. 前端只直接调用 C++ 主服务。
6. PostgreSQL 不存 Word、图片和生成报告二进制，只存元数据和路径。

## 5. 核心设计

### 5.1 技术栈

| 层级 | 技术选择 | 说明 |
| --- | --- | --- |
| C++ 主后端 | Drogon | 提供本地 Web API、文件上传下载、任务接口和前端 API |
| C++ 构建 | CMake | 作为真实工程源，Visual Studio 读取 CMake 工程 |
| C++ 依赖管理 | vcpkg | 管理 Drogon、PostgreSQL 客户端、JSON、测试框架等依赖 |
| C++ IDE | Visual Studio 2022 | 作为主要开发和调试入口，后续兼容 VS2026 |
| Python 工具服务 | FastAPI | 提供 Word 解析、图片抽取、AI/Milvus 辅助等本地工具 API |
| Python 包管理 | uv + pyproject.toml | 管理 Python 工具服务依赖、虚拟环境和锁定文件 |
| 前端 | React + TypeScript + Vite | 实现本地网页工作台 |
| 数据库 | PostgreSQL | 结构化事实主库 |
| 数据库迁移 | SQL 文件 | 第一版使用明确 SQL 迁移文件，不引入 Python ORM 迁移 |
| 文件归档 | 本地 archive 目录 | 存 Word、图片、模板、附件和生成报告 |

### 5.2 服务边界

```text
React 前端
  -> C++ Drogon 主服务 127.0.0.1:18080
      -> PostgreSQL
      -> archive/
      -> Python FastAPI 工具服务 127.0.0.1:18081
```

前端边界：

1. 前端只调用 C++ 主服务。
2. 前端不直接调用 Python 工具服务。
3. 前端不直接访问 PostgreSQL。
4. 前端上传 Word、模板和附件时，先传给 C++ 主服务。

C++ 主服务边界：

1. 接收前端请求。
2. 管理桥梁、年度检测任务、导入任务、校对状态、对比状态、章节草稿和报告生成状态。
3. 读写 PostgreSQL。
4. 管理文件归档目录。
5. 调用 Python 工具服务。
6. 对 Python 返回结果做校验、状态转换和入库。

Python 工具服务边界：

1. 提供 Word 解析 API。
2. 提供图片抽取 API。
3. 提供后续 AI/Milvus 辅助 API。
4. 读取 C++ 指定的输入文件。
5. 写入 C++ 指定的导入任务目录。
6. 返回结构化 JSON、warning、error、置信度和来源引用。
7. 不直接写 PostgreSQL。
8. 不决定病害事实、评分、等级或对比关系。

### 5.3 本地端口

| 服务 | 地址 | 说明 |
| --- | --- | --- |
| C++ Drogon 主服务 | `127.0.0.1:18080` | 前端和用户主要访问的 API 服务 |
| Python FastAPI 工具服务 | `127.0.0.1:18081` | 只允许本机调用的工具服务 |
| React Vite 开发服务 | `127.0.0.1:5173` | 第一版前端开发服务 |
| PostgreSQL | `127.0.0.1:5432` | 本地事实主库 |

端口冲突时，可以通过本地配置文件覆盖，但默认文档和示例脚本使用以上端口。

### 5.4 推荐目录结构

```text
bridge-report-system/
  PROJECT_CONTEXT.md

  backend-cpp/
    CMakeLists.txt
    CMakePresets.json
    vcpkg.json
    src/
      main.cpp
      api/
      app/
      archive/
      database/
      import_jobs/
      logging/
      config/
    include/
      bridge_report/
    tests/

  tools-python/
    pyproject.toml
    bridge_report_tools/
      main.py
      api/
      importers/
      docx/
      schemas/
      ai/
      milvus/
      config/
    tests/

  frontend/
    package.json
    vite.config.ts
    src/
      app/
      pages/
      components/
      api/
      state/
      styles/

  database/
    migrations/
      001_init_core_tables.sql
    seeds/

  archive/
    .gitkeep

  samples/
    documents/
    expected-json/
    extracted-images/

  scripts/
    dev/
    db/
    tools/

  docs/
    superpowers/
      specs/
      plans/
```

### 5.5 目录职责

`backend-cpp/` 是 C++ 主后端。它负责业务状态、数据库、文件归档、导入任务编排和对外 API。

`tools-python/` 是 Python 本地工具服务。它负责 Word 解析、图片抽取和后续 AI/Milvus 辅助能力。

`frontend/` 是本地网页前端。它只调用 C++ 主服务。

`database/` 保存数据库迁移 SQL 和 seed 数据。

`archive/` 是本地文件归档根目录。第一版可直接放在项目根目录，后续允许通过配置迁移到其他磁盘目录。

`samples/` 保存样例 Word、期望 JSON、抽取出的图片样例和验收资料。

`scripts/` 保存开发辅助脚本，包括启动服务、初始化数据库和运行工具。

`docs/` 保存设计文档、模块技术文档和实施计划。

### 5.6 C++ 与 Python API 契约

第一版中，C++ 通过 HTTP JSON 调用 Python。

Python 工具服务最小 API：

```text
GET /health
POST /v1/import/software-word
POST /v1/import/formal-word
```

`POST /v1/import/software-word` 输入示例：

```json
{
  "job_id": "import_20260702_001",
  "source_file_path": "archive/bridges/raoyanghe-2/2026/sources/software-report.docx",
  "work_dir": "archive/bridges/raoyanghe-2/2026/import-jobs/import_20260702_001",
  "bridge_hint": {
    "bridge_name": "绕阳河二号桥",
    "inspection_year": 2026
  }
}
```

Python 返回示例：

```json
{
  "job_id": "import_20260702_001",
  "status": "succeeded",
  "annual_data_path": "archive/bridges/raoyanghe-2/2026/import-jobs/import_20260702_001/annual-data.json",
  "warnings": [
    {
      "code": "low_confidence_bridge_name",
      "message": "桥梁名称来自文件名推断，需要人工确认。"
    }
  ],
  "errors": []
}
```

完整的 `BridgeAnnualInspectionData` 字段由第三个模块定义。本模块只规定 C++ 与 Python 使用 JSON 文件和 JSON API 交接。

### 5.7 配置原则

第一版用本地配置文件管理端口、数据库连接和归档目录。

建议配置文件：

```text
config/local.example.json
config/local.json
```

`local.example.json` 进入版本管理，`local.json` 保存本机配置并忽略。

配置项至少包括：

1. C++ 主服务端口。
2. Python 工具服务地址。
3. PostgreSQL 连接信息。
4. 文件归档根目录。
5. 日志目录。
6. 样例资料目录。

## 6. 错误处理与人工校对点

### 6.1 C++ 主服务错误处理

C++ 主服务必须处理：

1. Python 工具服务不可用。
2. Python 工具服务超时。
3. Python 返回非 JSON。
4. Python 返回 `status = failed`。
5. Python 返回的 `annual_data_path` 不存在。
6. Python 返回的 JSON 契约版本不匹配。
7. 文件归档目录不可写。
8. PostgreSQL 不可用。

这些错误必须写入导入任务状态，不能静默失败。

### 6.2 Python 工具服务错误处理

Python 工具服务必须返回结构化错误：

```json
{
  "job_id": "import_20260702_001",
  "status": "failed",
  "annual_data_path": null,
  "warnings": [],
  "errors": [
    {
      "code": "docx_open_failed",
      "message": "无法打开 Word 文件。"
    }
  ]
}
```

### 6.3 人工校对点

以下内容即使自动抽取成功，也必须进入人工校对：

1. 桥梁名称和别名。
2. 检测年份和检测日期。
3. 病害记录。
4. 病害尺寸。
5. 病害照片和照片标题。
6. 技术状况评定。
7. 从正式报告解析出的上一年病害。
8. 自动生成的历史对比候选关系。
9. 章节草稿。

## 7. 测试与验收标准

### 7.1 工程骨架验收

第一模块实施后，应能验证这些内容：

1. Visual Studio 2022 能打开 CMake 工程。
2. C++ 主服务能启动并提供 `/health`。
3. Python 工具服务能启动并提供 `/health`。
4. 前端开发服务能启动并调用 C++ `/health`。
5. C++ 能调用 Python `/health`。
6. 本地配置文件能覆盖默认端口和归档目录。

### 7.2 文档验收

本模块技术文档通过标准：

1. 技术栈明确。
2. 项目目录明确。
3. C++、Python、前端、PostgreSQL、archive 的职责边界明确。
4. C++ 与 Python 通信方式明确。
5. Python 不直接写事实库的边界明确。
6. 第一阶段不做内容明确。
7. 后续模块能引用本模块的目录和服务边界。

### 7.3 后续实施计划验收

进入实施计划后，第一批代码只需要证明工程骨架可运行。它不需要实现完整业务流程。

实施计划的最小可运行结果是：

1. C++ `/health` 返回成功。
2. Python `/health` 返回成功。
3. C++ 能调用 Python `/health` 并返回聚合健康状态。
4. 前端页面能显示 C++ 和 Python 服务状态。
5. `archive/`、`database/migrations/`、`samples/` 目录存在。

## 8. 风险与取舍

### 8.1 C++ 主后端复杂度

C++ 做 Web 后端比 Python 或 Node 更重，开发速度会慢一些。但用户希望主后端使用 C++；项目长期也会涉及本地文件、Word 装配、规则处理和 Windows 开发习惯，所以 C++ 主服务可以接受。

取舍：C++ 只承担主服务和事实管理，Word 解析、AI/Milvus 等更适合 Python 的工作放到 Python 工具服务。

### 8.2 两个本地后端服务

C++ 和 Python 同时作为本地服务，会增加启动和健康检查的复杂度。

取舍：第一版通过固定端口、`/health`、启动脚本和清晰错误状态控制复杂度。长期收益是 Python 工具能力更容易扩展。

### 8.3 Python 依赖管理

Python 工具服务使用 `uv` 管理虚拟环境和依赖。`pyproject.toml` 作为依赖声明，`uv.lock` 作为锁定文件。

取舍：`uv` 比手工 `python -m venv` 加 `pip install` 更快、更稳定，也更适合后续把 Word 解析、AI/Milvus 依赖固定下来。

### 8.4 Drogon 学习成本

Drogon 对传统 Visual Studio 工程习惯有学习成本，尤其是 CMake、vcpkg 和异步 API。

取舍：相比手写 HTTP 服务或使用过轻框架，Drogon 更适合本项目的本地 API、文件上传下载和后续扩展。

### 8.5 数据库迁移不用 ORM

第一版不使用 SQLAlchemy/Alembic，因为主后端是 C++。数据库迁移先采用 SQL 文件。

取舍：SQL 文件直接、透明，适合早期明确 PostgreSQL 表结构。后续如果迁移数量增长，可以再引入专门迁移工具。

### 8.6 前端不直接调 Python

前端如果直接调 Python，短期更方便测试解析功能，但会破坏 C++ 主服务作为业务编排中心的边界。

取舍：前端只调 C++，所有导入任务都由 C++ 创建、记录和编排。

## 9. Codex 技术把关意见

本模块可以进入用户评审。

把关结论：

1. 技术栈与用户偏好一致：C++ 主后端、Visual Studio 2022、CMake、vcpkg。
2. Drogon 适合承担本地 API 主服务，避免手写 HTTP 基础设施。
3. Python 工具服务采用 FastAPI 比命令行工具更适合中长期扩展。
4. C++ 作为唯一事实写入入口，符合 PostgreSQL 是事实主库的原则。
5. 前端只调用 C++，服务边界清晰。
6. 本模块没有提前设计数据库细表、Word 解析规则或页面细节，范围控制合理。

用户需要重点确认：

1. 是否接受 Drogon 作为 C++ Web/API 框架。
2. 是否接受 Python FastAPI 作为本地工具服务。
3. 是否接受 C++ 与 Python 固定端口、本地 HTTP JSON 通信。
4. 是否接受第一版数据库迁移使用 SQL 文件。
5. 是否接受推荐目录结构作为后续模块引用基础。

## 10. 变更记录

| 日期 | 变更 | 原因 | 影响模块 |
| --- | --- | --- | --- |
| 2026-07-02 | 创建第一版技术栈与项目骨架文档 | 用户确认采用 C++ 主后端、CMake + vcpkg + Visual Studio 2022、Drogon、Python 本地 HTTP 工具服务 | 后续所有模块 |
| 2026-07-02 | Python 依赖管理改为 uv + pyproject.toml | 用户确认本机已有 uv，希望 Python 工具服务使用 uv 管理环境 | `01-tech-stack-and-project-skeleton`、后续 Python 工具服务 |


