# ParaAFL - Parallel Master AFL

> An enhanced version of AFL (American Fuzzy Lop) 2.57b with parallel deterministic fuzzing and corpus pruning capabilities.

## Overview

ParaAFL extends the original AFL with two major improvements to address performance bottlenecks in multi-instance fuzzing scenarios:

1. **Parallel Master Deterministic Fuzzing**: Splits deterministic mutation stages across multiple Master instances, enabling true parallel execution of bitflip, arithmetic, interest, and dictionary stages.

2. **Corpus Pruning Module**: Automatically deduplicates redundant test cases across all fuzzer instances after each queue cycle, reducing corpus bloat and improving overall fuzzing efficiency.

## Key Features

- **Multi-Master Parallelism**: Deterministic stages are divided by flip ranges among multiple Master processes
- **Corpus Pruning**: Automatic trace-based deduplication across all fuzzer instances
- **Backward Compatible**: Single-Master mode behaves identically to original AFL
- **AFL Compatible**: Fully compatible with existing AFL instrumentation and workflow

## Improvements

### Improvement 1: Parallel Master Deterministic Fuzzing

The original AFL's `-M` mode only splits queue items using `exec_cksum % master_max`, but each Master still executes the full deterministic mutation sequence. ParaAFL splits the deterministic mutation operations themselves using the formula:

```
FlipRange(i, N) = [(i * Max_Flips) / N, ((i + 1) * Max_Flips) / N)
```

Where:
- `N` = Total number of Master processes (`master_max`)
- `i` = Current Master index (0-based)
- `Max_Flips` = Total number of flip operations for the current input

This allows each Master to execute only its assigned portion of deterministic mutations, achieving true parallelization.

### Improvement 2: Corpus Pruning

After each queue cycle, ParaAFL:
1. Scans all fuzzer instance queues
2. Executes each test case to obtain its execution trace checksum
3. Removes redundant test cases with duplicate traces
4. Keeps unique test cases for subsequent fuzzing rounds

This reduces corpus size and avoids wasting cycles on duplicate inputs.

## Build

```bash
cd /workspace/AFL
make clean
make
```

## Quick Start

### Single Master Mode (Same as original AFL)

```bash
cd /workspace/AFL/target_demo
../afl-fuzz -i seeds -o sync_out -M m1:1/1 -- ./target @@
```

### Multi-Master Mode (New Feature)

```bash
cd /workspace/AFL/target_demo

# Terminal 1: Master 1/2
../afl-fuzz -i seeds -o sync_out -M m1:1/2 -- ./target @@

# Terminal 2: Master 2/2
../afl-fuzz -i seeds -o sync_out -M m2:2/2 -- ./target @@
```

### With Slave Instances

```bash
cd /workspace/AFL/target_demo

# Terminal 1: Master 1/2
../afl-fuzz -i seeds -o sync_out -M m1:1/2 -- ./target @@

# Terminal 2: Master 2/2
../afl-fuzz -i seeds -o sync_out -M m2:2/2 -- ./target @@

# Terminal 3: Slave
../afl-fuzz -i seeds -o sync_out -S s1 -- ./target @@
```

## Demo Target

The `target_demo/` directory contains a sample target program for testing:

- `target.c` - Simple C program with multiple code branches
- `seeds/` - Initial seed input (`input.txt`)
- `sync_out/` - Output directory for fuzzing results

### How to Run the Demo

```bash
cd /workspace/AFL/target_demo

# Single Master mode
../afl-fuzz -i seeds -o sync_out -M m1:1/1 -- ./target @@

# Or with Multi-Master (run in separate terminals)
../afl-fuzz -i seeds -o sync_out -M m1:1/2 -- ./target @@
../afl-fuzz -i seeds -o sync_out -M m2:2/2 -- ./target @@
```

After running, check the results:

```bash
# View fuzzing statistics
cat target_demo/sync_out/m1/fuzzer_stats

# View corpus pruning log
cat target_demo/sync_out/m1/prune_log.txt
```

## Files Changed

- `afl-fuzz.c` - Core fuzzer with parallel Master and corpus pruning logic
- `config.h` - Added `PRUNE_INTERVAL` macro

## Verification

Compile and verify:

```bash
cd /workspace/AFL
make clean && make
./afl-fuzz --version
```

Expected output: `afl-fuzz 2.57b`

## License

Same as original AFL - Apache License 2.0
