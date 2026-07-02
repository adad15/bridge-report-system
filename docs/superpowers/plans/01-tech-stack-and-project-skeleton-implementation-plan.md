# Tech Stack and Project Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first runnable project skeleton for the bridge report system: C++ Drogon main service, Python FastAPI tool service, React/Vite frontend health page, shared local configuration, standard directories, and verification scripts.

**Architecture:** The React frontend calls only the C++ Drogon main service on `127.0.0.1:18080`. The C++ service owns orchestration, archive paths, future PostgreSQL writes, and calls the Python FastAPI tool service on `127.0.0.1:18081`. This plan creates health endpoints and directory/config foundations only; it does not implement database schema, Word parsing, import jobs, or report generation.

**Tech Stack:** C++20, Drogon, CMake, vcpkg, Visual Studio 2022, Python 3.11+, uv, FastAPI, pytest, React, TypeScript, Vite, Vitest, PowerShell.

---

## File Structure

Create this structure:

```text
bridge-report-system/
  .gitignore
  README.md
  config/
    local.example.json
  backend-cpp/
    CMakeLists.txt
    CMakePresets.json
    vcpkg.json
    include/
      bridge_report/
        config/
          AppConfig.hpp
        runtime/
          RuntimePaths.hpp
    src/
      config/
        AppConfig.cpp
      runtime/
        RuntimePaths.cpp
      main.cpp
    tests/
      test_app_config.cpp
  tools-python/
    pyproject.toml
    uv.lock
    bridge_report_tools/
      __init__.py
      config.py
      main.py
    tests/
      test_health.py
  frontend/
    package.json
    index.html
    tsconfig.json
    tsconfig.node.json
    vite.config.ts
    src/
      vite-env.d.ts
      App.tsx
      main.tsx
      styles.css
      api/
        health.ts
        health.test.ts
  database/
    migrations/
      001_skeleton_check.sql
    seeds/
      .gitkeep
  archive/
    .gitkeep
  samples/
    documents/
      .gitkeep
    expected-json/
      .gitkeep
    extracted-images/
      .gitkeep
  scripts/
    dev/
      check-layout.ps1
      check-health.ps1
      start-cpp-backend.ps1
      start-frontend.ps1
      start-python-tools.ps1
```

Responsibility boundaries:

- `backend-cpp/`: C++ main service. Owns public API, future PostgreSQL writes, archive orchestration, and calls Python.
- `tools-python/`: Python local tool service. Owns health endpoint now; later Word parsing, image extraction, AI/Milvus helpers.
- `frontend/`: Browser UI. Calls C++ service only.
- `database/`: SQL migration files and seeds.
- `archive/`: local binary archive root.
- `samples/`: sample Word files and expected parser outputs.
- `scripts/dev/`: local development checks and start scripts.

## Task 1: Repository Baseline, Directories, and Shared Config

**Files:**

- Create: `.gitignore`
- Create: `README.md`
- Create: `config/local.example.json`
- Create: `scripts/dev/check-layout.ps1`
- Create: `database/migrations/001_skeleton_check.sql`
- Create: `archive/.gitkeep`
- Create: `database/seeds/.gitkeep`
- Create: `samples/documents/.gitkeep`
- Create: `samples/expected-json/.gitkeep`
- Create: `samples/extracted-images/.gitkeep`

- [ ] **Step 1: Initialize Git if the workspace is not already a repository**

Run:

```powershell
git rev-parse --show-toplevel
```

Expected if the repository is not initialized:

```text
fatal: not a git repository (or any of the parent directories): .git
```

Then run:

```powershell
git init
```

Expected:

```text
Initialized empty Git repository in D:/vs2022 code/bridge-report-system/.git/
```

- [ ] **Step 2: Write the failing layout check**

Create `scripts/dev/check-layout.ps1`:

```powershell
$ErrorActionPreference = "Stop"

$requiredPaths = @(
  ".gitignore",
  "README.md",
  "config/local.example.json",
  "backend-cpp",
  "tools-python",
  "frontend",
  "database/migrations",
  "archive",
  "samples/documents",
  "samples/expected-json",
  "samples/extracted-images",
  "scripts/dev"
)

$missing = @()
foreach ($path in $requiredPaths) {
  if (-not (Test-Path $path)) {
    $missing += $path
  }
}

if ($missing.Count -gt 0) {
  Write-Host "Missing required project paths:"
  foreach ($path in $missing) {
    Write-Host " - $path"
  }
  exit 1
}

Write-Host "Project layout check passed."
```

- [ ] **Step 3: Run the layout check to verify it fails**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-layout.ps1
```

Expected: FAIL with missing paths such as `.gitignore`, `README.md`, `backend-cpp`, `tools-python`, and `frontend`.

- [ ] **Step 4: Create the baseline directories and files**

Run:

```powershell
New-Item -ItemType Directory -Force config
New-Item -ItemType Directory -Force backend-cpp
New-Item -ItemType Directory -Force tools-python
New-Item -ItemType Directory -Force frontend
New-Item -ItemType Directory -Force database/migrations
New-Item -ItemType Directory -Force database/seeds
New-Item -ItemType Directory -Force archive
New-Item -ItemType Directory -Force samples/documents
New-Item -ItemType Directory -Force samples/expected-json
New-Item -ItemType Directory -Force samples/extracted-images
```

Create `.gitignore`:

```gitignore
.vs/
.vscode/
build/
out/
CMakeUserPresets.json

backend-cpp/build/
backend-cpp/out/

tools-python/.venv/
tools-python/__pycache__/
tools-python/.pytest_cache/
tools-python/*.egg-info/

frontend/node_modules/
frontend/dist/
frontend/.vite/
frontend/vite.config.js
frontend/vite.config.d.ts
*.tsbuildinfo

config/local.json
logs/
tmp/

archive/*
!archive/.gitkeep

*.user
*.suo
*.obj
*.pdb
*.ilk
*.exe
```

Create `README.md`:

```markdown
# Bridge Report System

Local web system for bridge inspection report archiving, defect review, historical comparison, and formal Word report generation.

## First Module Scope

This skeleton proves the local service shape:

- C++ Drogon main service on `127.0.0.1:18080`
- Python FastAPI tool service on `127.0.0.1:18081`
- React/Vite frontend on `127.0.0.1:5173`
- PostgreSQL reserved as the future fact database
- `archive/` reserved as the local binary file archive

## Development Order

Read these documents first:

- `PROJECT_CONTEXT.md`
- `docs/superpowers/specs/2026-07-01-bridge-report-system-design.md`
- `docs/superpowers/specs/2026-07-01-modular-technical-doc-review-design.md`
- `docs/superpowers/specs/modules/01-tech-stack-and-project-skeleton.md`
```

Create `config/local.example.json`:

```json
{
  "cpp_server": {
    "host": "127.0.0.1",
    "port": 18080
  },
  "python_tools": {
    "base_url": "http://127.0.0.1:18081",
    "host": "127.0.0.1",
    "port": 18081
  },
  "frontend": {
    "host": "127.0.0.1",
    "port": 5173,
    "backend_base_url": "http://127.0.0.1:18080"
  },
  "postgres": {
    "host": "127.0.0.1",
    "port": 5432,
    "database": "bridge_report_system",
    "user": "bridge_report",
    "password": "bridge_report_dev"
  },
  "archive": {
    "root": "archive"
  },
  "logs": {
    "root": "logs"
  },
  "samples": {
    "root": "samples"
  }
}
```

Create `database/migrations/001_skeleton_check.sql`:

```sql
-- Skeleton migration used to verify the migration folder exists.
-- The real PostgreSQL schema is designed in module 02.
select 1;
```

Create the keep files:

```powershell
New-Item -ItemType File -Force archive/.gitkeep
New-Item -ItemType File -Force database/seeds/.gitkeep
New-Item -ItemType File -Force samples/documents/.gitkeep
New-Item -ItemType File -Force samples/expected-json/.gitkeep
New-Item -ItemType File -Force samples/extracted-images/.gitkeep
```

- [ ] **Step 5: Run the layout check to verify it passes**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-layout.ps1
```

Expected:

```text
Project layout check passed.
```

- [ ] **Step 6: Commit**

Run:

```powershell
git add .gitignore README.md config/local.example.json database/migrations/001_skeleton_check.sql archive/.gitkeep database/seeds/.gitkeep samples/documents/.gitkeep samples/expected-json/.gitkeep samples/extracted-images/.gitkeep scripts/dev/check-layout.ps1
git commit -m "chore: add project skeleton layout"
```

Expected: commit succeeds.

## Task 2: Python FastAPI Tool Service Health Endpoint

**Files:**

- Create: `tools-python/pyproject.toml`
- Create: `tools-python/uv.lock`
- Create: `tools-python/bridge_report_tools/__init__.py`
- Create: `tools-python/bridge_report_tools/config.py`
- Create: `tools-python/bridge_report_tools/main.py`
- Create: `tools-python/tests/test_health.py`

- [ ] **Step 1: Write the failing Python health test**

Create directories:

```powershell
New-Item -ItemType Directory -Force tools-python/bridge_report_tools
New-Item -ItemType Directory -Force tools-python/tests
```

Create `tools-python/tests/test_health.py`:

```python
from fastapi.testclient import TestClient

from bridge_report_tools.main import app


def test_health_returns_tool_service_identity() -> None:
    client = TestClient(app)

    response = client.get("/health")

    assert response.status_code == 200
    assert response.json() == {
        "status": "ok",
        "service": "bridge-report-python-tools",
        "version": "0.1.0",
        "host": "127.0.0.1",
        "port": 18081,
    }
```

- [ ] **Step 2: Run the Python test to verify it fails**

Run:

```powershell
cd tools-python
python -m pytest tests/test_health.py -v
```

Expected: FAIL with `ModuleNotFoundError: No module named 'bridge_report_tools'` or `ModuleNotFoundError: No module named 'fastapi'`.

- [ ] **Step 3: Add the Python package and health implementation**

Create `tools-python/pyproject.toml`:

```toml
[project]
name = "bridge-report-tools"
version = "0.1.0"
description = "Local Python tool service for the bridge report system."
requires-python = ">=3.11"
dependencies = [
  "fastapi>=0.115.0",
  "uvicorn[standard]>=0.30.0"
]

[project.optional-dependencies]
dev = [
  "pytest>=8.0.0",
  "httpx>=0.27.0"
]

[tool.pytest.ini_options]
pythonpath = ["."]
testpaths = ["tests"]
```

Create `tools-python/bridge_report_tools/__init__.py`:

```python
__all__ = ["__version__"]

__version__ = "0.1.0"
```

Create `tools-python/bridge_report_tools/config.py`:

```python
from dataclasses import dataclass
import os


@dataclass(frozen=True)
class ToolSettings:
    host: str = "127.0.0.1"
    port: int = 18081


def get_settings() -> ToolSettings:
    host = os.getenv("BRIDGE_TOOLS_HOST", "127.0.0.1")
    port_text = os.getenv("BRIDGE_TOOLS_PORT", "18081")
    return ToolSettings(host=host, port=int(port_text))
```

Create `tools-python/bridge_report_tools/main.py`:

```python
from fastapi import FastAPI
from pydantic import BaseModel

from bridge_report_tools import __version__
from bridge_report_tools.config import get_settings


class HealthResponse(BaseModel):
    status: str
    service: str
    version: str
    host: str
    port: int


app = FastAPI(
    title="Bridge Report Python Tools",
    version=__version__,
)


@app.get("/health", response_model=HealthResponse)
def health() -> HealthResponse:
    settings = get_settings()
    return HealthResponse(
        status="ok",
        service="bridge-report-python-tools",
        version=__version__,
        host=settings.host,
        port=settings.port,
    )
```

- [ ] **Step 4: Install Python dependencies with uv**

Run:

```powershell
cd tools-python
uv sync --extra dev
```

Expected: uv creates `.venv`, resolves dependencies, writes `uv.lock`, and installs FastAPI, Uvicorn, pytest, and httpx.

- [ ] **Step 5: Run the Python test to verify it passes**

Run:

```powershell
cd tools-python
uv run pytest tests/test_health.py -v
```

Expected:

```text
tests/test_health.py::test_health_returns_tool_service_identity PASSED
```

- [ ] **Step 6: Run the Python service manually**

Run:

```powershell
cd tools-python
uv run uvicorn bridge_report_tools.main:app --host 127.0.0.1 --port 18081
```

In another PowerShell window, run:

```powershell
Invoke-RestMethod http://127.0.0.1:18081/health
```

Expected JSON:

```json
{
  "status": "ok",
  "service": "bridge-report-python-tools",
  "version": "0.1.0",
  "host": "127.0.0.1",
  "port": 18081
}
```

- [ ] **Step 7: Commit**

Run:

```powershell
git add tools-python/pyproject.toml tools-python/uv.lock tools-python/bridge_report_tools/__init__.py tools-python/bridge_report_tools/config.py tools-python/bridge_report_tools/main.py tools-python/tests/test_health.py
git commit -m "feat: add python tool service health endpoint"
```

Expected: commit succeeds.

## Task 3: C++ Drogon Main Service Health Endpoints

**Files:**

- Create: `backend-cpp/vcpkg.json`
- Create: `backend-cpp/CMakeLists.txt`
- Create: `backend-cpp/CMakePresets.json`
- Create: `backend-cpp/include/bridge_report/config/AppConfig.hpp`
- Create: `backend-cpp/include/bridge_report/runtime/RuntimePaths.hpp`
- Create: `backend-cpp/src/config/AppConfig.cpp`
- Create: `backend-cpp/src/runtime/RuntimePaths.cpp`
- Create: `backend-cpp/src/main.cpp`
- Create: `backend-cpp/tests/test_app_config.cpp`

- [ ] **Step 1: Write the failing C++ config test**

Create directories:

```powershell
New-Item -ItemType Directory -Force backend-cpp/include/bridge_report/config
New-Item -ItemType Directory -Force backend-cpp/include/bridge_report/runtime
New-Item -ItemType Directory -Force backend-cpp/src/config
New-Item -ItemType Directory -Force backend-cpp/src/runtime
New-Item -ItemType Directory -Force backend-cpp/tests
```

Create `backend-cpp/tests/test_app_config.cpp`:

```cpp
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/runtime/RuntimePaths.hpp"

namespace {

std::filesystem::path write_config_file() {
    const auto path = std::filesystem::temp_directory_path() / "bridge_report_test_config.json";
    std::ofstream out(path);
    out << R"json({
  "cpp_server": {
    "host": "127.0.0.1",
    "port": 19080
  },
  "python_tools": {
    "base_url": "http://127.0.0.1:19081"
  },
  "archive": {
    "root": "test-archive"
  }
})json";
    return path;
}

}  // namespace

TEST(AppConfigTest, LoadsConfiguredPortsAndArchiveRoot) {
    const auto path = write_config_file();

    const auto config = bridge_report::config::load_app_config(path);

    EXPECT_EQ(config.host, "127.0.0.1");
    EXPECT_EQ(config.port, 19080);
    EXPECT_EQ(config.python_tools_base_url, "http://127.0.0.1:19081");
    EXPECT_EQ(config.archive_root.generic_string(), "test-archive");
}

TEST(AppConfigTest, UsesDefaultsWhenConfigFileDoesNotExist) {
    const auto config = bridge_report::config::load_app_config("missing-local-config.json");

    EXPECT_EQ(config.host, "127.0.0.1");
    EXPECT_EQ(config.port, 18080);
    EXPECT_EQ(config.python_tools_base_url, "http://127.0.0.1:18081");
    EXPECT_EQ(config.archive_root.generic_string(), "archive");
}

TEST(RuntimePathsTest, CreatesMissingLogDirectory) {
    const auto log_path = std::filesystem::temp_directory_path() / "bridge_report_test_logs";
    std::filesystem::remove_all(log_path);

    bridge_report::runtime::ensure_log_directory(log_path);

    EXPECT_TRUE(std::filesystem::is_directory(log_path));

    std::filesystem::remove_all(log_path);
}
```

- [ ] **Step 2: Run CMake configure to verify it fails**

Run:

```powershell
cmake -S backend-cpp -B backend-cpp/build
```

Expected: FAIL because `backend-cpp/CMakeLists.txt` does not exist yet.

- [ ] **Step 3: Add C++ dependencies and build configuration**

Create `backend-cpp/vcpkg.json`:

```json
{
  "name": "bridge-report-backend",
  "version-string": "0.1.0",
  "dependencies": [
    "drogon",
    "gtest"
  ]
}
```

Create `backend-cpp/CMakePresets.json`:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "vs2022-x64-debug",
      "displayName": "Visual Studio 2022 x64 Debug",
      "generator": "Visual Studio 17 2022",
      "architecture": "x64",
      "binaryDir": "${sourceDir}/build/vs-debug",
      "cacheVariables": {
        "CMAKE_CXX_STANDARD": "20",
        "CMAKE_CXX_STANDARD_REQUIRED": "ON",
        "CMAKE_TOOLCHAIN_FILE": "D:/vcpkg/scripts/buildsystems/vcpkg.cmake"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "vs2022-x64-debug",
      "configurePreset": "vs2022-x64-debug",
      "configuration": "Debug"
    }
  ],
  "testPresets": [
    {
      "name": "vs2022-x64-debug",
      "configurePreset": "vs2022-x64-debug",
      "configuration": "Debug",
      "output": {
        "outputOnFailure": true
      }
    }
  ]
}
```

Create `backend-cpp/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.25)

project(bridge_report_backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

add_library(bridge_report_backend_core
    src/config/AppConfig.cpp
    src/runtime/RuntimePaths.cpp
)

target_include_directories(bridge_report_backend_core
    PUBLIC
        include
)

target_link_libraries(bridge_report_backend_core
    PUBLIC
        Drogon::Drogon
)

add_executable(bridge-report-backend
    src/main.cpp
)

target_link_libraries(bridge-report-backend
    PRIVATE
        bridge_report_backend_core
        Drogon::Drogon
)

enable_testing()

add_executable(bridge_report_backend_tests
    tests/test_app_config.cpp
)

target_link_libraries(bridge_report_backend_tests
    PRIVATE
        bridge_report_backend_core
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(bridge_report_backend_tests)
```

- [ ] **Step 4: Add C++ config implementation and health service**

Create `backend-cpp/include/bridge_report/config/AppConfig.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <string>

namespace bridge_report::config {

struct AppConfig {
    std::string host{"127.0.0.1"};
    int port{18080};
    std::string python_tools_base_url{"http://127.0.0.1:18081"};
    std::filesystem::path archive_root{"archive"};
};

AppConfig load_app_config(const std::filesystem::path& path);

}  // namespace bridge_report::config
```

Create `backend-cpp/src/config/AppConfig.cpp`:

```cpp
#include "bridge_report/config/AppConfig.hpp"

#include <fstream>

#include <json/json.h>

namespace bridge_report::config {

namespace {

std::string get_string_or_default(const Json::Value& object, const char* key, std::string fallback) {
    if (!object.isObject() || !object.isMember(key) || !object[key].isString()) {
        return fallback;
    }
    return object[key].asString();
}

int get_int_or_default(const Json::Value& object, const char* key, int fallback) {
    if (!object.isObject() || !object.isMember(key) || !object[key].isInt()) {
        return fallback;
    }
    return object[key].asInt();
}

}  // namespace

AppConfig load_app_config(const std::filesystem::path& path) {
    AppConfig config;

    std::ifstream input(path);
    if (!input.good()) {
        return config;
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &root, &errors)) {
        return config;
    }

    const auto& cpp_server = root["cpp_server"];
    config.host = get_string_or_default(cpp_server, "host", config.host);
    config.port = get_int_or_default(cpp_server, "port", config.port);

    const auto& python_tools = root["python_tools"];
    config.python_tools_base_url = get_string_or_default(
        python_tools,
        "base_url",
        config.python_tools_base_url
    );

    const auto& archive = root["archive"];
    config.archive_root = get_string_or_default(
        archive,
        "root",
        config.archive_root.generic_string()
    );

    return config;
}

}  // namespace bridge_report::config
```

Create `backend-cpp/include/bridge_report/runtime/RuntimePaths.hpp`:

```cpp
#pragma once

#include <filesystem>

namespace bridge_report::runtime {

void ensure_log_directory(const std::filesystem::path& log_path);

}  // namespace bridge_report::runtime
```

Create `backend-cpp/src/runtime/RuntimePaths.cpp`:

```cpp
#include "bridge_report/runtime/RuntimePaths.hpp"

namespace bridge_report::runtime {

void ensure_log_directory(const std::filesystem::path& log_path) {
    std::filesystem::create_directories(log_path);
}

}  // namespace bridge_report::runtime
```

Create `backend-cpp/src/main.cpp`:

```cpp
#include <functional>
#include <iostream>
#include <string>

#include <drogon/drogon.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/runtime/RuntimePaths.hpp"

namespace {

Json::Value make_cpp_health_body(const bridge_report::config::AppConfig& config) {
    Json::Value body;
    body["status"] = "ok";
    body["service"] = "bridge-report-cpp-backend";
    body["version"] = "0.1.0";
    body["host"] = config.host;
    body["port"] = config.port;
    body["python_tools_base_url"] = config.python_tools_base_url;
    body["archive_root"] = config.archive_root.generic_string();
    return body;
}

void register_health_routes(const bridge_report::config::AppConfig& config) {
    drogon::app().registerHandler(
        "/health",
        [config](const drogon::HttpRequestPtr&,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto response = drogon::HttpResponse::newHttpJsonResponse(make_cpp_health_body(config));
            callback(response);
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/health/tools",
        [base_url = config.python_tools_base_url](
            const drogon::HttpRequestPtr&,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback
        ) {
            auto client = drogon::HttpClient::newHttpClient(base_url);
            auto request = drogon::HttpRequest::newHttpRequest();
            request->setMethod(drogon::Get);
            request->setPath("/health");

            client->sendRequest(
                request,
                [callback = std::move(callback)](
                    drogon::ReqResult result,
                    const drogon::HttpResponsePtr& tools_response
                ) mutable {
                    Json::Value body;
                    body["service"] = "bridge-report-cpp-backend";
                    body["checked_service"] = "bridge-report-python-tools";

                    if (result != drogon::ReqResult::Ok || tools_response == nullptr) {
                        body["status"] = "degraded";
                        body["tools_status"] = "unavailable";
                        auto response = drogon::HttpResponse::newHttpJsonResponse(body);
                        response->setStatusCode(drogon::k503ServiceUnavailable);
                        callback(response);
                        return;
                    }

                    body["status"] = "ok";
                    body["tools_status"] = "reachable";
                    body["tools_http_status"] = static_cast<int>(tools_response->statusCode());
                    body["tools_response_raw"] = std::string(tools_response->body());
                    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
                    callback(response);
                }
            );
        },
        {drogon::Get}
    );
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string config_path = argc > 1 ? argv[1] : "config/local.json";
    const auto config = bridge_report::config::load_app_config(config_path);

    register_health_routes(config);

    std::cout << "Bridge Report C++ backend listening on "
              << config.host << ":" << config.port << "\n";

    const std::string log_path = "logs";
    bridge_report::runtime::ensure_log_directory(log_path);

    drogon::app()
        .addListener(config.host, config.port)
        .setLogPath(log_path)
        .setLogLevel(trantor::Logger::kInfo)
        .run();

    return 0;
}
```

- [ ] **Step 5: Configure and build C++ with vcpkg**

Run:

```powershell
cd backend-cpp
cmake --preset vs2022-x64-debug
cmake --build --preset vs2022-x64-debug
```

Expected: CMake configures, vcpkg installs Drogon and GTest if needed, and `bridge-report-backend` builds.

- [ ] **Step 6: Run C++ tests to verify they pass**

Run:

```powershell
cd backend-cpp
ctest --preset vs2022-x64-debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 7: Run the C++ service manually**

Run:

```powershell
cd backend-cpp
.\build\vs-debug\Debug\bridge-report-backend.exe ..\config\local.example.json
```

In another PowerShell window, run:

```powershell
Invoke-RestMethod http://127.0.0.1:18080/health
```

Expected JSON contains:

```json
{
  "status": "ok",
  "service": "bridge-report-cpp-backend",
  "version": "0.1.0",
  "host": "127.0.0.1",
  "port": 18080,
  "python_tools_base_url": "http://127.0.0.1:18081",
  "archive_root": "archive"
}
```

- [ ] **Step 8: Verify C++ can call Python health**

Start Python service from Task 2. Keep C++ service running.

Run:

```powershell
Invoke-RestMethod http://127.0.0.1:18080/health/tools
```

Expected JSON contains:

```json
{
  "status": "ok",
  "service": "bridge-report-cpp-backend",
  "checked_service": "bridge-report-python-tools",
  "tools_status": "reachable"
}
```

- [ ] **Step 9: Commit**

Run:

```powershell
git add backend-cpp/vcpkg.json backend-cpp/CMakeLists.txt backend-cpp/CMakePresets.json backend-cpp/include/bridge_report/config/AppConfig.hpp backend-cpp/include/bridge_report/runtime/RuntimePaths.hpp backend-cpp/src/config/AppConfig.cpp backend-cpp/src/runtime/RuntimePaths.cpp backend-cpp/src/main.cpp backend-cpp/tests/test_app_config.cpp
git commit -m "feat: add cpp backend health service"
```

Expected: commit succeeds.

## Task 4: React Frontend Health Dashboard

**Files:**

- Create: `frontend/package.json`
- Create: `frontend/index.html`
- Create: `frontend/tsconfig.json`
- Create: `frontend/tsconfig.node.json`
- Create: `frontend/vite.config.ts`
- Create: `frontend/src/api/health.ts`
- Create: `frontend/src/api/health.test.ts`
- Create: `frontend/src/vite-env.d.ts`
- Create: `frontend/src/App.tsx`
- Create: `frontend/src/main.tsx`
- Create: `frontend/src/styles.css`

- [ ] **Step 1: Write the failing frontend API test**

Create directories:

```powershell
New-Item -ItemType Directory -Force frontend/src/api
```

Create `frontend/src/api/health.test.ts`:

```typescript
import { afterEach, describe, expect, it, vi } from "vitest";
import { fetchBackendHealth } from "./health";

describe("fetchBackendHealth", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("fetches C++ backend health from the configured base URL", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        status: "ok",
        service: "bridge-report-cpp-backend",
        version: "0.1.0",
      }),
    });
    vi.stubGlobal("fetch", fetchMock);

    const health = await fetchBackendHealth("http://127.0.0.1:18080");

    expect(fetchMock).toHaveBeenCalledWith("http://127.0.0.1:18080/health");
    expect(health.status).toBe("ok");
    expect(health.service).toBe("bridge-report-cpp-backend");
  });
});
```

- [ ] **Step 2: Run the frontend test to verify it fails**

Run:

```powershell
cd frontend
npm test -- --run
```

Expected: FAIL because `package.json` and `src/api/health.ts` do not exist.

- [ ] **Step 3: Add frontend package and Vite configuration**

Create `frontend/package.json`:

```json
{
  "name": "bridge-report-frontend",
  "version": "0.1.0",
  "private": true,
  "type": "module",
  "scripts": {
    "dev": "vite --host 127.0.0.1 --port 5173",
    "build": "tsc -b && vite build",
    "test": "vitest"
  },
  "dependencies": {
    "@vitejs/plugin-react": "^4.3.0",
    "react": "^18.3.0",
    "react-dom": "^18.3.0",
    "vite": "^5.4.0"
  },
  "devDependencies": {
    "@types/node": "^26.1.0",
    "@types/react": "^18.3.0",
    "@types/react-dom": "^18.3.0",
    "typescript": "^5.5.0",
    "vitest": "^2.0.0"
  }
}
```

Create `frontend/index.html`:

```html
<!doctype html>
<html lang="zh-CN">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>Bridge Report System</title>
  </head>
  <body>
    <div id="root"></div>
    <script type="module" src="/src/main.tsx"></script>
  </body>
</html>
```

Create `frontend/tsconfig.json`:

```json
{
  "compilerOptions": {
    "target": "ES2020",
    "useDefineForClassFields": true,
    "lib": ["DOM", "DOM.Iterable", "ES2020"],
    "allowJs": false,
    "skipLibCheck": true,
    "esModuleInterop": true,
    "allowSyntheticDefaultImports": true,
    "strict": true,
    "forceConsistentCasingInFileNames": true,
    "module": "ESNext",
    "moduleResolution": "Bundler",
    "resolveJsonModule": true,
    "isolatedModules": true,
    "noEmit": true,
    "tsBuildInfoFile": "./node_modules/.tmp/tsconfig.tsbuildinfo",
    "jsx": "react-jsx"
  },
  "include": ["src"],
  "references": [{ "path": "./tsconfig.node.json" }]
}
```

Create `frontend/tsconfig.node.json`:

```json
{
  "compilerOptions": {
    "composite": true,
    "target": "ES2020",
    "lib": ["ES2020"],
    "skipLibCheck": true,
    "module": "ESNext",
    "moduleResolution": "Bundler",
    "allowSyntheticDefaultImports": true,
    "outDir": "./node_modules/.tmp/tsconfig-node",
    "tsBuildInfoFile": "./node_modules/.tmp/tsconfig.node.tsbuildinfo",
    "types": ["node"]
  },
  "include": ["vite.config.ts"]
}
```

Create `frontend/vite.config.ts`:

```typescript
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

export default defineConfig({
  plugins: [react()],
  server: {
    host: "127.0.0.1",
    port: 5173,
  },
});
```

- [ ] **Step 4: Add health API and dashboard UI**

Create `frontend/src/api/health.ts`:

```typescript
export type BackendHealth = {
  status: string;
  service: string;
  version: string;
  host?: string;
  port?: number;
  python_tools_base_url?: string;
  archive_root?: string;
};

export async function fetchBackendHealth(baseUrl: string): Promise<BackendHealth> {
  const response = await fetch(`${baseUrl}/health`);
  if (!response.ok) {
    throw new Error(`Backend health check failed with HTTP ${response.status}`);
  }
  return response.json() as Promise<BackendHealth>;
}
```

Create `frontend/src/vite-env.d.ts`:

```typescript
/// <reference types="vite/client" />
```

Create `frontend/src/App.tsx`:

```typescript
import { useEffect, useState } from "react";

import { BackendHealth, fetchBackendHealth } from "./api/health";
import "./styles.css";

const backendBaseUrl =
  import.meta.env.VITE_BACKEND_BASE_URL ?? "http://127.0.0.1:18080";

export function App() {
  const [health, setHealth] = useState<BackendHealth | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    fetchBackendHealth(backendBaseUrl)
      .then((result) => {
        setHealth(result);
        setError(null);
      })
      .catch((caught: unknown) => {
        setHealth(null);
        setError(caught instanceof Error ? caught.message : "Unknown health check error");
      });
  }, []);

  return (
    <main className="app-shell">
      <section className="status-panel">
        <h1>桥梁报告系统</h1>
        <div className="status-row">
          <span>C++ 主服务</span>
          <strong>{health?.status ?? "checking"}</strong>
        </div>
        <div className="status-grid">
          <span>服务</span>
          <span>{health?.service ?? "-"}</span>
          <span>版本</span>
          <span>{health?.version ?? "-"}</span>
          <span>Python 工具服务</span>
          <span>{health?.python_tools_base_url ?? "-"}</span>
          <span>归档目录</span>
          <span>{health?.archive_root ?? "-"}</span>
        </div>
        {error ? <p className="error-text">{error}</p> : null}
      </section>
    </main>
  );
}
```

Create `frontend/src/main.tsx`:

```typescript
import React from "react";
import ReactDOM from "react-dom/client";

import { App } from "./App";

ReactDOM.createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
```

Create `frontend/src/styles.css`:

```css
:root {
  font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
  color: #18202a;
  background: #f3f5f8;
}

body {
  margin: 0;
}

.app-shell {
  min-height: 100vh;
  display: grid;
  place-items: center;
  padding: 32px;
}

.status-panel {
  width: min(680px, 100%);
  background: #ffffff;
  border: 1px solid #d9e0e8;
  border-radius: 8px;
  padding: 24px;
  box-shadow: 0 12px 28px rgba(24, 32, 42, 0.08);
}

.status-panel h1 {
  margin: 0 0 24px;
  font-size: 24px;
}

.status-row {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  padding: 12px 0;
  border-bottom: 1px solid #e7ecf2;
}

.status-grid {
  display: grid;
  grid-template-columns: 140px 1fr;
  gap: 10px 18px;
  margin-top: 20px;
  font-size: 14px;
}

.error-text {
  margin: 18px 0 0;
  color: #a43333;
}
```

- [ ] **Step 5: Install frontend dependencies**

Run:

```powershell
cd frontend
npm install --registry=https://registry.npmmirror.com
```

Expected: npm installs React, Vite, TypeScript, and Vitest.

- [ ] **Step 6: Run frontend test to verify it passes**

Run:

```powershell
cd frontend
npm test -- --run
```

Expected:

```text
src/api/health.test.ts ... passed
```

- [ ] **Step 7: Build frontend**

Run:

```powershell
cd frontend
npm run build
```

Expected: TypeScript and Vite build succeed.

- [ ] **Step 8: Commit**

Run:

```powershell
git add frontend/package.json frontend/package-lock.json frontend/index.html frontend/tsconfig.json frontend/tsconfig.node.json frontend/vite.config.ts frontend/src/api/health.ts frontend/src/api/health.test.ts frontend/src/vite-env.d.ts frontend/src/App.tsx frontend/src/main.tsx frontend/src/styles.css
git commit -m "feat: add frontend health dashboard"
```

Expected: commit succeeds.

## Task 5: Development Scripts and End-to-End Health Check

**Files:**

- Create: `scripts/dev/start-python-tools.ps1`
- Create: `scripts/dev/start-cpp-backend.ps1`
- Create: `scripts/dev/start-frontend.ps1`
- Create: `scripts/dev/check-health.ps1`

- [ ] **Step 1: Write the health check script**

Create `scripts/dev/check-health.ps1`:

```powershell
$ErrorActionPreference = "Stop"

$cppHealth = Invoke-RestMethod http://127.0.0.1:18080/health
if ($cppHealth.status -ne "ok") {
  throw "C++ backend health check failed."
}

$toolsHealth = Invoke-RestMethod http://127.0.0.1:18081/health
if ($toolsHealth.status -ne "ok") {
  throw "Python tools health check failed."
}

$toolsViaCpp = Invoke-RestMethod http://127.0.0.1:18080/health/tools
if ($toolsViaCpp.tools_status -ne "reachable") {
  throw "C++ backend cannot reach Python tools service."
}

Write-Host "C++ backend: $($cppHealth.status)"
Write-Host "Python tools: $($toolsHealth.status)"
Write-Host "C++ -> Python: $($toolsViaCpp.tools_status)"
Write-Host "Health check passed."
```

- [ ] **Step 2: Run health check to verify it fails when services are stopped**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-health.ps1
```

Expected: FAIL with an `Invoke-RestMethod` connection error because services are not running.

- [ ] **Step 3: Add start scripts**

Create `scripts/dev/start-python-tools.ps1`:

```powershell
$ErrorActionPreference = "Stop"

Set-Location tools-python

uv sync --extra dev

uv run uvicorn bridge_report_tools.main:app --host 127.0.0.1 --port 18081
```

Create `scripts/dev/start-cpp-backend.ps1`:

```powershell
$ErrorActionPreference = "Stop"

Set-Location backend-cpp

cmake --preset vs2022-x64-debug
cmake --build --preset vs2022-x64-debug

.\build\vs-debug\Debug\bridge-report-backend.exe ..\config\local.example.json
```

Create `scripts/dev/start-frontend.ps1`:

```powershell
$ErrorActionPreference = "Stop"

Set-Location frontend

if (-not (Test-Path "node_modules")) {
  npm install --registry=https://registry.npmmirror.com
}

$env:VITE_BACKEND_BASE_URL = "http://127.0.0.1:18080"
npm run dev
```

- [ ] **Step 4: Start the services**

Open three PowerShell windows.

Window 1:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/start-python-tools.ps1
```

Expected: Uvicorn listens on `127.0.0.1:18081`.

Window 2:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/start-cpp-backend.ps1
```

Expected: C++ backend listens on `127.0.0.1:18080`.

Window 3:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/start-frontend.ps1
```

Expected: Vite listens on `127.0.0.1:5173`.

- [ ] **Step 5: Run the end-to-end health check**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-health.ps1
```

Expected:

```text
C++ backend: ok
Python tools: ok
C++ -> Python: reachable
Health check passed.
```

- [ ] **Step 6: Open the frontend**

Open:

```text
http://127.0.0.1:5173
```

Expected: the page displays `桥梁报告系统`, `C++ 主服务`, status `ok`, the C++ service name, Python tools URL, and archive root.

- [ ] **Step 7: Run all verification commands**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dev/check-layout.ps1
cd tools-python
uv run pytest tests/test_health.py -v
cd ..\backend-cpp
ctest --preset vs2022-x64-debug
cd ..\frontend
npm test -- --run
npm run build
cd ..
powershell -ExecutionPolicy Bypass -File scripts/dev/check-health.ps1
```

Expected:

```text
Project layout check passed.
tests/test_health.py::test_health_returns_tool_service_identity PASSED
100% tests passed
src/api/health.test.ts ... passed
vite build succeeds
Health check passed.
```

- [ ] **Step 8: Commit**

Run:

```powershell
git add scripts/dev/start-python-tools.ps1 scripts/dev/start-cpp-backend.ps1 scripts/dev/start-frontend.ps1 scripts/dev/check-health.ps1
git commit -m "chore: add development health scripts"
```

Expected: commit succeeds.

## Plan Self-Review

Spec coverage:

1. C++ main backend: covered in Task 3.
2. Visual Studio 2022 through CMake/vcpkg: covered in Task 3 through `CMakePresets.json`, `vcpkg.json`, and CMake commands.
3. Python FastAPI local tool service and uv dependency management: covered in Task 2.
4. React + TypeScript + Vite frontend: covered in Task 4.
5. PostgreSQL and archive boundaries: covered in Task 1 through config, migration folder, and archive folder.
6. C++ calls Python through local HTTP JSON: covered in Task 3 `/health/tools`.
7. Frontend calls only C++: covered in Task 4 using `VITE_BACKEND_BASE_URL`.
8. Engineering verification: covered in Task 1 layout check, Task 2 pytest, Task 3 CTest, Task 4 Vitest/build, Task 5 end-to-end health check.

Red-flag scan:

1. The plan contains no unresolved work markers.
2. The database migration file is a skeleton check with `select 1`; module 02 owns real schema design.

Type consistency:

1. Python health response fields match frontend and C++ expectations: `status`, `service`, `version`.
2. C++ config fields match `config/local.example.json`: `cpp_server.host`, `cpp_server.port`, `python_tools.base_url`, `archive.root`.
3. Frontend uses only C++ `/health`, not Python `/health`.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/01-tech-stack-and-project-skeleton-implementation-plan.md`. Two execution options:

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
