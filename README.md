# xsshCollector - Embedded Linux On-Demand System Collector

A standalone, lightweight C99 collector designed for on-demand Linux target inspection.

## Key Features

- **Decoupled Architecture**: Zero Python, daemon, database, web server, or LLM requirements on target systems.
- **On-Demand Execution**: Gathers system state strictly upon invocation and exits immediately.
- **Portable & Minimal**: Pure C99 with standard POSIX libc dependencies.
- **Static Linking Support**: Builds zero-dependency standalone binaries for deployment on resource-constrained embedded devices.
- **Protocol Conformance**: Strictly complies with Glances Remote Inspection Protocol v1.0.
- **Generic Command Execution**: Extensible `--op exec` interface allowing arbitrary target command collection without altering collector binaries.

## Supported Operations

| Operation | Description | Target Data / Mechanism |
|---|---|---|
| `capabilities` | Reports supported operations, kernel version, and architecture | `uname()`, `gethostname()` |
| `summary` | System identification, uptime, CPU, memory, load averages | `/proc/version`, `/proc/uptime`, `/proc/stat`, `/proc/meminfo`, `/proc/loadavg` |
| `process_list` | Process table with CPU%, Memory%, threads, and state | `/proc/[pid]/stat`, `/proc/[pid]/status`, `/proc/[pid]/cmdline` |
| `process_details` | Detailed per-process statistics (IO, context switches, FDs, affinity) | `/proc/[pid]/io`, `/proc/[pid]/fd`, `/proc/[pid]/status` |
| `exec` | Executes arbitrary command and returns stdout, stderr, exit code, and duration | Target shell execution (`/bin/sh -c`) via non-blocking pipes |

## Building the Collector

### Native Build (Host Architecture)
```bash
make all
# Output: build/glances-collector
```

### Static Linking (Self-Contained Binary)
Produces a binary with libc statically linked, requiring no shared libraries on the target filesystem:
```bash
make static
# Output: build/glances-collector-static
```

### Probing Toolchains
Inspects host compilers, static libc support, and available cross-compilers:
```bash
make probe
```

### Cross-Compilation (ARM / AArch64)
If cross-compilers (`aarch64-linux-gnu-gcc` or `arm-linux-gnueabihf-gcc`) are installed:
```bash
# For 64-bit ARM (AArch64)
make cross-aarch64

# For 32-bit ARM (ARMhf)
make cross-arm

# Custom toolchain prefix
CROSS_COMPILE=arm-v7a-linux-gnueabihf- make all
```

## CLI Usage

```bash
# System Capabilities
glances-collector --op capabilities [--target <target_id>]

# System Summary
glances-collector --op summary [--target <target_id>]

# Process Listing
glances-collector --op process_list [--target <target_id>]

# Process Details (requires --pid)
glances-collector --op process_details --pid 1234 [--target <target_id>]

# Generic Command Execution (requires --cmd)
glances-collector --op exec --cmd "df -h" [--target <target_id>]
glances-collector --op exec --cmd "vcgencmd measure_temp" [--target <target_id>]
glances-collector --op exec --cmd "dmesg --level=err,warn | tail -n 10" [--target <target_id>]
```

## Output & Diagnostics Contract

1. **`stdout`**: Exactly one valid JSON envelope per invocation (`status: "ok"` or `status: "error"`).
2. **`stderr`**: Diagnostic, error, and usage messages only. Never mix diagnostics into stdout JSON stream.
3. **Exit Codes**:
   - `0`: Success or structured protocol error emitted on stdout.
   - `1`: Invalid CLI arguments or syntax error.

---

## Generic Command Execution (`--op exec`)

The generic execution interface enables on-demand collection of arbitrary target commands, turning `glances-collector` into a universal metric & telemetry probe.

### C Collector Implementation Details
- **Non-blocking Pipe Isolation**: Uses two separate pipes (`pipe()`) with `O_NONBLOCK` and `select()` multiplexing to concurrently read `stdout` and `stderr` without deadlocks.
- **Bounded Buffers**: Stdout is capped at 1 MB and stderr at 64 KB to protect embedded targets from memory exhaustion.
- **Precision Timing**: Measures execution time using `clock_gettime(CLOCK_MONOTONIC)` in milliseconds (`duration_ms`).
- **Structured Exit Status**: Captures child exit status via `waitpid()` and `WEXITSTATUS()`.

### Wire Format JSON Envelope
```json
{
  "protocol_version": "1.0",
  "status": "ok",
  "operation": "exec",
  "target_id": "embedded-target-01",
  "timestamp": 1792289886.172,
  "collector_version": "1.0.0",
  "capabilities": ["capabilities", "summary", "process_list", "process_details", "exec"],
  "data": {
    "command": "vcgencmd measure_temp",
    "exit_code": 0,
    "stdout": "temp=42.8'C\n",
    "stderr": "",
    "duration_ms": 1.24
  }
}
```

---

## Web UI Dynamic Component Integration Specification

This section describes the end-to-end architecture for extending the Web Server and Web UI to dynamically ingest, parse, and render metrics defined purely through declarative JSON specifications.

### 1. Architecture & Data Flow

```
+---------------------------------------------------------------------------------+
| Embedded Target (Remote)                                                        |
|   glances-collector --op exec --cmd "<command>"                                 |
+---------------------------------------+-----------------------------------------+
                                        | JSON Envelope (stdout, exit_code, duration_ms)
                                        v
+---------------------------------------------------------------------------------+
| External Web Server (Glances Backend)                                           |
|   1. Transport Layer : Invokes collector via multi-hop SSH or local runner     |
|   2. Parser Engine   : Parses output using regex, table, json, or kv           |
|                        * If no parser specified, passes raw data directly       |
|   3. REST API Router : Serves component definitions & polled metric data       |
+---------------------------------------+-----------------------------------------+
                                        | REST API (/api/5/custom/...)
                                        v
+---------------------------------------------------------------------------------+
| Browser Web UI (Vue 3 / HTML)                                                   |
|   Schema-Driven DynamicComponent.vue                                            |
|   - RawView        : Preformatted monospace terminal box for unparsed logs      |
|   - TableView      : Data table for tabular command outputs                     |
|   - MetricCardView : Single metric card with warning/critical threshold colors  |
|   - KeyValueView   : Two-column key/value grid                                  |
+---------------------------------------------------------------------------------+
```

### 2. Declarative Component Definition (4 Core User Requirements)

Users define new web components by creating a JSON configuration file (e.g. `components.json`). No recompilation of the frontend or backend is required.

Each component definition consists of four core aspects:
1. **Collection Method (`collector.command`)**: The shell command to execute on the target.
2. **Collection Cadence (`collector.interval`)**: Polling / refresh frequency in seconds.
3. **Result Parsing (`parser`)**:
   - `null` or omitted: **No parsing applied. Raw `stdout` data is forwarded directly to the UI.**
   - `json`: Parses JSON output string into structured objects.
   - `regex`: Extracts fields using named capture groups `(?P<field>pattern)`.
   - `table`: Splits tabular output by delimiter into headers and rows.
   - `key_value`: Splits lines by delimiter (e.g. `:`) into dictionary pairs.
4. **Web UI Presentation (`ui`)**:
   - `raw`: Renders in a scrollable, monospace preformatted box.
   - `table`: Renders tabular columns with custom labels.
   - `metric_card`: Highlights a primary numeric value, unit, and status thresholds.
   - `key_value`: Renders label-value grid.

### 3. Server-Side Requirements (`GenericParser`)

The Web Server implements a `GenericParser` with a mandatory fallback to raw data:

```python
class GenericParser:
    @classmethod
    def parse(cls, stdout: str, parser_spec: dict | None) -> dict:
        # Fallback to raw data if parser is absent or undefined
        if not parser_spec or not parser_spec.get("type"):
            return {"parsed": False, "raw": stdout, "format": "raw"}

        p_type = parser_spec.get("type", "").lower()
        try:
            if p_type == "json":
                return {"parsed": True, "data": json.loads(stdout.strip()), "raw": stdout}
            elif p_type == "regex":
                m = re.search(parser_spec["pattern"], stdout)
                return {"parsed": True, "data": m.groupdict() if m else {}, "raw": stdout}
            elif p_type == "table":
                lines = [l.strip() for l in stdout.strip().splitlines() if l.strip()]
                delim = parser_spec.get("delimiter", r"\s+")
                headers = re.split(delim, lines[0])
                rows = [dict(zip(headers, re.split(delim, l, maxsplit=len(headers)-1))) for l in lines[1:]]
                return {"parsed": True, "data": rows, "raw": stdout}
            elif p_type == "key_value":
                sep = parser_spec.get("separator", ":")
                return {"parsed": True, "data": dict(l.split(sep, 1) for l in stdout.splitlines() if sep in l), "raw": stdout}
        except Exception as exc:
            # On parse failure, gracefully fallback to raw data with warning
            return {"parsed": False, "raw": stdout, "warning": f"Parsing failed: {exc}", "format": "raw"}
        
        return {"parsed": False, "raw": stdout, "format": "raw"}
```

#### Server Endpoints
- `GET /api/5/custom/components`: Returns all registered component specifications.
- `GET /api/5/custom/data/{id}`: Returns the latest collected and parsed data for a component.

### 4. Web UI Dynamic Component Architecture (Vue 3)

The frontend registers a generic `DynamicComponent.vue` dispatcher that selects the appropriate view based on `spec.ui.type` and data freshness.

#### Automatic Raw Fallback Rule
If `payload.parsed === false` or `spec.ui.type === 'raw'`, the UI automatically renders `RawView.vue`:

```vue
<!-- RawView.vue: Default view when no parsing is specified -->
<template>
  <div class="gl-raw-container">
    <div v-if="payload.warning" class="alert alert-warning py-1 small">{{ payload.warning }}</div>
    <pre class="gl-terminal-box"><code>{{ payload.raw || payload.stdout }}</code></pre>
  </div>
</template>

<style scoped>
.gl-terminal-box {
  background: #1e1e1e;
  color: #d4d4d4;
  font-family: monospace;
  font-size: 0.85rem;
  padding: 8px;
  border-radius: 4px;
  max-height: 250px;
  overflow-y: auto;
  white-space: pre-wrap;
  word-break: break-all;
}
</style>
```

---

## Component Configuration Examples

### Example 1: Raw Output (Kernel Warning & Error Logs)
*No parsing specified — rendered directly as raw terminal output.*

```json
{
  "id": "kernel_errors",
  "title": "Kernel Errors & Warnings",
  "slot": "left",
  "collector": {
    "method": "command",
    "command": "dmesg --level=err,warn | tail -n 10",
    "interval": 30
  },
  "parser": null,
  "ui": {
    "type": "raw",
    "max_lines": 10
  }
}
```

### Example 2: Tabular Output (Disk Partitions)
*Tabular parsing with whitespace delimiter — rendered as a styled data table.*

```json
{
  "id": "disk_partitions",
  "title": "Disk Partitions (df -h)",
  "slot": "right",
  "collector": {
    "method": "command",
    "command": "df -h -T -x tmpfs -x devtmpfs",
    "interval": 60
  },
  "parser": {
    "type": "table",
    "delimiter": "\\s+"
  },
  "ui": {
    "type": "table",
    "columns": [
      { "key": "Filesystem", "label": "Device" },
      { "key": "Type", "label": "FS Type" },
      { "key": "Size", "label": "Total" },
      { "key": "Used", "label": "Used" },
      { "key": "Avail", "label": "Free" },
      { "key": "Use%", "label": "Usage" },
      { "key": "Mounted", "label": "Mount Point" }
    ]
  }
}
```

### Example 3: Regex Metric Card (SoC Temperature)
*Named regex capture with threshold styling — rendered as a live status card.*

```json
{
  "id": "soc_temp",
  "title": "SoC Core Temperature",
  "slot": "top",
  "collector": {
    "method": "command",
    "command": "vcgencmd measure_temp",
    "interval": 5
  },
  "parser": {
    "type": "regex",
    "pattern": "temp=(?P<temp>[0-9.]*)'C"
  },
  "ui": {
    "type": "metric_card",
    "field": "temp",
    "unit": "°C",
    "thresholds": {
      "warning": 70.0,
      "critical": 80.0
    }
  }
}
```
