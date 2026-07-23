# 默认 Debug：开发期保留迭代器调试与调试堆这层 bug 探测器，断点也准确。
# 需要用真实数据量点界面时传 -Configuration Release——Debug 关优化且开
# _ITERATOR_DEBUG_LEVEL=2，几千条构件的 JSON 序列化要慢约 4 倍
# （实测全量测试 3800ms vs 999ms）。
param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

# 由脚本自身定位仓库根，不依赖调用方的工作目录（start-all.ps1 以 backend-cpp 作
# 工作目录调用本脚本）。
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

# 构建要在 backend-cpp 下进行：CMake 预设在那里。
Set-Location (Join-Path $repositoryRoot "backend-cpp")
cmake --preset vs2022-x64-debug
cmake --build --preset ("vs2022-x64-" + $Configuration.ToLower())

# 但必须回到仓库根再启动后端。配置里的 archive.root 是相对路径 "archive"，
# 按进程工作目录解析；若留在 backend-cpp 启动，归档会写进 backend-cpp\archive，
# 与仓库根的 archive（.gitkeep 所标记的正式位置）分叉，表现为照片时有时无。
Set-Location $repositoryRoot
& "backend-cpp\build\vs-debug\$Configuration\bridge-report-backend.exe" "config\local.example.json"
