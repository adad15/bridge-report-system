param([string]$OutputPath,[string]$BuildDirectory='backend-cpp/build/vs-debug',[ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$report=[ordered]@{recorded_at=[DateTime]::UtcNow.ToString('o'); powershell=$PSVersionTable.PSVersion.ToString(); os=[Environment]::OSVersion.VersionString; architecture=$env:PROCESSOR_ARCHITECTURE; configuration=$Configuration; executable=(Get-BackendArtifact $BuildDirectory $Configuration); tools=@{}; inputs=@{}; missing=@()}
foreach ($tool in @('node.exe','npm.cmd','uv.exe','cmake.exe','psql.exe','git.exe')) {
  try {
    $override=''; if ($tool -eq 'psql.exe') {$override='PSQL_EXE'}; if ($tool -eq 'cmake.exe') {$override='CMAKE_EXE'}
    $path=Find-DevelopmentTool $tool $override
    $version=@(& $path --version)[0]
    if ($LASTEXITCODE -ne 0) { throw 'Version probe failed.' }
    $report.tools[$tool]=@{path=$path; version=[string]$version}
  } catch { $report.missing+= $tool }
}
$root=Get-RepositoryRoot
$python=Join-Path $root 'tools-python/.venv/Scripts/python.exe'
if (Test-Path -LiteralPath $python) {
  $version=& $python --version
  $report.tools['python']=@{path=$python; version=[string]$version}
} else { $report.missing+='python virtual environment' }
$git=Get-Command git.exe -ErrorAction SilentlyContinue
if ($git) {
  $report['git_revision']=(& $git.Source -C $root rev-parse HEAD)
  $report['worktree_dirty']= [bool](& $git.Source -C $root status --porcelain --untracked-files=normal)
}
foreach ($name in @('frontend/package.json','frontend/package-lock.json','tools-python/pyproject.toml','tools-python/uv.lock','backend-cpp/vcpkg.json','backend-cpp/CMakePresets.json')) {
  $report.inputs[$name]=Get-FileDigest (Join-Path $root $name)
}
if ($env:VCPKG_ROOT -and (Test-Path -LiteralPath $env:VCPKG_ROOT)) {
  $report['vcpkg_root']=$env:VCPKG_ROOT
  if ($git) { $report['vcpkg_revision']=(& $git.Source -C $env:VCPKG_ROOT rev-parse HEAD) }
  $status=Join-Path (Resolve-RepositoryPath $BuildDirectory) 'vcpkg_installed/vcpkg/status'
  if (Test-Path -LiteralPath $status) { $report['installed_status_sha256']=Get-FileDigest $status }
} else { $report.missing+='VCPKG_ROOT' }
$vswhere=Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio/Installer/vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
  $report['visual_studio']=@(& $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json | Select-Object installationVersion,installationPath)
  if ($report.visual_studio.Count -eq 0) { $report.missing+='MSVC' }
} else { $report.missing+='Visual Studio locator' }
$manifest=Join-Path (Split-Path $report.executable) 'build-info.json'
if (Test-Path -LiteralPath $manifest) { $report['build']=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json }
$cache=Join-Path (Resolve-RepositoryPath $BuildDirectory) 'CMakeCache.txt'
if (Test-Path -LiteralPath $cache) {
  $report['cmake_environment']=@(Get-Content -LiteralPath $cache | Where-Object { $_ -match '^(CMAKE_(CXX_COMPILER|GENERATOR|VS_WINDOWS_TARGET_PLATFORM_VERSION|VS_PLATFORM_TOOLSET)|VCPKG_(TARGET_TRIPLET|INSTALLED_DIR|OVERLAY_PORTS|OVERLAY_TRIPLETS))[^=]*=' })
  $installedLine=Get-Content -LiteralPath $cache | Where-Object { $_ -match '^VCPKG_INSTALLED_DIR:[^=]+=' } | Select-Object -First 1
  if ($installedLine) {
    $status=Join-Path ($installedLine -replace '^[^=]+=','') 'vcpkg/status'
    if (Test-Path -LiteralPath $status) {
      $report['installed_status_sha256']=Get-FileDigest $status
      $report['installed_dependencies']=@((Get-Content -LiteralPath $status -Raw) -split '(?:\r?\n){2,}' | Where-Object { $_ -match 'Status: install ok installed' } | ForEach-Object {
        $entry=@{}; foreach ($line in ($_ -split '\r?\n')) { if ($line -match '^(Package|Version|Port-Version|Architecture|Feature): (.*)$') { $entry[$matches[1]]=$matches[2] } }; $entry
      })
    }
  }
}
$report['machine']=@{cpu=@(Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name); memory_bytes=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory}
$report['office']='Not probed: real Office verification is explicit; ProgID alone does not prove application identity.'
if ($OutputPath) { Write-Utf8Json (Resolve-RepositoryPath $OutputPath) $report }
$report | ConvertTo-Json -Depth 10
if ($report.missing.Count -gt 0) { throw "Missing tools: $($report.missing -join ', ')" }
