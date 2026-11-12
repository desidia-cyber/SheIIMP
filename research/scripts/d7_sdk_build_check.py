#!/usr/bin/env python3
"""MSVC-only compile check for a Dumper-7 generated SDK.

Dumper-7 ships CppSDK/SDKTest.py which builds the SDK twice (MSVC and ClangCL).
This machine has no ClangCL component, so that built-in test stops with
"Could not select Visual Studio toolchains ... for ClangCL" and waits on Enter.

This script reuses Dumper-7's OWN CMakeLists.txt and TestMain.cpp (read out of
the generated SDKTest.py -- nothing is rewritten) and runs the MSVC half only.

usage: python d7_sdk_build_check.py --sdk <...\\CppSDK> --out <work-dir>
exit 0 = SDK compiled, 1 = failed (see <work-dir>/msvc.log)
"""
import argparse
import importlib.util
import json
import shutil
import subprocess
import sys
from pathlib import Path


def load_sdktest(sdk_root: Path):
    """The generator only writes SDKTest.py when its own compile-test toggle is on, so fall back
    to the copy that came with the published release's SDK (same CMakeLists/TEST_MAIN)."""
    candidates = [sdk_root / "SDKTest.py", sdk_root.parent.parent / "SDKTest.py"]
    script = next((c for c in candidates if c.is_file()), None)
    if script is None:
        raise SystemExit(f"SDKTest.py not found in {sdk_root} (and no fallback)")
    print(f"using {script}")
    spec = importlib.util.spec_from_file_location("d7_sdktest", script)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run_logged(command, log_file: Path, cwd: Path) -> bool:
    printable = subprocess.list2cmdline(command)
    with log_file.open("a", encoding="utf-8", errors="replace") as log:
        log.write(f"\n> {printable}\n")
        log.flush()
        try:
            r = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, text=True, check=False)
        except OSError as e:
            log.write(f"\nFailed to start command: {e}\n")
            return False
        log.write(f"\nExit code: {r.returncode}\n")
    print(f"  {'ok  ' if r.returncode == 0 else 'FAIL'} {command[0]} {' '.join(command[1:4])} (exit {r.returncode})")
    return r.returncode == 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True, help="path to the CppSDK directory")
    ap.add_argument("--out", required=True, help="work/output directory")
    args = ap.parse_args()

    sdk_root = Path(args.sdk).resolve()
    out = Path(args.out).resolve()
    work = out / "SDKTest-MSVC"
    if work.exists():
        shutil.rmtree(work)
    (work / "Logs").mkdir(parents=True)
    (work / "Output").mkdir(parents=True)

    mod = load_sdktest(sdk_root)
    (work / "TestMain.cpp").write_text(mod.TEST_MAIN, encoding="utf-8", newline="\n")
    (work / "CMakeLists.txt").write_text(mod.CMAKE_LISTS, encoding="utf-8", newline="\n")

    if shutil.which("cmake") is None:
        print("cmake not on PATH")
        return 1
    caps = subprocess.run(["cmake", "-E", "capabilities"], capture_output=True, text=True, check=True)
    generators = {e["name"] for e in json.loads(caps.stdout)["generators"]}
    print("cmake generators available:", ", ".join(sorted(g for g in generators if "Visual Studio" in g)))

    try:
        generator, instance = mod.find_vs_toolchain(generators)  # toolset=None -> MSVC
    except Exception as e:  # noqa: BLE001
        print(f"toolchain selection failed: {e}")
        return 1
    print(f"toolchain: {generator}  instance={instance}")

    log = work / "Logs" / "MSVC.log"
    log.write_text("MSVC build log\n", encoding="utf-8")
    build_dir = work / "Build-MSVC"

    configure = [
        "cmake", "-S", str(work), "-B", str(build_dir),
        "-G", generator, "-A", "x64",
        f"-DSDK_ROOT={sdk_root}",
        f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY={work / 'Output'}",
        f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={work / 'Output'}",
    ]
    if instance:
        configure.append(f"-DCMAKE_GENERATOR_INSTANCE={instance}")
    print("configuring ...")
    if not run_logged(configure, log, work):
        print(f"FAILED: see {log}")
        return 1

    build = ["cmake", "--build", str(build_dir), "--config", "Release", "--target", "SDKBuildTest"]
    assert_size = (sdk_root / "Assertions.inl").stat().st_size if (sdk_root / "Assertions.inl").is_file() else 0
    print(f"building (4 TUs against the {assert_size/1e6:.1f} MB Assertions.inl) ...")
    if not run_logged(build, log, work):
        print(f"FAILED: see {log}")
        return 1

    dlls = list((work / "Output").glob("*.dll"))
    print(f"OK: SDK compiled; {len(dlls)} dll(s) in {work / 'Output'} ({[d.name for d in dlls]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
