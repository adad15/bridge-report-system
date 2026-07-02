$ErrorActionPreference = "Stop"

Set-Location frontend

if (-not (Test-Path "node_modules")) {
  npm install --registry=https://registry.npmmirror.com
}

$env:VITE_BACKEND_BASE_URL = "http://127.0.0.1:18080"
npm run dev
