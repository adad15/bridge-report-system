# 默认跑 Release：Debug 关优化且开迭代器调试，几千条构件的 JSON 序列化与字符串处理
# 在其下要慢约 4 倍（实测全量测试 3800ms vs 999ms）。需要断点调试时传 -Configuration Debug。
param(
  [ValidateSet("Release", "Debug")]
  [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

Set-Location backend-cpp

cmake --preset vs2022-x64-debug
cmake --build --preset ("vs2022-x64-" + $Configuration.ToLower())

& (Join-Path ".\build\vs-debug" $Configuration "bridge-report-backend.exe") ..\config\local.example.json
