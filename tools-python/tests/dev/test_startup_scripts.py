"""Fault-injection tests for the real PowerShell launch helpers, without business services."""
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[3]
SCRIPTS = ROOT / "scripts" / "dev"
pytestmark = pytest.mark.skipif(os.name != "nt", reason="Windows launch scripts")


@pytest.fixture(params=["powershell", "pwsh"])
def shell(request):
    executable = shutil.which(request.param)
    if not executable:
        pytest.skip(f"{request.param} unavailable")
    return executable


def quote(value):
    return "'" + str(value).replace("'", "''") + "'"


def run(shell, body, tmp_path, *, cwd=None, env=None):
    script = tmp_path / "验证 script.ps1"
    script.write_text(
        "$ErrorActionPreference='Stop'\n"
        + ". " + quote(SCRIPTS / "common.ps1") + "\n"
        + body,
        encoding="utf-8-sig",
    )
    merged = os.environ.copy()
    merged.pop("BRIDGE_REPORT_DATABASE_URL", None)
    merged.update(env or {})
    return subprocess.run(
        [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)],
        cwd=cwd or tmp_path, env=merged, capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=45,
    )


def test_native_failure_stops_before_next_stage(shell, tmp_path):
    fake = tmp_path / "bad command.cmd"
    fake.write_text("@exit /b 19\n", encoding="ascii")
    marker = tmp_path / "incorrect-success"
    result = run(shell, f"Invoke-CheckedCommand {quote(fake)} @() 'probe'\n"
                       f"Set-Content -LiteralPath {quote(marker)} 'bad'\n", tmp_path)
    assert result.returncode != 0
    assert not marker.exists()
    assert "19" in result.stderr


def test_config_and_artifact_independent_of_cwd(shell, tmp_path):
    output = tmp_path / "out.json"
    result = run(shell, f"$c=Get-DevelopmentConfig 'config/local.example.json'\n"
                       f"Write-Utf8Json {quote(output)} @{{config=$c.Path;exe=(Get-BackendArtifact 'backend-cpp/build/test' 'Release')}}", tmp_path)
    assert result.returncode == 0, result.stderr
    info = json.loads(output.read_text(encoding="utf-8"))
    assert Path(info["config"]) == ROOT / "config/local.example.json"
    assert Path(info["exe"]) == ROOT / "backend-cpp/build/test/Release/bridge-report-backend.exe"


@pytest.mark.parametrize("content", ["{", '{"cpp_server":{"port":0}}', "[]", '{"cpp_server":"bad"}'])
def test_invalid_explicit_config_is_rejected(shell, tmp_path, content):
    config = tmp_path / "invalid.json"
    config.write_text(content, encoding="utf-8")
    result = run(shell, f"Get-DevelopmentConfig {quote(config)}", tmp_path)
    assert result.returncode != 0


def test_missing_explicit_config_does_not_fall_back(shell, tmp_path):
    result = run(shell, "Get-DevelopmentConfig 'config/does-not-exist.json'", tmp_path)
    assert result.returncode != 0


def test_wrong_database_override_rejected_without_secret(shell, tmp_path):
    secret = "postgresql://u:secret-never-log@example/db"
    result = run(shell, "Get-DevelopmentConfig 'config/local.example.json'", tmp_path,
                 env={"BRIDGE_REPORT_DATABASE_URL": secret})
    assert result.returncode != 0
    assert secret not in result.stdout + result.stderr


def test_existing_port_is_not_reused(shell, tmp_path):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        result = run(shell, f"Assert-PortAvailable '127.0.0.1' {listener.getsockname()[1]}", tmp_path)
        assert result.returncode != 0
        assert listener.fileno() != -1


def test_old_or_wrong_health_identity_rejected(shell, tmp_path):
    result = run(shell, """
$old=@{status='ok';service='bridge-report-cpp-backend'}
if (Test-ServiceIdentity $old 'bridge-report-cpp-backend' '' '' '') { throw 'accepted old service' }
$new=@{status='ok';service='bridge-report-cpp-backend';instance_id='i';launch_id='old';build_id='b'}
if (Test-ServiceIdentity $new 'bridge-report-cpp-backend' 'new' 'b' '') { throw 'accepted wrong launch' }
if (Test-ServiceIdentity $new 'bridge-report-cpp-backend' 'old' 'wrong' '') { throw 'accepted wrong build' }
if (-not (Test-ServiceIdentity $new 'bridge-report-cpp-backend' 'old' 'b' '')) { throw 'rejected match' }
""", tmp_path)
    assert result.returncode == 0, result.stderr


def test_wait_is_bounded(shell, tmp_path):
    result = run(shell, "Wait-ServiceReady $null { $false } 1 'test-service'", tmp_path)
    assert result.returncode != 0
    assert "test-service" in result.stderr


def test_owned_process_arguments_and_cleanup(shell, tmp_path):
    output = tmp_path / "args.json"
    code = "import json,sys,time;open(sys.argv[1],'w',encoding='utf-8').write(json.dumps(sys.argv[2:]));time.sleep(15)"
    values = ["中文 with spaces", 'a"b', "trailing\\"]
    argument_list = ",".join(quote(x) for x in ["-c", code, str(output), *values])
    result = run(shell, f"""
$p=Start-OwnedService {quote(sys.executable)} @({argument_list}) {quote(tmp_path)} {quote(tmp_path / 'child')}
try {{
  Wait-ServiceReady $p {{ Test-Path -LiteralPath {quote(output)} }} 10 'child'
}} finally {{ Stop-OwnedService $p }}
if (-not $p.HasExited) {{ throw 'owned child remained running' }}
""", tmp_path)
    assert result.returncode == 0, result.stderr
    assert json.loads(output.read_text(encoding="utf-8")) == values


@pytest.mark.parametrize("stage", ["configure", "build"])
def test_cpp_failure_never_starts_old_executable(shell, tmp_path, stage):
    root = tmp_path / "项目 with spaces"
    (root / "scripts/dev").mkdir(parents=True)
    (root / "config").mkdir()
    (root / "backend-cpp/build/vs-debug/Debug").mkdir(parents=True)
    for name in ["common.ps1", "start-cpp-backend.ps1"]:
        shutil.copyfile(SCRIPTS / name, root / "scripts/dev" / name)
    shutil.copyfile(ROOT / "config/local.example.json", root / "config/local.example.json")
    calls = root / "calls.txt"
    fake = root / "cmake.cmd"
    fake.write_text('@echo off\n'
                    'echo call>>"%CALLS_FILE%"\n'
                    + ('exit /b 31\n' if stage == "configure" else
                       'if "%~1"=="--preset" exit /b 0\nexit /b 32\n'), encoding="ascii")
    vcpkg = root / "vcpkg/scripts/buildsystems"
    vcpkg.mkdir(parents=True)
    (vcpkg / "vcpkg.cmake").write_text("", encoding="ascii")
    old = root / "backend-cpp/build/vs-debug/Debug/bridge-report-backend.exe"
    old.write_bytes(b"deliberately not executable; must not reach launch")
    result = run(shell, "& " + quote(root / "scripts/dev/start-cpp-backend.ps1"),
                 tmp_path, env={"CMAKE_EXE": str(fake), "VCPKG_ROOT": str(root / "vcpkg"), "CALLS_FILE": str(calls)})
    assert result.returncode != 0
    assert ("31" if stage == "configure" else "32") in result.stderr
    assert len(calls.read_text().splitlines()) == (1 if stage == "configure" else 2)


def test_database_probe_failure_is_sanitized(shell, tmp_path):
    fake = tmp_path / "psql.cmd"
    fake.write_text("@echo secret-db-password 1>&2\n@exit /b 17\n", encoding="ascii")
    result = run(shell, "Assert-DatabaseReady (Get-DevelopmentConfig 'config/local.example.json')",
                 tmp_path, env={"PSQL_EXE": str(fake)})
    assert result.returncode != 0
    assert "17" in result.stderr
    assert "secret-db-password" not in result.stdout + result.stderr


def test_owned_cleanup_includes_confirmed_descendants(shell, tmp_path):
    output = tmp_path / "pid.txt"
    code = ("import subprocess,sys,time;"
            "p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(40)']);"
            "open(sys.argv[1],'w').write(str(p.pid));time.sleep(40)")
    args = ",".join(quote(x) for x in ["-c", code, str(output)])
    result = run(shell, f"""
$p=Start-OwnedService {quote(sys.executable)} @({args}) {quote(tmp_path)} {quote(tmp_path/'tree')}
try {{
  Wait-ServiceReady $p {{ Test-Path -LiteralPath {quote(output)} }} 10 'tree'
  $child=[Diagnostics.Process]::GetProcessById([int](Get-Content -LiteralPath {quote(output)}))
  $null=$child.Handle
}} finally {{ Stop-OwnedService $p }}
try {{ if (-not $child.HasExited) {{ throw 'confirmed descendant survived cleanup' }} }}
finally {{ $child.Dispose() }}
""", tmp_path)
    assert result.returncode == 0, result.stderr


def test_environment_restore_removes_previously_absent_variable(shell, tmp_path):
    result = run(shell, """
Remove-Item Env:BRIDGE_TEST_ABSENT -ErrorAction SilentlyContinue
$saved=@{BRIDGE_TEST_ABSENT=[Environment]::GetEnvironmentVariable('BRIDGE_TEST_ABSENT')}
$env:BRIDGE_TEST_ABSENT='temporary'
Restore-DevelopmentEnvironment $saved
if (Test-Path Env:BRIDGE_TEST_ABSENT) { throw 'restore left an empty variable' }
""", tmp_path)
    assert result.returncode == 0, result.stderr


def test_stop_record_with_wrong_creation_time_leaves_process_alive(shell, tmp_path):
    args = ",".join(quote(x) for x in ["-c", "import time;time.sleep(30)"])
    record = tmp_path / "startup.json"
    result = run(shell, f"""
$p=Start-OwnedService {quote(sys.executable)} @({args}) {quote(tmp_path)} {quote(tmp_path/'foreign')}
try {{
  Write-Utf8Json {quote(record)} @{{processes=@(@{{pid=$p.Id;started_at=$p.StartTime.AddDays(-1).ToUniversalTime().ToString('o');executable=$p.MainModule.FileName}})}}
  & {quote(SCRIPTS/'stop-all.ps1')} -StartupRecord {quote(record)}
  if ($p.HasExited) {{ throw 'mismatched process was stopped' }}
}} finally {{ Stop-OwnedService $p }}
""", tmp_path)
    assert result.returncode == 0, result.stderr
