param(
  [ValidateSet('Debug','Release')][string]$Configuration='Debug',
  [string]$BuildDirectory='backend-cpp/build/vs-debug',
  [string]$Preset='vs2022-x64-debug',
  [string]$ConfigPath,
  [switch]$BuildOnly
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$config = Get-DevelopmentConfig $ConfigPath
$cmake = Find-DevelopmentTool 'cmake.exe' 'CMAKE_EXE'
if (-not $env:VCPKG_ROOT -or -not (Test-Path -LiteralPath (Join-Path $env:VCPKG_ROOT 'scripts/buildsystems/vcpkg.cmake'))) { throw 'Set VCPKG_ROOT to a valid vcpkg checkout.' }
$build = Resolve-RepositoryPath $BuildDirectory
$cachePath=Join-Path $build 'CMakeCache.txt'
if (Test-Path -LiteralPath $cachePath) {
  $cache=@{}
  foreach ($line in (Get-Content -LiteralPath $cachePath)) { if ($line -match '^([^#/][^:]*):[^=]+=(.*)$') { $cache[$matches[1]]=$matches[2] } }
  $expectedToolchain=[IO.Path]::GetFullPath((Join-Path $env:VCPKG_ROOT 'scripts/buildsystems/vcpkg.cmake'))
  if ($cache.CMAKE_TOOLCHAIN_FILE -and [IO.Path]::GetFullPath($cache.CMAKE_TOOLCHAIN_FILE) -ne $expectedToolchain) { throw 'Toolchain changed; select a fresh BuildDirectory instead of reusing this CMake cache.' }
  if ($cache.VCPKG_TARGET_TRIPLET -and $cache.VCPKG_TARGET_TRIPLET -ne 'x64-windows') { throw 'Triplet changed; select a fresh BuildDirectory.' }
  if ($Preset -eq 'vs2022-x64-debug' -and $cache.VCPKG_INSTALLED_DIR -and
      [IO.Path]::GetFullPath($cache.VCPKG_INSTALLED_DIR) -ne [IO.Path]::GetFullPath((Join-Path $build 'vcpkg_installed'))) {
    throw 'This cache uses an external vcpkg installation. Select a fresh BuildDirectory, or an explicit local preset for that installation.'
  }
}
if (-not $BuildOnly) { Assert-PortAvailable $config.Value.cpp_server.host $config.Value.cpp_server.port }
Push-Location (Join-Path (Get-RepositoryRoot) 'backend-cpp')
try {
  Invoke-CheckedCommand $cmake @('--preset',$Preset,'-B',$build) 'CMake configure'
  Invoke-CheckedCommand $cmake @('--build',$build,'--config',$Configuration,'--target','bridge-report-backend') 'CMake build'
} finally { Pop-Location }
$exe = Get-BackendArtifact $BuildDirectory $Configuration
$manifest = Join-Path (Split-Path $exe) 'build-info.json'
if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $manifest)) { throw 'Build did not publish the executable and its manifest.' }
if ($BuildOnly) { return }
Push-Location (Get-RepositoryRoot)
try { Invoke-CheckedCommand $exe @($config.Path) 'C++ backend' } finally { Pop-Location }
