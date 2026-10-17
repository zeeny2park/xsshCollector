# Remote On-Demand System Inspection Protocol (v1.0)

This document defines the Version 1.0 JSON contract between the Glances external inspection server and embedded Linux remote collectors.

---

## 1. Architecture & Design Principles

```
+------------------+         +-------------------------------+
|  Browser Web UI  | <=====> |  Glances External Server      |
|  (Vue.js v5 UI)  |   HTTP  |  - Web UI & Orchestration     |
+------------------+         |  - Caching & Deduplication    |
                             |  - Glances Normalizer         |
                             |  - Virtual Plugin Adapter     |
                             +---------------+---------------+
                                             |
                               +-------------v---------------+
                               |  Remote Transport Adapter   |
                               |  - Multi-Hop SSH CLI / MCP  |
                               +-------------+---------------+
                                             | SSH Exec (on demand)
                               +-------------v---------------+
                               |  Embedded C/C++ Collector   |
                               |  - Lightweight, no Python   |
                               |  - Reads /proc and /sys     |
                               +-----------------------------+
```

1. **Decoupled Monitoring**: The external server manages web presentation, caching, request deduplication, and normalizes remote data into Glances data structures. The external server **never** collects local psutil metrics for remote targets.
2. **On-Demand Execution**: No daemon or long-running monitoring agent runs on the embedded target. The collector binary is invoked only when the user requests data (initial page load, section navigation, process inspection, explicit refresh).
3. **Strict Boundaries**: Browser users cannot execute arbitrary commands. Operations are restricted to a closed enum of inspection actions with validated arguments.
4. **Transport Independence**: The transport layer connects via multi-hop SSH CLI / MCP or deterministic mock transport without embedding credentials or secrets in frontend code.

---

## 2. Invocations & Wire Format

The embedded collector binary is invoked by the transport runner via SSH exec:
```bash
/usr/libexec/glances-collector --op <operation> [--pid <pid>] [--format json]
```
Alternatively, input may be piped via stdin as a JSON request:
```json
{
  "schema_version": "1.0",
  "operation": "process_details",
  "pid": 1042,
  "timestamp": 1776418800.0,
  "options": {}
}
```

The collector outputs a single JSON response to `stdout` and exits with code 0 on success (or non-zero on unrecoverable failure). Diagnostic logs, if any, are emitted exclusively to `stderr`.

---

## 3. Supported Operations

| Operation | Purpose | When Triggered |
|---|---|---|
| `summary` | Target OS info, uptime, CPU summary, memory summary, load average | Initial page load, periodic refresh |
| `process_list` | Process counts and tabular process list (CPU%, MEM%, RSS, threads) | Process view opened, periodic refresh |
| `process_details` | Extended process stats (IO, VMS, file descriptors, cwd, exe) | Process clicked / pinned in Web UI |
| `capabilities` | Collector version and supported metric sets | Target registration / discovery |

---

## 4. Response Envelope Specification

Every collector response adheres to the following envelope:

```json
{
  "schema_version": "1.0",
  "collector_version": "1.0.0",
  "target_id": "target-board-01",
  "target_hostname": "embedded-linux",
  "operation": "summary",
  "timestamp": 1776418800.123,
  "success": true,
  "error": null,
  "capabilities": ["summary", "process_list", "process_details", "io", "fd"],
  "payload": {}
}
```

### Top-level Fields

- `schema_version` (string, required): Must be `"1.0"`.
- `collector_version` (string, required): Semantic version of the embedded collector binary.
- `target_id` (string, optional): Unique logical identifier assigned to the target.
- `target_hostname` (string, optional): Hostname reported by the target kernel.
- `operation` (string, required): One of `["summary", "process_list", "process_details", "capabilities"]`.
- `timestamp` (float, required): Epoch timestamp in seconds (UTC) when metrics were gathered.
- `success` (boolean, required): `true` if collection succeeded, `false` otherwise.
- `error` (object or null): Structured error details when `success` is `false`.
- `capabilities` (list of strings, optional): Features supported on this target/kernel.
- `payload` (object, required): Operation-specific metric data (empty dictionary on error).

---

## 5. Structured Error Codes

When `success` is `false`, the `error` object contains:

```json
{
  "code": "PROCESS_NOT_FOUND",
  "message": "Process 1042 does not exist or has exited",
  "details": {
    "pid": 1042,
    "last_seen_epoch": 1776418790.0
  }
}
```

Standard error codes:
- `UNKNOWN`: Unclassified collector error.
- `INVALID_REQUEST`: Malformed request parameters or invalid PID.
- `UNSUPPORTED_OPERATION`: Requested operation is not implemented by collector.
- `PROCESS_NOT_FOUND`: Target PID exited or does not exist.
- `PID_REUSED`: Detected PID starttime mismatch (PID recycled by kernel).
- `PERMISSION_DENIED`: Lacking privileges (e.g. reading `/proc/[pid]/fd` or `/proc/[pid]/io`).
- `KERNEL_UNSUPPORTED`: Required `/proc` or `/sys` interface is unavailable.
- `TIMEOUT`: Collection execution exceeded target time budget.
- `TRANSPORT_ERROR`: Transport connection lost or command failed to launch.
- `RESPONSE_TOO_LARGE`: Output exceeded maximum allowed size ceiling (5 MB).

---

## 6. Tri-State Field Semantics

A critical requirement is distinguishing **unsupported / unavailable** metrics from **genuine zero values**:

- **Omitted or `null`**: The field is not supported by the kernel configuration, or could not be read due to permissions (e.g. non-root access to `/proc/[pid]/io`). Glances UI displays this as `N/A` or `-`, avoiding misleading zero alarms.
- **`0` or `0.0`**: Genuine measured zero (e.g., zero zombie processes, 0.0% CPU usage, 0 read bytes).
- **Positive number**: Measured non-zero value.

---

## 7. Operation Payloads & Examples

### 7.1 `summary` Operation

Collects basic machine identification, uptime, CPU, memory, and load average from `/proc/version`, `/proc/uptime`, `/proc/stat`, `/proc/meminfo`, and `/proc/loadavg`.

#### Response Example
```json
{
  "schema_version": "1.0",
  "collector_version": "1.0.0",
  "target_id": "embedded-gateway-01",
  "target_hostname": "edge-node-arm64",
  "operation": "summary",
  "timestamp": 1776418800.0,
  "success": true,
  "error": null,
  "capabilities": ["summary", "process_list", "process_details"],
  "payload": {
    "system": {
      "hostname": "edge-node-arm64",
      "os_name": "Linux",
      "os_version": "6.6.21-yocto-standard",
      "kernel_version": "6.6.21",
      "arch": "aarch64"
    },
    "uptime": {
      "uptime_seconds": 864200.5
    },
    "cpu": {
      "total": 14.5,
      "user": 8.2,
      "system": 5.1,
      "idle": 85.5,
      "iowait": 1.2,
      "cpucore": 4
    },
    "mem": {
      "total": 2147483648,
      "used": 629145600,
      "free": 1518338048,
      "percent": 29.3
    },
    "load": {
      "min1": 0.42,
      "min5": 0.35,
      "min15": 0.28,
      "cpucore": 4
    }
  }
}
```

### 7.2 `process_list` Operation

Collects aggregate process state counts and list of active processes by reading `/proc/[pid]/stat`, `/proc/[pid]/status`, and `/proc/[pid]/cmdline`.

#### Response Example
```json
{
  "schema_version": "1.0",
  "collector_version": "1.0.0",
  "target_id": "embedded-gateway-01",
  "target_hostname": "edge-node-arm64",
  "operation": "process_list",
  "timestamp": 1776418805.0,
  "success": true,
  "error": null,
  "capabilities": ["summary", "process_list", "process_details"],
  "payload": {
    "count": {
      "total": 65,
      "running": 2,
      "sleeping": 63,
      "stopped": 0,
      "zombie": 0
    },
    "processes": [
      {
        "pid": 1,
        "name": "systemd",
        "cmdline": "/lib/systemd/systemd --system",
        "status": "sleeping",
        "username": "root",
        "cpu_percent": 0.1,
        "memory_percent": 0.4,
        "memory_rss": 8388608,
        "num_threads": 1,
        "created_at": 1776332380.0
      },
      {
        "pid": 342,
        "name": "firmware-app",
        "cmdline": "/usr/bin/firmware-app -c /etc/app.conf",
        "status": "running",
        "username": "appuser",
        "cpu_percent": 12.4,
        "memory_percent": 4.5,
        "memory_rss": 96665600,
        "num_threads": 8,
        "created_at": 1776332410.0
      }
    ]
  }
}
```

### 7.3 `process_details` Operation

Collects extended metrics for a single PID on demand. Triggered in Glances Web UI when clicking on a process row.

#### Response Example
```json
{
  "schema_version": "1.0",
  "collector_version": "1.0.0",
  "target_id": "embedded-gateway-01",
  "target_hostname": "edge-node-arm64",
  "operation": "process_details",
  "timestamp": 1776418810.0,
  "success": true,
  "error": null,
  "capabilities": ["summary", "process_list", "process_details", "io", "fd"],
  "payload": {
    "pid": 342,
    "name": "firmware-app",
    "cmdline": "/usr/bin/firmware-app -c /etc/app.conf",
    "status": "running",
    "username": "appuser",
    "cpu_percent": 12.4,
    "memory_percent": 4.5,
    "memory_rss": 96665600,
    "memory_vms": 268435456,
    "num_threads": 8,
    "created_at": 1776332410.0,
    "io_read_bytes": 104857600,
    "io_write_bytes": 20971520,
    "open_files_count": 14,
    "cwd": "/var/run/app",
    "exe": "/usr/bin/firmware-app"
  }
}
```

---

## 8. Embedded Linux Implementation Guidelines (/proc & /sys)

When implementing the lightweight C or C++ collector on embedded targets:

1. **CPU Statistics**:
   - Sample `/proc/stat` total and idle counters across a short delta (or compute delta against previous boot-relative ticks).
2. **Memory Statistics**:
   - Read `/proc/meminfo`. `MemTotal`, `MemFree`, `MemAvailable`, `Buffers`, `Cached`.
   - `used = MemTotal - MemAvailable` (or `MemFree + Buffers + Cached` on older kernels).
3. **Process Listing**:
   - Iterate `/proc` directory entries matching numeric PIDs.
   - Read `/proc/[pid]/stat`: fields `(pid)`, `(comm)`, `state`, `utime`, `stime`, `num_threads`, `starttime`, `vsize`, `rss`.
   - Read `/proc/[pid]/status`: `Uid:` to resolve username via `getpwuid()`.
   - Read `/proc/[pid]/cmdline`: delimited by `\0`, replace with spaces. If empty, fall back to `comm` in brackets (kernel thread).
4. **PID Reuse & Disappearance Handling**:
   - If `/proc/[pid]` does not exist when opening, return `PROCESS_NOT_FOUND`.
   - Record `starttime` from field 22 of `/proc/[pid]/stat` as `created_at` to detect recycled PIDs.
5. **Permissions & Optional Details**:
   - `/proc/[pid]/io` requires root or `ptrace_attach` permissions. If `EACCES`, return `io_read_bytes: null` and `io_write_bytes: null` rather than failing the entire request.
   - `/proc/[pid]/fd` requires reading directory count. If `EACCES`, return `open_files_count: null`.
6. **No External Dependencies**:
   - Implement with standard C11/POSIX or C++17 without heavy dependencies. Statically link libc (e.g. musl or glibc) to ensure portability across embedded target root filesystems.
