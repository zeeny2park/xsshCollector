# xsshCollector - Embedded Linux On-Demand System Collector

A standalone, lightweight C99 collector designed for on-demand Linux target inspection.

## Key Features

- **Decoupled Architecture**: Zero Python, daemon, database, web server, or LLM requirements on target systems.
- **On-Demand Execution**: Gathers system state strictly upon invocation and exits immediately.
- **Portable & Minimal**: Pure C99 with standard POSIX libc dependencies.
- **Static Linking Support**: Builds zero-dependency standalone binaries for deployment on resource-constrained embedded devices.
- **Protocol Conformance**: Strictly complies with Glances Remote Inspection Protocol v1.0.

## Supported Operations

| Operation | Description | Target Files Read |
|---|---|---|
| `capabilities` | Reports supported operations, kernel version, and architecture | `uname()`, `gethostname()` |
| `summary` | System identification, uptime, CPU, memory, load averages | `/proc/version`, `/proc/uptime`, `/proc/stat`, `/proc/meminfo`, `/proc/loadavg` |
| `process_list` | Process table with CPU%, Memory%, threads, and state | `/proc/[pid]/stat`, `/proc/[pid]/status`, `/proc/[pid]/cmdline` |
| `process_details` | Detailed per-process statistics (IO, context switches, FDs, affinity) | `/proc/[pid]/io`, `/proc/[pid]/fd`, `/proc/[pid]/status` |

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
```

## Output & Diagnostics Contract

1. **`stdout`**: Exactly one valid JSON envelope per invocation (`status: "ok"` or `status: "error"`).
2. **`stderr`**: Diagnostic, error, and usage messages only. Never mix diagnostics into stdout JSON stream.
3. **Exit Codes**:
   - `0`: Success or structured protocol error emitted on stdout.
   - `1`: Invalid CLI arguments or syntax error.
