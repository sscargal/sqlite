#!/bin/bash

# Usage: ./run_pmem_bench.sh [--strace]
# Example: ./run_pmem_bench.sh --strace
set -e
STRACE=0

# Check if strace option is provided
if [[ "$1" == "--strace" ]]; then
    STRACE=1
    shift
fi

# Create output directory
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
OUTDIR="run_${TIMESTAMP}"
mkdir -p "$OUTDIR"

LOGFILE="$OUTDIR/results.csv" # Output log file
PMEMTEST="./pmemtest"  # Path to your pmemtest binary

# Test matrix
DEVICES=("/mnt/pmem/pmemtest.db" "/home/amd/pmemtest.db")
MMAP_OPTS=("enabled" "disabled")
SIZES=(104857600 1073741824) # 100MB, 1GB
ROWS=(10000 100000) # 10K, 100K
# Journal modes to test (default DELETE, plus others)
JOURNAL_MODES=("DELETE" "TRUNCATE" "PERSIST" "MEMORY" "WAL" "OFF")

echo "Test,Device,MMAP,Size,Rows,JournalMode,Insert_sec,Insert_IOPS,Select_sec,Select_IOPS,Syscalls" > "$LOGFILE"

# Run the tests


for DEV in "${DEVICES[@]}"; do
    DEV_DIR=$(dirname "$DEV")

    if [[ ! -d "$DEV_DIR" ]]; then
        echo "Warning: Device directory $DEV_DIR does not exist, skipping $DEV"
        continue
    fi
    if [[ ! -e "$DEV" ]]; then
        touch "$DEV"
    fi
    for MMAP in "${MMAP_OPTS[@]}"; do
        for JM in "${JOURNAL_MODES[@]}"; do
            for SIZE in "${SIZES[@]}"; do
                for ROW in "${ROWS[@]}"; do
                    START_DT=$(date +"%Y-%m-%dT%H:%M:%S")
                    START_SEC=$(date +%s)
                    TESTNAME="$(basename "$DEV")_${MMAP}_${SIZE}_${ROW}_jm${JM}"
                    ARGS="-f $DEV -s $SIZE -r $ROW -j $JM"
                    if [[ "$MMAP" == "disabled" ]]; then
                        ARGS="$ARGS -m"
                    fi
                    if [[ $STRACE -eq 1 ]]; then
                        echo "Running (strace): $PMEMTEST $ARGS"
                        STRACE_LOG="strace_${TESTNAME}.log"
                        strace -c -o "$STRACE_LOG" $PMEMTEST $ARGS > tmp_bench.log 2>&1
                        END_DT=$(date +"%Y-%m-%dT%H:%M:%S")
                        END_SEC=$(date +%s)
                        ELAPSED=$((END_SEC-START_SEC))
                        SYSCALLS=$(grep "total" "$STRACE_LOG" | awk '{print $4}')
                    else
                        echo "Running: $PMEMTEST $ARGS"
                        $PMEMTEST $ARGS > tmp_bench.log 2>&1
                        END_DT=$(date +"%Y-%m-%dT%H:%M:%S")
                        END_SEC=$(date +%s)
                        ELAPSED=$((END_SEC-START_SEC))
                        SYSCALLS="N/A"
                        STRACE_LOG=""
                    fi
                    # Extract results
                    INSERT=$(grep "^Insert:" tmp_bench.log | awk '{print $2","$4}')
                    SELECT=$(grep "^Select:" tmp_bench.log | awk '{print $2","$4}')
                    # Extract actual journal_mode from new output format
                    ACTUAL_JM=$(grep '^journal_mode:' tmp_bench.log | awk '{print $2}')
                    if [[ -z "$ACTUAL_JM" ]]; then
                        ACTUAL_JM="$JM"
                    fi
                    echo "$TESTNAME,$DEV,$MMAP,$SIZE,$ROW,$ACTUAL_JM,$INSERT,$SELECT,$SYSCALLS,$STRACE_LOG,$START_DT,$END_DT,$ELAPSED" >> "$LOGFILE"
                    rm -f tmp_bench.log
                done
            done
        done
    done
done

echo "All tests complete. Results saved to $LOGFILE"
