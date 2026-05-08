# Hospital-Patient-Triage-Bed-Allocator
---
A multi-process, multi-threaded **Hospital Patient Triage & Bed Allocation System** built in C and Bash, developed as an Operating Systems lab project. It simulates real hospital admissions using core OS concepts: shared memory, named pipes (FIFOs), POSIX semaphores, pthreads, `fork`/`exec`, and memory allocation strategies.

---

## Features

- **Priority-based triage** — patients are scored by severity (1–10) and mapped to a priority level (1–5)
- **Three bed types** — ICU (4 beds), Isolation (4 beds), General (12 beds)
- **Pluggable memory allocation strategies** — Best-Fit, First-Fit, Worst-Fit
- **Memory coalescing** — adjacent free partitions of the same type are merged on discharge
- **Fragmentation logging** — external and internal fragmentation are tracked and logged
- **Scheduling simulations** — FCFS and Priority scheduling stats (wait time, turnaround) written to log on shutdown
- **Multi-threaded admissions** — separate threads for Receptionist, Scheduler, Discharge Reader, and Nurses (one per ward type)
- **Process per patient** — each admitted patient runs as a forked child process via `patient_simulator`

---

## Project Structure

```
hospital_project/
├── src/
│   ├── hospital.h            # Shared constants, structs (PatientRecord, BedPartition, SharedMemory)
│   ├── admissions.c          # Main admissions manager — threads, IPC, bed allocation, scheduling
│   └── patient_simulator.c   # Child process simulating patient treatment and discharge
├── scripts/
│   ├── triage.sh             # Validate and submit a patient to the admissions FIFO
│   ├── start_hospital.sh     # Initialize IPC resources and launch admissions
│   └── stop_hospital.sh      # Gracefully shut down the hospital and clean all IPC
├── logs/
│   ├── schedule_log.txt      # Admit events + FCFS/Priority scheduling simulation output
│   ├── memory_log.txt        # Fragmentation snapshots on each admit/discharge
│   └── full_output.txt       # Sample console output from a full run
└── Makefile
```

---

## IPC Mechanisms Used

| Mechanism | Purpose |
|---|---|
| Shared Memory (`shmget`/`shmat`) | Ward state — bed partitions and patient assignments shared between processes |
| Named FIFO `/tmp/triage_fifo` | Triage script → Receptionist thread |
| Named FIFO `/tmp/discharge_fifo` | Patient simulator process → Discharge reader thread |
| POSIX Semaphores | Limit concurrent ICU and Isolation admissions |
| Anonymous Pipe | Pass patient name from parent to forked `patient_simulator` child |
| `pthread` mutex + cond vars | Synchronise queue and bed access between threads |
| `SIGTERM` / `SIGCHLD` | Graceful shutdown; reap child processes |

---

## Build

```bash
make
```

Produces two binaries: `admissions` and `patient_simulator`.

---

## Usage

### Start the hospital

```bash
# Default: Best-Fit strategy
./scripts/start_hospital.sh best

# Or choose a strategy: best | first | worst
./scripts/start_hospital.sh first
./scripts/start_hospital.sh worst
```

### Admit a patient

```bash
./scripts/triage.sh <name> <age> <severity 1-10>

# Examples
./scripts/triage.sh Alice 25 2    # Severity 2 → Priority 1 → ICU
./scripts/triage.sh Bob   40 6    # Severity 6 → Priority 3 → Isolation
./scripts/triage.sh Sara  30 9    # Severity 9 → Priority 5 → General
```

### Stop the hospital

```bash
./scripts/stop_hospital.sh
```

On shutdown, scheduling simulations (FCFS and Priority) are written to `logs/schedule_log.txt`.

---

## Priority & Bed Mapping

| Severity | Priority | Bed Type | Care Units |
|---|---|---|---|
| 1–2 | 1 | ICU | 3 |
| 3–4 | 2 | ICU | 3 |
| 5–6 | 3 | Isolation | 2 |
| 7–8 | 4 | General | 1 |
| 9–10 | 5 | General | 1 |

---

## Memory Allocation Strategies

| Strategy | Description |
|---|---|
| **Best-Fit** | Allocates the smallest partition that fits (minimises wasted space) |
| **First-Fit** | Allocates the first partition found that fits |
| **Worst-Fit** | Allocates the largest available partition |

Adjacent free partitions of the same bed type are **coalesced** automatically on discharge to reduce external fragmentation.

---

## Logs

| File | Contents |
|---|---|
| `logs/schedule_log.txt` | Admit timestamps, FCFS simulation, Priority simulation |
| `logs/memory_log.txt` | Free units, largest contiguous block, fragmentation % at each event |

---

## Quick Test

```bash
make test
```

Starts the hospital in Best-Fit mode and admits Alice (ICU), Bob (General), and Sara (General) automatically.

---

## Cleanup

```bash
make clean
```

Removes binaries, FIFOs, PID file, and the shared memory segment.

---

## Author
Ayesha Rauf 23F-0807
Aqsa Ishaq  23F-0839

---

## My Contribution
I implemented:

start_hospital.sh
stop_hospital.sh
admissions.c — fork/exec
admissions.c — threads
admissions.c — mutex/condvars
admissions.c — scheduling
Memory allocator Best-Fit
Fragmentation logging

