#!/usr/bin/env python3
"""Converts every *.jsonl of a run (ground truth or subset) to Parquet, next to it.

Usage: python3 scripts/to_parquet.py data/groundtruth/lust-run0 [more dirs...]
Requires pyarrow.
"""
import glob
import os
import sys

import pyarrow.json as pj
import pyarrow.parquet as pq


def convert(run_dir):
    for path in sorted(glob.glob(os.path.join(run_dir, "*.jsonl"))):
        if os.path.getsize(path) == 0:
            continue
        table = pj.read_json(path, read_options=pj.ReadOptions(block_size=64 << 20))
        out = path[:-len(".jsonl")] + ".parquet"
        pq.write_table(table, out, compression="zstd")
        print(f"{out}: {table.num_rows} rows")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for d in sys.argv[1:]:
        convert(d)
