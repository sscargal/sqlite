# SQLite3 Persistent Memory (PMEM) Support — pmem Branch

This README describes how to build, configure, and benchmark the modified SQLite3 with persistent memory (PMEM) support, as implemented in the `pmem` branch. It also summarizes key code changes and provides guidance for running benchmarks on CXL NV-CMM/PMem devices.

## 1. Building SQLite3 with libpmem2 Support

### Prerequisites (Ubuntu 24.04)

Install required packages:

```bash
sudo apt update
sudo apt upgrade -y
sudo apt install -y build-essential autoconf automake libtool pkg-config tcl-dev libpmem2 libpmem2-dev git
```

### Clone and Checkout the pmem Branch

```bash
git clone https://github.com/sscargal/sqlite.git
cd sqlite
git checkout pmem
```

### Prepare the Build System

```bash
./autosetup/autosetup
tclsh ./tool/mkpragmatab.tcl
```


### Out-of-Tree Build (Recommended)

In the same top-level directory as the sqlite was cloned, create a new `bld` directory. Your directory structure should look like this (example, in your home directory):

```text
$HOME/
├── sqlite/   # cloned source
└── bld/      # build directory (created by you)
```

```bash
mkdir bld
cd bld
../sqlite/configure --enable-all
```

You should see `+ pmem` in the feature flags in the `configure` output. Example output:

```
Feature flags...
  ...
  + pmem
Library feature flags: ... -DSQLITE_HAVE_LIBPMEM2 ...
Shell options: ... -lpmem2
```

Build the project:

```bash
make sqlite3.c
make -j$(nproc)
```

## 2. CXL NV-CMM/PMem Device Setup Example

This project assumes you have a persistent memory device (e.g., Intel Optane, CXL NV-CMM) available and mounted at `/mnt/pmem`.

**Example steps:**

1. Identify your CXL NV-CMM device and configure it for FSDAX mode (using `cxl` and `ndctl`):

   ```bash
   # List CXL memory devices
   cxl list -M

   # Example: Enable a CXL region in FSDAX mode (replace region0 with your region)
   sudo ndctl create-namespace --region=region0 --mode=fsdax --map=dev
   # Or reconfigure an existing namespace to FSDAX
   sudo ndctl disable-namespace namespace0.0
   sudo ndctl create-namespace --reconfigure=namespace0.0 --mode=fsdax
   # List resulting PMEM devices
   ndctl list -N -v
   # You should see /dev/pmem0 or similar
   ```

2. Create a DAX filesystem (XFS or ext4 recommended):

   ```bash
   sudo mkfs.xfs -f /dev/pmem0
   sudo mkdir -p /mnt/pmem
   sudo mount -o dax /dev/pmem0 /mnt/pmem
   sudo chown $USER /mnt/pmem
   ```

3. Verify DAX is enabled:

   ```bash
   mount | grep pmem
   # Should show 'dax' in the mount options
   ```

**Note:**
- The benchmarking scripts expect `/mnt/pmem/pmemtest.db` to be writable.
- Adjust device paths as needed for your environment.

## 3. Summary of os_unix.c Changes (PMEM Integration)

The following functions in `src/os_unix.c` were modified to support PMEM via libpmem2:

- **unixOpen**: Detects when a file is on a DAX/PMEM filesystem and initializes libpmem2 mappings for direct access.
- **unixWrite**: Uses `pmem2_memcpy` to copy data directly to persistent memory, followed by `pmem2_flush` and `pmem2_drain` to ensure the data is made persistent (i.e., durable on power loss). This replaces the traditional pattern of `memcpy` followed by an `fsync` for PMEM files.
- **unixSync**: Bypasses `fsync` for PMEM files, using `pmem2_flush` and `pmem2_drain` to guarantee persistence.
- **unixTruncate**: Ensures PMEM mappings are updated on file size changes.
- **unixFileControl**: Handles new control codes for querying PMEM status and toggling PMEM optimizations.
- **unixMapfile/unixUnmapfile**: Manage PMEM-aware memory mapping and unmapping.
- **PRAGMA pmem_status**: Added support for querying whether a database file is using PMEM optimizations.

These changes use libpmem2 function calls to perform the same memory copy and persistence operations (memcpy + flush + drain) that are required to make data durable on persistent memory, ensuring correctness and crash consistency.

**See code comments in `src/os_unix.c` for detailed explanations and rationale for each change.**

## 4. Running Benchmarks with pmemtest and run_pmemtest.sh

### pmemtest.c

`src/pmemtest.c` is a standalone benchmark tool for evaluating SQLite performance on different storage backends and journal modes.

**Build:**

```bash
cd src
gcc -O2 -o pmemtest pmemtest.c -lsqlite3 -lpmem2
```

**Usage:**

```bash
./pmemtest -f <dbfile> -s <size> -r <rows> [-m] [-j <journal_mode>] [-d <durability>] [-h]
```

- `-f <dbfile>`: Path to database file (e.g., `/mnt/pmem/pmemtest.db`)
- `-s <size>`: Database file size in bytes
- `-r <rows>`: Number of rows to insert/select
- `-m`: Disable mmap (use traditional I/O)
- `-j <journal_mode>`: Set PRAGMA journal_mode (DELETE, WAL, etc.)
- `-d <durability>`: Set durability mode (optional)
- `-h`: Show help

The tool prints timing, IOPS, and the actual journal mode used.

### Automated Benchmarking: run_pmemtest.sh

`src/run_pmemtest.sh` automates running `pmemtest` across a matrix of devices, mmap options, sizes, row counts, and journal modes. Results are logged in CSV format for analysis.

**Usage:**

```bash
cd src
chmod +x run_pmemtest.sh
./run_pmemtest.sh [--strace]
```

- By default, results are saved in a timestamped directory (e.g., `run_20250730_123456/results.csv`).
- The script expects `/mnt/pmem/pmemtest.db` and `/home/amd/pmemtest.db` to be writable.
- The test matrix can be edited at the top of the script.

**CSV Output Columns:**
- Test, Device, MMAP, Size, Rows, JournalMode, Insert_sec, Insert_IOPS, Select_sec, Select_IOPS, Syscalls, StraceLog, StartTime, EndTime, Elapsed

## 5. Additional Notes

- For best results, ensure the PMEM device is not being used by other processes during benchmarking.
- Review the code in `src/os_unix.c` and `src/pmemtest.c` for further details on PMEM integration and benchmarking methodology.


## 6. Future Work

### Current Focus: Write Path Integration

The current PMEM integration in this branch focuses on the write path, specifically using `memcpy`-like operations (via `pmem2_memcpy`) to persist SQLite's memory-mapped file pages. This approach mirrors how SQLite handles memory-mapped files today, but with the addition of explicit persistence (flush + drain) to guarantee durability on PMEM.

**Read Path:**
- The read path is currently unchanged and uses standard memory-mapped file access. Optimizations for PMEM-aware reads are left for future work.

**Write Path Atomicity:**
- Presently, atomicity is guaranteed at the SQLite page granularity. Once this is fully validated, it may be possible to safely disable the journaling code path for PMEM-backed databases, eliminating the double-write penalty and further improving performance.

### Additional Future Work

- **Read Path Optimization:**
  - Investigate PMEM-aware prefetching, caching, or direct access optimizations for read-heavy workloads.
- **Write Path Enhancements:**
  - Explore finer-grained atomicity, such as sub-page or multi-page transactions, using advanced PMEM primitives.
- **libpmemobj Integration:**
  - Consider using `libpmemobj` to manage persistent objects, transactions, and memory pools. This could provide:
    - Stronger atomicity guarantees (e.g., transactional updates across multiple pages)
    - Simplified recovery and consistency mechanisms
    - Potential for lock-free or concurrent data structures in the VFS layer
- **Journaling Bypass:**
  - Once atomicity and crash consistency are fully validated, implement logic to disable or bypass SQLite's journaling for PMEM databases, maximizing performance.
- **Testing and Validation:**
  - Expand test coverage for crash consistency, power-fail safety, and correctness under concurrent workloads.

Contributions and suggestions for any of these areas are welcome. For questions or contributions, see the repository at https://github.com/sscargal/sqlite (pmem branch).
