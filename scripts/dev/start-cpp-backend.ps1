# 默认 Debug：开发期保留迭代器调试与调试堆这层 bug 探测器，断点也准确。
# 需要用真实数据量点界面时传 -Configuration Release——Debug 关优化且开
# _ITERATOR_DEBUG_LEVEL=2，几千条构件的 JSON 序列化要慢约 4 倍
# （实测全量测试 3800ms vs 999ms）。
param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

Set-Location backend-cpp

cmake --preset vs2022-x64-debug
cmake --build --preset ("vs2022-x64-" + $Configuration.ToLower())

# 不用 Join-Path 拼三段：它的多段形式是 PowerShell 6+ 才有的，5.1 下会报
# "找不到接受实际参数的位置形式参数"。
& ".\build\vs-debug\$Configuration\bridge-report-backend.exe" ..\config\local.example.json
