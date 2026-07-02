# 项目目录结构与技术架构梳理

本项目 `bridge-report-system` 是一个专为桥梁定期检测报告的归档、病害校对、历史对比及 Word 报告自动生成而设计的本地网页系统。目前项目已搭建好基础骨架（Skeleton），整体架构与目录结构非常清晰。

---

## 1. 整体技术框架与通信边界

项目的整体架构设计遵循**“单机运行、服务分工、边界清晰”**的原则，主要分为三层：

```text
React 前端 (127.0.0.1:5173)
      │
      ▼
C++ Drogon 主后端 (127.0.0.1:18080)
      │
      ├─► PostgreSQL 数据库 (127.0.0.1:5432) ── 结构化事实数据
      │
      ├─► archive/ (本地文件归档) ──────────── 二进制文件与报告存储
      │
      └─► Python FastAPI 工具服务 (127.0.0.1:18081) ── Word解析、图片抽取等 CPU/AI 密集任务
```

### 核心分工与通信规则
1. **React 前端**：主要面向用户提供可视化工作台，**仅直接调用 C++ 主后端**，不直接访问 Python 工具服务或 PostgreSQL 数据库。
2. **C++ Drogon 主后端**：
   - 业务逻辑与状态管理的中心。
   - **唯一**允许向 PostgreSQL 数据库写入数据的服务，确保数据一致性。
   - 负责管理本地文件归档目录（`archive/`）。
   - 协调、调用 Python 本地服务，并对其返回的解析数据进行校验和入库。
3. **Python FastAPI 工具服务**：
   - 定位为**本地工具服务**。
   - 负责处理 Word/Excel 解析、图片自动提取、AI 润色以及 Milvus 向量检索等底层处理。
   - **不直接读写 PostgreSQL 数据库**，解析到的结果以 JSON 形式通过 HTTP API 交付给 C++ 主服务。

---

## 2. 文件夹/目录结构详细梳理

以下是项目当前目录的详细梳理：

| 目录/文件名 | 核心职责/作用 | 当前状态与关键技术栈 |
| :--- | :--- | :--- |
| **`backend-cpp/`** | C++ 主后端服务目录 | **技术栈：Drogon, CMake, vcpkg**<br>• `CMakeLists.txt` / `CMakePresets.json`：定义构建和生成器配置（VS2022）。<br>• `vcpkg.json`：管理 C++ 第三方依赖。<br>• `src/main.cpp`：主程序入口，注册了 `/health` 与 `/health/tools` 路由。<br>• `include/` 与 `src/`：包含了系统配置加载（`AppConfig`）和路径确保（`RuntimePaths`）等模块。 |
| **`tools-python/`** | Python 本地工具服务目录 | **技术栈：FastAPI, uv**<br>• `pyproject.toml` / `uv.lock`：使用 modern Python 依赖工具 `uv` 管理依赖与虚拟环境。<br>• `bridge_report_tools/main.py`：主程序入口，提供了 `/health` 接口。<br>• `tests/`：预留的测试用例。 |
| **`frontend/`** | 本地网页前端目录 | **技术栈：React + TypeScript + Vite + Vanilla CSS**<br>• 基于 Vite 驱动的 React + TS 页面。<br>• `src/App.tsx`：目前是一个简单的状态面板，用于展示 C++ 主服务和 Python 工具服务的健康检查状态。<br>• `src/api/health.ts`：用于请求后端健康检查接口。 |
| **`database/`** | 数据库初始化与迁移目录 | **技术栈：PostgreSQL, SQL 迁移文件**<br>• `migrations/`：包含 SQL 迁移脚本，当前有 `001_skeleton_check.sql`，用于创建检测数据库连接状态的验证表。<br>• `seeds/`：用于存放初始或测试用种子数据。 |
| **`config/`** | 系统配置文件目录 | • `local.example.json`：存放本地环境配置样例（包括端口、数据库连接串、归档根目录等）。在实际部署中会复制为 `local.json`（不进版本控制）。 |
| **`archive/`** | 本地文件归档根目录 | • 预留的本地二进制文件仓库，后续所有的上传 Word 原件、提取的病害照片、生成的 Word 报告均会存放在该目录下。 |
| **`samples/`** | 样例资料目录 | • 用于存放开发与测试所用的桥梁正式 Word 报告、提取出的样例图片以及期望的中间 JSON 数据（如样例桥梁 `绕阳河二号桥` 的报告）。 |
| **`scripts/`** | 开发辅助脚本目录 | • `dev/`：包含了 PowerShell 脚本，如一键启动 C++ 服务（`start-cpp-backend.ps1`）、启动 Python 服务（`start-python-tools.ps1`）、启动前端（`start-frontend.ps1`）以及健康检查脚本（`check-health.ps1`）。 |
| **`docs/`** | 项目文档与规范设计目录 | • `superpowers/specs/` 下包含完整的架构设计文档（`2026-07-01-bridge-report-system-design.md`）和分模块评审指南（`01-tech-stack-and-project-skeleton.md`），为后续详细功能开发指引了方向。 |
| **`PROJECT_CONTEXT.md`** | 项目概况说明书 | • 快速回顾项目现状、已确认的设计细节、核心页面和下一步开发建议，是新开发窗口的首读文件。 |
| **`README.md`** | 基础运行指南 | • 简要描述本地服务的 IP、端口约定以及工程的骨架范围。 |

---

## 3. 当前已实现的内容 (Skeleton)

目前，项目已经完成了**第一阶段工程骨架的搭建与连通性验证**：
1. **C++ 后端**：已使用 Drogon 搭建好基础服务，默认监听 `127.0.0.1:18080`，并提供 `/health` (查看自身状态) 与 `/health/tools` (代理调用 Python 服务的健康状态)。
2. **Python 工具端**：已使用 FastAPI 搭建好工具服务，默认监听 `127.0.0.1:18081`，并提供 `/health`。
3. **前端**：已基于 React + Vite 搭建起基本页面，能够请求 C++ 后端的 `/health` 接口，并在界面上实时呈现两端服务的运行状态（OK/Degraded/Offline）。
4. **辅助脚本**：提供了开发环境下一键运行和连通性检测的 `.ps1` 脚本，可快速启动各端服务。

---

## 4. 下一步的核心开发方向

根据 `PROJECT_CONTEXT.md` 和设计 specs，在骨架连通之后，下一步将围绕以下方面展开：
1. 设计 PostgreSQL 的详细 Schema（即物理表结构，如桥梁、构件、病害观测、照片等）。
2. 在 `archive/` 目录下规划并建立具体的子目录结构（按桥梁、年份、导入任务等归类文件）。
3. 细化并定义 C++ 与 Python 之间的核心数据交换格式——即统一的年度检测中间模型 `BridgeAnnualInspectionData` (JSON Schema)。
4. 编写 Word 报告解析的原型代码，并尝试对 `绕阳河二号桥` 进行数据提取测试。
