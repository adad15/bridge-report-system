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
