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
