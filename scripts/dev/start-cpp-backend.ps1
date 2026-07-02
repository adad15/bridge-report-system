$ErrorActionPreference = "Stop"

Set-Location backend-cpp

cmake --preset vs2022-x64-debug
cmake --build --preset vs2022-x64-debug

.\build\vs-debug\Debug\bridge-report-backend.exe ..\config\local.example.json
