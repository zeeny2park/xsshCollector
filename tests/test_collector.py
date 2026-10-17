#
# xsshCollector - Standalone Embedded Linux On-Demand Collector
#
# Tests for Protocol v1.0 Conformance and Native C Collector Binary
#

from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path
from typing import Any, Dict

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent
BINARY_PATH = REPO_ROOT / "build" / "glances-collector"
STATIC_BINARY_PATH = REPO_ROOT / "build" / "glances-collector-static"


@pytest.fixture(scope="session", autouse=True)
def build_collector():
    """Ensure collector binaries are built before running tests."""
    subprocess.run(["make", "-C", str(REPO_ROOT), "all", "static"], check=True, capture_output=True)
    assert BINARY_PATH.exists(), f"Binary not found: {BINARY_PATH}"
    assert STATIC_BINARY_PATH.exists(), f"Static binary not found: {STATIC_BINARY_PATH}"


def run_collector(*args: str) -> subprocess.CompletedProcess[str]:
    """Invoke the native C collector binary with given CLI arguments."""
    return subprocess.run(
        [str(BINARY_PATH), *args],
        capture_output=True,
        text=True,
        timeout=10,
    )


def parse_and_validate(stdout: str) -> Dict[str, Any]:
    """Validate and parse a Protocol v1.0 JSON response envelope."""
    data = json.loads(stdout)
    assert data.get("protocol_version") == "1.0"
    assert data.get("status") in ("ok", "error")
    assert "operation" in data
    assert "timestamp" in data
    assert "collector_version" in data
    assert isinstance(data.get("capabilities"), list)
    return data


# ===================================================================
# Protocol v1.0 Conformance Tests
# ===================================================================


def test_collector_capabilities():
    """Test 'capabilities' operation output conforming to Protocol v1.0."""
    res = run_collector("--op", "capabilities", "--target", "test-board-01")
    assert res.returncode == 0
    assert res.stdout.strip().startswith("{")
    assert res.stdout.strip().endswith("}")

    parsed = parse_and_validate(res.stdout)
    assert parsed["status"] == "ok"
    assert parsed["protocol_version"] == "1.0"
    assert parsed["collector_version"] == "1.0.0"
    assert parsed["operation"] == "capabilities"
    assert parsed["target_id"] == "test-board-01"
    assert "capabilities" in parsed["capabilities"]
    assert "summary" in parsed["capabilities"]
    assert "process_list" in parsed["capabilities"]
    assert "process_details" in parsed["capabilities"]

    target_info = parsed["data"]["target_info"]
    assert target_info["os"] == "Linux"
    assert "kernel" in target_info
    assert "arch" in target_info


def test_collector_summary():
    """Test 'summary' operation output conforming to Protocol v1.0."""
    res = run_collector("--op", "summary", "--target", "test-board-01")
    assert res.returncode == 0

    parsed = parse_and_validate(res.stdout)
    assert parsed["status"] == "ok"
    assert parsed["operation"] == "summary"

    data = parsed["data"]
    assert "hostname" in data
    assert data["os_name"] == "Linux"
    assert data["platform"] in ("64bit", "32bit")
    assert isinstance(data["uptime"], int)
    assert data["uptime"] >= 0

    cpu = data["cpu"]
    assert 0.0 <= cpu["total"] <= 100.0
    assert 0.0 <= cpu["idle"] <= 100.0
    assert cpu["cpucore"] >= 1

    mem = data["memory"]
    assert mem["total"] > 0
    assert mem["available"] > 0
    assert 0.0 <= mem["percent"] <= 100.0

    load = data["load"]
    assert load["min1"] >= 0.0
    assert load["min5"] >= 0.0
    assert load["min15"] >= 0.0


def test_collector_process_list():
    """Test 'process_list' operation returns valid processes."""
    res = run_collector("--op", "process_list", "--target", "test-board-01")
    assert res.returncode == 0

    parsed = parse_and_validate(res.stdout)
    assert parsed["status"] == "ok"
    assert parsed["operation"] == "process_list"

    data = parsed["data"]
    assert "processes" in data
    processes = data["processes"]
    assert isinstance(processes, list)
    assert len(processes) > 0

    my_pid = os.getpid()
    pids = [p["pid"] for p in processes]
    assert my_pid in pids or 1 in pids

    first = processes[0]
    assert isinstance(first["pid"], int)
    assert isinstance(first["name"], str)
    assert isinstance(first["username"], str)
    assert first["status"] in ("R", "S", "D", "Z", "T")
    assert isinstance(first["cmdline"], list)
    assert 0.0 <= first["memory_percent"] <= 100.0


def test_collector_process_details_current_process():
    """Test 'process_details' operation on the test runner process itself."""
    my_pid = os.getpid()
    res = run_collector("--op", "process_details", "--pid", str(my_pid), "--target", "test-board-01")
    assert res.returncode == 0

    parsed = parse_and_validate(res.stdout)
    assert parsed["status"] == "ok"
    assert parsed["operation"] == "process_details"

    details = parsed["data"]
    assert details["pid"] == my_pid
    assert "pytest" in details["name"] or "python" in details["name"]
    assert details["memory_info"]["rss"] > 0
    assert details["memory_info"]["vms"] > 0
    assert details["memory_swap"] == 0  # Measured zero, not None
    assert details["num_handles"] is None  # Unsupported on Linux, not 0
    assert isinstance(details["cpu_affinity"], list)
    assert len(details["cpu_affinity"]) >= 1


def test_collector_process_details_root_process():
    """Test 'process_details' operation on PID 1 (init/systemd)."""
    res = run_collector("--op", "process_details", "--pid", "1", "--target", "test-board-01")
    assert res.returncode == 0

    parsed = parse_and_validate(res.stdout)
    assert parsed["status"] == "ok"
    assert parsed["data"]["pid"] == 1
    assert parsed["data"]["num_handles"] is None


def test_collector_nonexistent_pid():
    """Test structured error response when requesting a non-existent PID."""
    bogus_pid = 9999999
    res = run_collector("--op", "process_details", "--pid", str(bogus_pid), "--target", "test-board-01")
    assert res.returncode == 0

    parsed = parse_and_validate(res.stdout)
    assert parsed["status"] == "error"
    assert parsed["error"]["code"] == "PROCESS_NOT_FOUND"
    assert "not found" in parsed["error"]["message"].lower()


def test_collector_missing_pid():
    """Test error handling when process_details is requested without --pid."""
    res = run_collector("--op", "process_details")
    assert res.returncode != 0
    parsed = parse_and_validate(res.stdout)
    assert parsed["status"] == "error"
    assert parsed["error"]["code"] == "INVALID_REQUEST"


def test_collector_invalid_operation():
    """Test structured error response for unsupported operations."""
    res = run_collector("--op", "arbitrary_shell_command")
    assert res.returncode != 0

    parsed = parse_and_validate(res.stdout)
    assert parsed["status"] == "error"
    assert parsed["error"]["code"] == "UNSUPPORTED_OPERATION"


def test_collector_stdout_purity():
    """Ensure that only pure JSON is emitted on stdout (diagnostics on stderr)."""
    res = run_collector("--op", "summary")
    assert res.returncode == 0
    stdout_trimmed = res.stdout.strip()
    assert stdout_trimmed.startswith("{")
    assert stdout_trimmed.endswith("}")
    json.loads(stdout_trimmed)


def test_collector_static_binary():
    """Verify the statically linked binary executes and produces valid JSON."""
    res = subprocess.run(
        [str(STATIC_BINARY_PATH), "--op", "summary", "--target", "static-target"],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert res.returncode == 0
    parsed = parse_and_validate(res.stdout)
    assert parsed["status"] == "ok"
    assert parsed["target_id"] == "static-target"
