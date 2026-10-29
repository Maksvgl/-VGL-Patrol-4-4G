#!/usr/bin/env python3
"""Run the real PowerShell driver against local fake executables, never hardware."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parent
DRIVER = ROOT.parent / "windows" / "PPK_Test.ps1"
CANDIDATE = "VGL_Patrol_1.6_PPK_RAM_TEST_FULL_S132_BOOTLOADER.hex"
APPROVED = "VGL_Patrol_1.6_1400ms_RF5ms_FULL_S132_BOOTLOADER.hex"
DRIVER_BYTES = DRIVER.read_bytes()
CASE_ROOT = ROOT / "cases" / (datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S") + "_" + uuid.uuid4().hex)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(pwsh, index, name, args, scenario="", expected=0, needle=None,
            corrupt=False, traversal=False, discovery=""):
    folder = CASE_ROOT / f"{index:02d} {name} with spaces"
    folder.mkdir(parents=True)
    (folder / DRIVER.name).write_bytes(DRIVER_BYTES)
    for filename in (CANDIDATE, APPROVED):
        (folder / filename).write_text(":00000001FF\n", encoding="ascii")
    manifest = {"firmware_file": CANDIDATE, "files": [
        {"file": path.name, "sha256": sha(path)}
        for path in (folder / DRIVER.name, folder / CANDIDATE, folder / APPROVED)
    ]}
    if traversal:
        manifest["files"][0]["file"] = "../PPK_Test.ps1"
    (folder / "flash_manifest.json").write_text(json.dumps(manifest))
    if corrupt:
        (folder / CANDIDATE).write_text("corrupted\n")
    trace = folder / "trace.jsonl"
    state = folder / "state.json"
    env = os.environ.copy()
    paths = [str(CASE_ROOT / "bin")]
    if discovery:
        mock = ROOT / "mock_tool.py"
        nrfbin = folder / "Nordic bin"
        javabin = folder / "Java bin"
        seggerbin = folder / "SEGGER path bin"
        programfiles = folder / "Program Files"
        installed = programfiles / "SEGGER" / "JLink_V920"
        for bindir in (nrfbin, javabin, seggerbin, installed):
            bindir.mkdir(parents=True)
        (nrfbin / "nrfjprog.exe").symlink_to(mock)
        java = javabin / "JLink.exe"
        java.write_text("#!/usr/bin/env python3\nimport sys\nprint('Error: unknown option: '+(sys.argv[1] if len(sys.argv)>1 else ''))\nprint('Usage: jlink <options> --module-path <modulepath> --add-modules <module>[,<module>...]')\nsys.exit(2)\n")
        java.chmod(0o755)
        (seggerbin / "JLink.exe").symlink_to(mock)
        (installed / "JLink.exe").symlink_to(mock)
        if discovery in ("java_only", "missing", "java_then_segger_path"):
            (installed / "JLink.exe").unlink()
        paths = [str(nrfbin)]
        if discovery != "missing":
            paths.append(str(javabin))
        if discovery == "java_then_segger_path":
            paths.append(str(seggerbin))
        env["ProgramFiles"] = str(programfiles)
        env["ProgramFiles(x86)"] = str(folder / "Empty Program Files x86")
        args = [arg.replace("{JAVA_JLINK}", str(javabin / "JLink.exe"))
                .replace("{SEGGER_JLINK}", str(installed / "JLink.exe"))
                .replace("{MISSING_JLINK}", str(folder / "No JLink.exe")) for arg in args]
    env.update({"MOCK_STATE_PATH": str(state), "MOCK_TRACE_PATH": str(trace),
                "MOCK_SCENARIO": scenario, "PATH": os.pathsep.join(paths + [env["PATH"]])})
    result = subprocess.run([str(pwsh), "-NoLogo", "-NoProfile", "-NonInteractive", "-File",
                             str(folder / DRIVER.name), *args], env=env,
                            capture_output=True, text=True, timeout=90)
    output = result.stdout + result.stderr
    (folder / "output.txt").write_text(output)
    events = [json.loads(line) for line in trace.read_text().splitlines()] if trace.exists() else []
    commands = [event["command"] for event in events if "command" in event]
    calls = [event for event in events if "tool" in event]
    problems = []
    if result.returncode != expected:
        problems.append(f"exit {result.returncode}, expected {expected}")
    if needle and needle not in output:
        problems.append(f"missing expected output: {needle!r}")
    no_hardware = "-ValidateOnly" in args or corrupt or traversal or name in (
        "invalid_recover", "invalid_restore", "invalid_check_flash", "invalid_validate_flash")
    if no_hardware and events:
        problems.append("unexpected target/tool access before validation")
    if discovery and expected != 0 and calls:
        problems.append("discovery failure accessed a probe/target tool before rejecting J-Link")
    if discovery and any("identity" in event for event in events):
        problems.append("discovery executed a candidate instead of inspecting its identity")
    if discovery and expected == 0:
        jlinks = [call for call in calls if call["tool"].lower() == "jlink.exe"]
        if not jlinks:
            problems.append("discovery success did not use SEGGER target commands")
        if any(Path(call.get("path", "")).parent.name.lower() == "java bin" for call in jlinks):
            problems.append("Java executable was used for target commands")
    if result.returncode == 0 and not no_hardware:
        if commands[-4:] != ["Go", "WriteDP 2, 0", "WriteDP 1, 0", "Exit"]:
            problems.append("final target commands did not release DP power in required order")
        if events[-1] != {"command": "Exit"}:
            problems.append("tool/target access happened after final debug release")
        reports = list(folder.glob("logs/*/result.json"))
        if len(reports) != 1 or not json.loads(reports[0].read_text()).get("success"):
            problems.append("successful operation did not produce successful result.json")
    programs = [call for call in calls if "--program" in call["args"]]
    recovers = [call for call in calls if "--recover" in call["args"]]
    if bool(recovers) != ("-Recover" in args and result.returncode == 0):
        problems.append("recover command presence differs from explicit successful recovery")
    if "-Flash" not in args and programs:
        problems.append("non-flash operation programmed flash")
    if "-Flash" not in args and any(flag in call["args"] for call in calls
                                     for flag in ["--memwr", "--recover", "--reset"]):
        problems.append("non-flash operation wrote UICR, recovered or reset target")
    if result.returncode != 0 and list(folder.glob("logs/*/result.json")):
        problems.append("failed operation produced a success report")
    if name in ("multiple_probes", "unexpected_uicr", "locked_flash") and programs:
        problems.append("unsafe condition did not prevent programming")
    if name == "recover_flash":
        if not any("--memwr" in call["args"] for call in calls):
            problems.append("erased UICR boot address was not restored")
        order = [next((flag for flag in ["--recover", "--memrd", "--program", "--memwr", "--reset"]
                       if flag in call["args"]), "other") for call in calls]
        if order != ["other", "--recover", "--memrd", "--program", "--memwr", "--memrd", "--reset", "other", "other", "other"]:
            problems.append(f"recovery/program/readback ordering changed: {order}")
    if "-CheckNormal" in args and result.returncode == 0:
        if any(command.lower().startswith(("w4 ", "w1 ")) for command in commands):
            problems.append("normal check wrote target RAM/registers")
        if any(flag in call["args"] for call in calls for flag in ["--program", "--memwr", "--recover", "--reset"]):
            problems.append("normal check changed flash/UICR/reset")
    if "-RestoreApproved" in args and result.returncode == 0:
        if not programs or programs[0]["args"][programs[0]["args"].index("--program") + 1] != str(folder / APPROVED):
            problems.append("restore programmed unexpected HEX")
        if any(command.lower().startswith(("w4 ", "w1 ")) for command in commands):
            problems.append("restore sent factory request")
        post_boot_verifies = [call for call in calls if "--verify" in call["args"]
                              and "--program" not in call["args"] and "--memwr" not in call["args"]]
        if len(post_boot_verifies) != 1 or post_boot_verifies[0]["args"][post_boot_verifies[0]["args"].index("--verify") + 1] != str(folder / APPROVED):
            problems.append("restore did not perform post-boot image verification")
    if result.returncode == 0 and not no_hardware and "-CheckNormal" not in args and "-RestoreApproved" not in args:
        if "W4 0xE000E200, 0x00100000" not in commands:
            problems.append("request did not pend the verified enabled IRQ20")
    return {"name": name, "passed": not problems, "exit_code": result.returncode,
            "expected_exit_code": expected, "issues": problems, "tool_calls": len(calls),
            "jlink_commands": len(commands), "output": str(folder / "output.txt")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pwsh", default=str(ROOT / "runtime" / "pwsh"))
    opts = parser.parse_args()
    pwsh = Path(opts.pwsh).resolve()
    if not pwsh.is_file():
        parser.error("PowerShell executable required; pass --pwsh /absolute/path/to/pwsh")
    bindir = CASE_ROOT / "bin"
    bindir.mkdir(parents=True)
    mock = ROOT / "mock_tool.py"
    mock.chmod(0o755)
    for name in ("nrfjprog.exe", "JLink.exe"):
        path = bindir / name
        path.unlink(missing_ok=True)
        path.symlink_to(mock)
    cases = [
        ("validate_only", ["-ValidateOnly"], "", 0, "PACKAGE CHECK PASSED."),
        ("flash", ["-Flash"], "", 0, "READY FOR PPK MEASUREMENT."),
        ("recover_flash", ["-Flash", "-Recover"], "", 0, "READY FOR PPK MEASUREMENT."),
        ("start", [], "", 0, "READY FOR PPK MEASUREMENT."),
        ("check_normal_inactive", ["-CheckNormal"], "", 0, "NORMAL_BOOT_CONFIRMED."),
        ("check_normal_active", ["-CheckNormal"], "normal_active", 0, "normal activation flag: 1"),
        ("restore", ["-Flash", "-RestoreApproved"], "", 0, "APPROVED_1_6_RESTORED."),
        ("recover_restore", ["-Flash", "-Recover", "-RestoreApproved"], "", 0, "APPROVED_1_6_RESTORED."),
        ("restore_replaced_after_boot", ["-Flash", "-RestoreApproved"], "wrong_installed", 1, "Verify_approved_firmware_after_boot failed"),
        ("multiple_probes", ["-Flash"], "multiple", 1, "Connect exactly one SWD probe"),
        ("explicit_probe", ["-ProbeSerial", "987654321"], "multiple", 0, "Selected SWD probe: 987654321"),
        ("unexpected_uicr", ["-Flash"], "unexpected_uicr", 1, "Unexpected UICR bootloader address"),
        ("locked_flash", ["-Flash"], "locked", 1, "Cannot access the processor configuration"),
        ("wrong_installed", [], "wrong_installed", 1, "Verify_installed_PPK_firmware failed"),
        ("normal_wrong_installed", ["-CheckNormal"], "wrong_installed", 1, "do not run recover merely"),
        ("rtc_failure", [], "rtc_failure", 1, "RTC read/validity failed"),
        ("disabled_irq", [], "irq_disabled", 1, "IRQ20) is disabled"),
        ("missing_stamp", [], "missing_stamp", 1, "startup stamp is absent"),
        ("stale_normal_mailbox", ["-CheckNormal"], "stale_mailbox", 1, "payload is not fully clear"),
        ("running_request", [], "running_request", 1, "previous factory request is still running"),
        ("bad_cookie", [], "bad_cookie", 1, "acknowledgement cookie is invalid"),
        ("wrong_ticks", [], "wrong_ticks", 1, "polling period is not 45875"),
        ("inactive", [], "inactive", 1, "active flag was not set"),
        ("coil", [], "coil", 1, "charging-coil power"),
        ("power_saving", [], "power_saving", 1, "power-saving mode"),
        ("invalid_rtc", [], "invalid_rtc", 1, "TEST FAILED:"),
        ("old_rtc", [], "old_rtc", 1, "not close to the PC time"),
        ("final_jlink_error", [], "jlink_error", 1, "J-Link reported a failure"),
        ("invalid_recover", ["-Recover"], "", 1, "valid only with -Flash"),
        ("invalid_restore", ["-RestoreApproved"], "", 1, "requires -Flash"),
        ("invalid_check_flash", ["-CheckNormal", "-Flash"], "", 1, "cannot be combined"),
        ("invalid_validate_flash", ["-ValidateOnly", "-Flash"], "", 1, "cannot be combined"),
    ]
    results = [execute(pwsh, index, *case) for index, case in enumerate(cases, 1)]
    results.append(execute(pwsh, len(results) + 1, "wrong_hash", [], expected=1,
                           needle="SHA-256 mismatch", corrupt=True))
    results.append(execute(pwsh, len(results) + 1, "unsafe_manifest", [], expected=1,
                           needle="Unsafe filename in manifest", traversal=True))
    discovery_cases = [
        ("java_path_segger_install", ["-Flash", "-Recover"], "installed", 0, "READY FOR PPK MEASUREMENT."),
        ("java_then_segger_path", [], "java_then_segger_path", 0, "READY FOR PPK MEASUREMENT."),
        ("java_only_recover_blocked", ["-Flash", "-Recover"], "java_only", 1, "SEGGER"),
        ("missing_jlink_recover_blocked", ["-Flash", "-Recover"], "missing", 1, "SEGGER"),
        ("explicit_java_recover_blocked", ["-Flash", "-Recover", "-JLinkPath", "{JAVA_JLINK}"], "installed", 1, "SEGGER"),
        ("explicit_segger_java_path", ["-JLinkPath", "{SEGGER_JLINK}"], "installed", 0, "READY FOR PPK MEASUREMENT."),
        ("explicit_missing_recover_blocked", ["-Flash", "-Recover", "-JLinkPath", "{MISSING_JLINK}"], "installed", 1, "explicit JLinkPath does not exist"),
    ]
    for name, args, discovery, expected, needle in discovery_cases:
        results.append(execute(pwsh, len(results) + 1, name, args, discovery=discovery,
                               expected=expected, needle=needle))
    runtime_version = subprocess.run([str(pwsh), "-NoProfile", "-Command", "$PSVersionTable.PSVersion.ToString()"], capture_output=True, text=True, check=True).stdout.strip()
    summary = {"driver_sha256": hashlib.sha256(DRIVER_BYTES).hexdigest(), "runtime": str(pwsh),
               "runtime_version": runtime_version, "platform": platform.platform(), "case_root": str(CASE_ROOT),
               "hardware_access": False,
               "scope": "PowerShell 7 Linux host logic with Python executable mocks; does not prove Windows 5.1, SWD, RTC, firmware or electrical behavior",
               "passed": sum(result["passed"] for result in results), "total": len(results),
               "results": results}
    (ROOT / "results.json").write_text(json.dumps(summary, indent=2) + "\n")
    for result in results:
        print(("PASS " if result["passed"] else "FAIL ") + result["name"] +
              (": " + "; ".join(result["issues"]) if result["issues"] else ""))
    print(f"{summary['passed']}/{summary['total']} passed; driver SHA-256 {summary['driver_sha256']}")
    return 0 if summary["passed"] == summary["total"] else 1


if __name__ == "__main__":
    sys.exit(main())
