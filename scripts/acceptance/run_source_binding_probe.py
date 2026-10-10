"""Compile the offline probe against production inventory sources, then audit exported JSON."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inventory", type=Path)
    parser.add_argument("profile", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "runtime/acceptance-tools/source-binding")
    parser.add_argument("--dependencies", type=Path,
                        default=ROOT / "backend-cpp/build/batch-a/vcpkg_installed/x64-windows")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if not build.is_relative_to(ROOT / "runtime"):
        raise SystemExit("Build directory must be inside the repository runtime directory.")
    build.mkdir(parents=True, exist_ok=True)
    args.output = args.output.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    dependencies = args.dependencies.resolve()
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    flags = subprocess.CREATE_NO_WINDOW
    installation = subprocess.check_output(
        [str(vswhere), "-latest", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
        creationflags=flags).decode("utf-8").strip()
    if not installation:
        raise SystemExit("MSVC x64 tools were not found.")
    devcmd = Path(installation) / "Common7/Tools/VsDevCmd.bat"
    environment_text = subprocess.check_output(
        f'cmd.exe /d /s /c ""{devcmd}" -no_logo -arch=x64 -host_arch=x64 >nul && set"',
        creationflags=flags).decode("mbcs", errors="replace")
    env = {}
    for line in environment_text.splitlines():
        if "=" in line and not line.startswith("="):
            key, value = line.split("=", 1)
            env[key.upper()] = value
    compiler = shutil.which("cl.exe", path=env.get("PATH", ""))
    if not compiler:
        raise SystemExit("Could not initialize MSVC.")
    sources = [ROOT / "scripts/acceptance/source_binding_probe.cpp"] + [
        ROOT / "backend-cpp/src/inventory" / name for name in (
            "ComponentMatcher.cpp", "ComponentCategoryLexicon.cpp",
            "ComponentRangeParser.cpp", "SideComponentPair.cpp")]
    inventory, profile = args.inventory.resolve(), args.profile.resolve()
    inputs = [inventory, profile, *sources]
    hashes = {str(path): digest(path) for path in inputs}
    executable = build / "source-binding-probe.exe"
    command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/utf-8", "/MD", "/O2",
               "/I" + str(ROOT / "backend-cpp/include"), "/I" + str(dependencies / "include"),
               "/Fe:" + str(executable), *map(str, sources), str(dependencies / "lib/jsoncpp.lib")]
    with (args.output.parent / "binding-probe-build.log").open("w", encoding="utf-8") as log:
        subprocess.run(command, cwd=build, env=env, stdout=log, stderr=subprocess.STDOUT,
                       creationflags=flags, check=True)
    env["PATH"] = str(dependencies / "bin") + os.pathsep + env["PATH"]
    result = subprocess.run(
        [str(executable), str(inventory), str(profile), str(args.output)],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=flags)
    if result.returncode:
        raise SystemExit(result.stderr.decode("utf-8", errors="replace"))
    if any(digest(path) != hashes[str(path)] for path in inputs):
        raise SystemExit("Audit inputs changed during execution.")
    (args.output.parent / "binding-probe-identity.json").write_text(json.dumps({
        "inputs_sha256": hashes, "executable_sha256": digest(executable),
        "result_sha256": digest(args.output), "business_writes_performed": False,
    }, ensure_ascii=False, indent=2), encoding="utf-8")
    print(result.stdout.decode("utf-8"))


if __name__ == "__main__":
    main()
