# ParaAFL — Parallel Master AFL

> An enhanced version of AFL (American Fuzzy Lop) 2.57b that adds **parallel deterministic fuzzing across multiple Master instances** and an **automatic corpus pruning module** to address performance bottlenecks in multi-instance fuzzing.

---

## Table of Contents

- [Description](#description)
- [Key Features](#key-features)
- [Methodology](#methodology)
- [Code Information](#code-information)
- [Dataset Information](#dataset-information)
- [Requirements](#requirements)
- [Usage Instructions](#usage-instructions)
- [Demo Walkthrough](#demo-walkthrough)
- [Citations](#citations)
- [License](#license)
- [Contribution Guidelines](#contribution-guidelines)

---

## Description

ParaAFL is a fork of the well-known coverage-guided fuzzer [AFL](https://github.com/google/AFL)
(version 2.57b). AFL discovers software bugs by mutating program inputs and using lightweight
instrumentation to track which inputs reach new code paths.

In AFL's standard parallel (`-M` / `-S`) setup, multiple instances cooperate by *splitting the
input queue*, but every Master instance still re-runs the **entire** deterministic mutation
sequence for the inputs it owns. This duplicates a large amount of work and limits the speedup
gained from running more instances.

ParaAFL introduces two improvements over the upstream behaviour:

1. **Parallel Master Deterministic Fuzzing** — the deterministic mutation *operations themselves*
   (bitflips, arithmetic, interesting-value, and dictionary stages) are partitioned across Master
   instances, so each Master only executes its assigned slice of the work.
2. **Corpus Pruning** — after each queue cycle, redundant test cases (those producing duplicate
   execution traces) are detected across all instances and reported, reducing corpus bloat.

Single-Master usage is fully backward compatible: with one Master the tool behaves identically to
the original AFL.

---

## Key Features

- **Multi-Master Parallelism** — deterministic stages are divided by flip ranges among multiple Master processes.
- **Corpus Pruning** — trace-based deduplication scanned across every fuzzer instance in the sync directory.
- **Backward Compatible** — single-Master mode (`-M name:1/1`) behaves identically to original AFL.
- **AFL Compatible** — works with existing AFL instrumentation, seeds, dictionaries, and workflow.

---

## Methodology

### Improvement 1 — Parallel Master Deterministic Fuzzing

Upstream AFL's `-M` mode splits *queue items* using `exec_cksum % master_max`, but each Master
still runs the full deterministic mutation sequence. ParaAFL instead splits the deterministic
mutation *operations* using a flip-range partition:

```
FlipRange(i, N) = [ (i * Max_Flips) / N , ((i + 1) * Max_Flips) / N )
```

Where:

- `N` = total number of Master processes (`master_max`)
- `i` = current Master index (0-based; AFL's `master_id` is 1-based and converted internally)
- `Max_Flips` = total number of flip operations for the current input

Each Master executes only the flip operations that fall inside its assigned `[start, end)` range,
so the deterministic stages run in true parallel rather than being repeated by every Master.
The computation lives in `calculate_flip_range()` (`afl-fuzz.c`), and each deterministic stage
(`FLIP_BIT`, arithmetic, interest, dictionary, …) is gated by the resulting range when `master_max > 1`.

### Improvement 2 — Corpus Pruning

After each queue cycle (controlled by `PRUNE_INTERVAL`), ParaAFL:

1. Scans the queue of every fuzzer instance under the shared sync directory.
2. Re-executes each test case to obtain its execution-trace checksum (`get_file_trace()`).
3. Detects redundant test cases whose traces duplicate an already-seen trace
   (`find_or_update_trace()`).
4. Records kept vs. removed counts to a per-instance prune log for analysis.

This reduces effective corpus size and avoids spending cycles on inputs that exercise the same paths.
The logic lives in `prune_corpus()` (`afl-fuzz.c`) and is triggered from the main fuzzing loop when
`queue_cycle % prune_interval == 0`.

---

## Code Information

ParaAFL keeps the full AFL toolchain. The components most relevant to this fork:

| Path | Description |
| --- | --- |
| `afl-fuzz.c` | Core fuzzer. Contains the ParaAFL additions: `calculate_flip_range()`, the per-stage flip-range gating, and `prune_corpus()` / `find_or_update_trace()` / `get_file_trace()`. |
| `config.h` | Build-time constants. Adds `#define PRUNE_INTERVAL 1` (queue-cycle interval for corpus pruning). |
| `afl-gcc.c`, `afl-as.c`, `afl-as.h` | The instrumenting compiler wrapper and assembler used to build instrumented targets. |
| `afl-showmap.c`, `afl-tmin.c`, `afl-cmin`, `afl-analyze.c` | Standard AFL utilities (trace mapping, test-case minimisation, corpus minimisation, input analysis). |
| `llvm_mode/` | Optional LLVM/clang instrumentation mode. |
| `qemu_mode/` | Optional QEMU black-box instrumentation mode (for binaries without source). |
| `libdislocator/`, `libtokencap/` | Optional helper libraries (heap allocator for fault detection, token capture). |
| `target_demo/` | Self-contained demo target and seeds for trying ParaAFL out (see below). |
| `docs/` | Upstream AFL documentation (technical details, parallel fuzzing notes, env variables, etc.). |

ParaAFL-specific runtime configuration:

- `PRUNE_INTERVAL` (in `config.h`, default `1`) — how many queue cycles between corpus-pruning passes.
- `-M name:i/N` — the existing AFL Master syntax; `N > 1` activates parallel deterministic fuzzing.

---

## Dataset Information

This is a fuzzing tool rather than a dataset project, but it ships several input collections:

- **Demo seeds** — `target_demo/seeds/input.txt`: a minimal starting input for the bundled demo target.
- **Sample dictionaries** — `dictionaries/`: token dictionaries for common formats, used with AFL's
  `-x` option to seed structure-aware mutations.
- **Format test cases** — `testcases/`: small, valid example inputs for many file/data formats,
  intended as initial seed corpora.
- **Crashing examples** — `docs/vuln_samples/`: historical AFL crash samples (images, archives,
  media, etc.) demonstrating real bugs AFL has found in third-party software.

Example output produced by a run lives under `target_demo/sync_out/<instance>/`:

- `queue/` — newly discovered, coverage-increasing inputs.
- `fuzzer_stats` — machine-readable run statistics.
- `plot_data` — time-series data for `afl-plot`.
- `prune_log.txt` — ParaAFL corpus-pruning report (kept vs. removed test cases).

---

## Requirements

Target platform: **Linux**, built with **GCC or Clang**.

- A C toolchain: `gcc` (or `clang`) and `make`.
- Standard build tools / libc development headers (e.g. `build-essential` on Debian/Ubuntu).
- Bash and standard POSIX utilities (used by `afl-cmin`, `afl-plot`, `afl-whatsup`).
- *(Optional)* `clang` + `llvm-config` if you want to build `llvm_mode/`.
- *(Optional)* `qemu` build dependencies if you want to build `qemu_mode/`.

Install on Debian/Ubuntu:

```bash
sudo apt-get update
sudo apt-get install build-essential
```

---

## Usage Instructions

### 1. Build

From the repository root:

```bash
make clean
make
```

Verify the build:

```bash
./afl-fuzz --version    # expected: afl-fuzz 2.57b
```

### 2. Instrument your target

Compile the program under test with the AFL compiler wrapper so it is instrumented:

```bash
CC=/path/to/ParaAFL/afl-gcc ./configure   # or use afl-gcc directly
make
```

For a single source file:

```bash
./afl-gcc -o target target.c
```

### 3. Run the fuzzer

#### Single-Master mode (identical to original AFL)

```bash
./afl-fuzz -i seeds -o sync_out -M m1:1/1 -- ./target @@
```

#### Multi-Master mode (parallel deterministic fuzzing)

Run each Master in its own terminal, all sharing the same output (sync) directory:

```bash
# Terminal 1 — Master 1 of 2
./afl-fuzz -i seeds -o sync_out -M m1:1/2 -- ./target @@

# Terminal 2 — Master 2 of 2
./afl-fuzz -i seeds -o sync_out -M m2:2/2 -- ./target @@
```

#### Adding Slave instances

```bash
# Master 1/2
./afl-fuzz -i seeds -o sync_out -M m1:1/2 -- ./target @@
# Master 2/2
./afl-fuzz -i seeds -o sync_out -M m2:2/2 -- ./target @@
# Slave
./afl-fuzz -i seeds -o sync_out -S s1   -- ./target @@
```

Notes:

- `@@` is replaced by AFL with the path to the current test-case file. Omit it if the target reads from stdin.
- `-i` is the input seed directory; `-o` is the shared output/sync directory.
- Tune the pruning frequency by changing `PRUNE_INTERVAL` in `config.h` and rebuilding.

---

## Demo Walkthrough

The `target_demo/` directory contains a self-contained example.

- `target.c` — a small C program with several nested branches and a deliberate `NULL`-pointer crash
  reachable on the input prefix `ABC!` (useful for confirming the fuzzer finds crashes).
- `seeds/input.txt` — initial seed input.
- `sync_out/` — example output directory.

Build and run the demo from the repository root:

```bash
# Build ParaAFL first
make

# Build the demo target with instrumentation
./afl-gcc -o target_demo/target target_demo/target.c

cd target_demo

# Single-Master mode
../afl-fuzz -i seeds -o sync_out -M m1:1/1 -- ./target @@

# Or Multi-Master mode (separate terminals)
../afl-fuzz -i seeds -o sync_out -M m1:1/2 -- ./target @@
../afl-fuzz -i seeds -o sync_out -M m2:2/2 -- ./target @@
```

Inspect the results:

```bash
# Fuzzing statistics
cat target_demo/sync_out/m1/fuzzer_stats

# Corpus pruning report
cat target_demo/sync_out/m1/prune_log.txt
```

---

## Citations

If you build on ParaAFL, please cite both the upstream AFL project and this work.

**Upstream AFL**

> Michał Zalewski, *American Fuzzy Lop (AFL)*. https://github.com/google/AFL

**ParaAFL**

This code accompanies a research paper that has **not yet been published**. A formal citation
(venue and identifier) will be added here once it is available. In the meantime, please reference
this repository and its author directly.

```bibtex
@misc{paraafl,
  title        = {ParaAFL: Parallel Master AFL with Deterministic Stage Partitioning and Corpus Pruning},
  author       = {z1r0},
  note         = {Accompanying research paper; not yet published. Citation to be updated upon publication.},
  howpublished = {Source code},
  year         = {2026}
}
```

---

## License

ParaAFL is distributed under the **Apache License 2.0**, the same license as upstream AFL.
See the [`LICENSE`](LICENSE) file for the full text.

ParaAFL is authored by **z1r0** and is a derivative work of AFL by Michał Zalewski and
contributors; original copyright notices are retained.

---

## Contribution Guidelines

Contributions are welcome. Please see [`CONTRIBUTING.md`](CONTRIBUTING.md) for details. In summary:

- Contributions require a signed Contributor License Agreement (CLA).
- All submissions are made and reviewed via GitHub pull requests.
- Keep changes focused; for fuzzer-core changes (`afl-fuzz.c`, `config.h`), describe the impact on
  the parallel-Master and pruning logic, and verify the build with `make clean && make`.
