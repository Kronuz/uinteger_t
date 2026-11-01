#!/usr/bin/env python3
"""Summarize the output of bench/run.sh and bench/ab.sh.

    summarize.py DIR                   the best time per (limbs, op) over DIR's runs, with ratios to GMP and Boost
    summarize.py --ab DIR              A against B (DIR holds a_N.txt and b_N.txt, as ab.sh writes them)
    summarize.py --ab DIR_A DIR_B      A against B from two run.sh output directories
    summarize.py --table DIR           the Markdown table of every row, for docs/perf
    summarize.py --readme [LABEL=]DIR...   the README performance table, one machine per DIR

A PATH is a run.sh output directory (its run_N.txt files), a file of bench.cc output, or a glob
pattern. Times are microseconds per call. The best time of a row is its minimum over the runs (or
the median with --stat median); the spread of a row is (max - min) / min over the runs.
"""

from __future__ import annotations

import argparse
import glob
import statistics
import sys
from pathlib import Path

Key = tuple[int, str]
# For each (limbs, op), the values of each column (uinteger_t, gmp, boost), one per run.
Samples = dict[Key, list[list[float]]]

COLUMNS = ("uinteger_t", "gmp", "boost")
OP_ORDER = ("add", "mul", "sqr", "div n/2", "div n/8", "str10", "parse10", "str16")
# bench.cc prints times with three decimals, so one unit in the last place is its resolution.
RESOLUTION = 0.001

README_SIZES = (4, 64, 16384)
OP_LABELS = {
    "add": "`a + b`",
    "mul": "`a * b`",
    "sqr": "`a * a`",
    "div n/2": "`a / b`, b half of a",
    "div n/8": "`a / b`, b an eighth of a",
    "str10": "`a.str(10)`",
    "parse10": "`uinteger_t(s, 10)`",
    "str16": "`a.str(16)`",
}
README_OPS = ("add", "mul", "div n/2", "str10", "parse10")


def parse_file(path: Path, samples: Samples) -> None:
    """Add the rows of one bench.cc output to samples."""
    with path.open(encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.split()
            if len(parts) < 5 or not parts[0].isdigit():
                continue
            try:
                values = [None if c == "-" else float(c) for c in parts[-3:]]
            except ValueError:
                continue
            columns = samples.setdefault(
                (int(parts[0]), " ".join(parts[1:-3])), [[], [], []]
            )
            for column, value in zip(columns, values):
                if value is not None:
                    column.append(value)


def expand(paths: list[str], pattern: str = "run_*.txt") -> list[Path]:
    """The files named by paths: a directory gives its files matching pattern, a glob its matches."""
    files: list[Path] = []
    for p in paths:
        path = Path(p)
        if path.is_dir():
            files += sorted(path.glob(pattern))
        elif any(c in p for c in "*?["):
            files += sorted(Path(m) for m in glob.glob(p))
        elif path.is_file():
            files.append(path)
        else:
            sys.exit(f"summarize.py: {p}: no such file or directory")
    if not files:
        sys.exit(f"summarize.py: no {pattern} files in {' '.join(paths)}")
    return files


def load(paths: list[str], pattern: str = "run_*.txt") -> Samples:
    samples: Samples = {}
    for f in expand(paths, pattern):
        parse_file(f, samples)
    return samples


def parse_list(text: str | None) -> list[str]:
    return [t for t in (text or "").replace(",", " ").split() if t]


def op_matches(op: str, wanted: list[str]) -> bool:
    """An op matches a name when it is that name or its first word ("div" matches "div n/2")."""
    return not wanted or op in wanted or op.split()[0] in wanted


def row_order(key: Key) -> tuple[int, int, str]:
    limbs, op = key
    return (limbs, OP_ORDER.index(op) if op in OP_ORDER else len(OP_ORDER), op)


def select(samples: Samples, sizes: list[int], ops: list[str]) -> list[Key]:
    keys = [
        k for k in samples if (not sizes or k[0] in sizes) and op_matches(k[1], ops)
    ]
    return sorted(keys, key=row_order)


def best(values: list[float], stat: str) -> float | None:
    if not values:
        return None
    return min(values) if stat == "min" else statistics.median(values)


def spread(values: list[float]) -> float | None:
    """(max - min) / min of the runs, or None for a single run."""
    if len(values) < 2 or min(values) <= 0:
        return None
    return (max(values) - min(values)) / min(values)


def cell(x: float | None, width: int = 11) -> str:
    return f"{'-':>{width}}" if x is None else f"{x:{width}.3f}"


def percent(x: float | None, width: int = 7) -> str:
    return f"{'-':>{width}}" if x is None else f"{x * 100:{width - 1}.1f}%"


def print_runs(samples: Samples, keys: list[Key], stat: str) -> None:
    print(
        f"{'limbs':<7}{'op':<9}{'uinteger_t':>11} {'gmp':>11} {'boost':>11} {'vs gmp':>8} {'vs boost':>9} {'spread':>7}"
    )
    for key in keys:
        u, g, b = (best(c, stat) for c in samples[key])
        vs_gmp = f"{u / g:8.2f}" if u is not None and g else f"{'-':>8}"
        vs_boost = f"{u / b:9.2f}" if u is not None and b else f"{'-':>9}"
        print(
            f"{key[0]:<7}{key[1]:<9}{cell(u)} {cell(g)} {cell(b)} {vs_gmp} {vs_boost} {percent(spread(samples[key][0]))}"
        )
    print(
        "\ntimes in microseconds per call; vs gmp and vs boost are uinteger_t's time over theirs (below 1: uinteger_t is faster)"
    )


def print_ab(
    a: Samples, b: Samples, keys: list[Key], column: int, stat: str, threshold: float
) -> None:
    print(
        f"{'limbs':<7}{'op':<9}{'A':>11} {'spread':>7} {'B':>11} {'spread':>7} {'B/A':>7}  verdict"
    )
    counts = {"faster": 0, "slower": 0, "": 0}
    worst = 0.0
    for key in keys:
        av, bv = a[key][column], b[key][column]
        x, y = best(av, stat), best(bv, stat)
        if not x or not y:
            continue
        sa, sb = spread(av), spread(bv)
        ratio = y / x
        # A row counts when B/A is off 1 by more than the spreads of both sides, the threshold, and
        # the resolution of the printed times.
        noise = max(threshold, sa or 0.0, sb or 0.0, RESOLUTION / min(x, y))
        verdict = (
            "faster" if ratio < 1 - noise else "slower" if ratio > 1 + noise else ""
        )
        counts[verdict] += 1
        worst = max(worst, abs(ratio - 1))
        print(
            f"{key[0]:<7}{key[1]:<9}{cell(x)} {percent(sa)} {cell(y)} {percent(sb)}"
            f" {ratio:7.3f}  {verdict}".rstrip()
        )
    same = counts[""]
    print(
        f"\n{counts['faster']} faster, {counts['slower']} slower, {same} within the noise "
        f"(more than {threshold * 100:.1f}% and the spreads); "
        f"largest |B/A - 1| is {worst * 100:.1f}%. B/A below 1 means B is faster. "
        "On an A/A run every row should be within the noise."
    )


def fmt_time(x: float | None) -> str:
    if x is None:
        return "-"
    if x >= 100:
        return f"{x:,.0f}"
    if x >= 10:
        return f"{x:.1f}"
    if x >= 1:
        return f"{x:.2f}"
    return f"{x:.3f}"


def fmt_ratio(x: float | None) -> str:
    """A time ratio to two significant digits (three decimals below 0.1)."""
    if x is None:
        return "-"
    if x >= 10:
        return f"{x:.0f}"
    if x >= 0.1:
        return f"{x:.2f}"
    return f"{x:.3f}"


def ratio(top: list[float], bottom: list[float], stat: str) -> float | None:
    u, v = best(top, stat), best(bottom, stat)
    return u / v if u is not None and v else None


def table_rows(
    samples: Samples, sizes: list[int], ops: list[str], stat: str
) -> list[tuple[str, int, list[list[float]]]]:
    """(op label, limbs, columns) for the requested sizes of each op, ops in OP_ORDER, sizes ascending."""
    rows = []
    for op in OP_ORDER:
        if not op_matches(op, ops):
            continue
        shown = sorted(
            k[0] for k in samples if k[1] == op and (not sizes or k[0] in sizes)
        )
        for limbs in shown:
            rows.append((OP_LABELS[op], limbs, samples[(limbs, op)]))
    return rows


def print_table(samples: Samples, sizes: list[int], ops: list[str], stat: str) -> None:
    """Every row of one machine: the times of the three libraries and uinteger_t's time over each."""
    print(
        "| operation | bits | uinteger_t, µs | GMP, µs | Boost, µs | × GMP's time | × Boost's time |"
    )
    print("|---|---:|---:|---:|---:|---:|---:|")
    last = None
    for label, limbs, (u, g, b) in table_rows(samples, sizes, ops, stat):
        print(
            f"| {label if label != last else ''} | {limbs * 64:,} | {fmt_time(best(u, stat))} "
            f"| {fmt_time(best(g, stat))} | {fmt_time(best(b, stat))} "
            f"| {fmt_ratio(ratio(u, g, stat))} | {fmt_ratio(ratio(u, b, stat))} |"
        )
        last = label


def print_readme(
    machines: list[Samples], sizes: list[int], labels: list[str], stat: str
) -> None:
    """The README table: each cell holds one value per machine, separated by " / "."""
    where = f" ({' / '.join(labels)})" if len(labels) > 1 else ""
    print(
        f"| operation | bits | uinteger_t, µs{where} | × GMP's time{where} | × Boost's time{where} |"
    )
    print("|---|---:|---:|---:|---:|")
    last = None
    for op in README_OPS:
        for limbs in sizes:
            cells: list[list[str]] = [[], [], []]
            for samples in machines:
                columns = samples.get((limbs, op))
                if columns is None:
                    print(
                        f"summarize.py: no row for {limbs} limbs, {op}", file=sys.stderr
                    )
                    columns = [[], [], []]
                u, g, b = columns
                cells[0].append(fmt_time(best(u, stat)))
                cells[1].append(fmt_ratio(ratio(u, g, stat)))
                cells[2].append(fmt_ratio(ratio(u, b, stat)))
            label = OP_LABELS[op]
            print(
                f"| {label if label != last else ''} | {limbs * 64:,} "
                + "".join(f"| {' / '.join(c)} " for c in cells)
                + "|"
            )
            last = label


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Summarize the output of bench/run.sh and bench/ab.sh.",
        epilog="Times are microseconds per call. See docs/BENCHMARKING.md.",
    )
    parser.add_argument(
        "paths",
        nargs="+",
        metavar="PATH",
        help="run.sh output directories, bench.cc output files or globs",
    )
    parser.add_argument(
        "--ab",
        action="store_true",
        help="compare A with B: one ab.sh directory (a_N.txt, b_N.txt) or two run.sh directories",
    )
    parser.add_argument(
        "--readme",
        action="store_true",
        help="print the README performance table: each PATH is one machine, as LABEL=PATH or PATH",
    )
    parser.add_argument(
        "--table",
        action="store_true",
        help="print a Markdown table of every row (--sizes and --ops select) for one machine",
    )
    parser.add_argument(
        "--stat",
        choices=("min", "median"),
        default="min",
        help="the statistic over runs (default: min)",
    )
    parser.add_argument(
        "--sizes",
        help="limb counts to show, comma separated (--readme default: 4,64,16384)",
    )
    parser.add_argument(
        "--ops",
        help="ops to show, comma separated (add mul sqr div str10 parse10 str16)",
    )
    parser.add_argument(
        "--column",
        choices=COLUMNS,
        default="uinteger_t",
        help="with --ab, the column to compare (default: uinteger_t)",
    )
    parser.add_argument(
        "--threshold",
        type=float,
        default=3.0,
        help="with --ab, the percent a row must differ by to count (default: 3)",
    )
    args = parser.parse_args()

    sizes = [int(s) for s in parse_list(args.sizes)]
    ops = parse_list(args.ops)
    if args.ab + args.readme + args.table > 1:
        parser.error("--ab, --readme and --table do not combine")
    if args.ab:
        if len(args.paths) == 1:
            a, b = load(args.paths, "a_*.txt"), load(args.paths, "b_*.txt")
        elif len(args.paths) == 2:
            a, b = load([args.paths[0]]), load([args.paths[1]])
        else:
            parser.error("--ab takes one ab.sh directory or two run.sh directories")
        column = COLUMNS.index(args.column)
        keys = [k for k in select(a, sizes, ops) if k in b]
        print_ab(a, b, keys, column, args.stat, args.threshold / 100)
        return
    if args.readme:
        labels = [
            p.partition("=")[0] if "=" in p else f"machine {i + 1}"
            for i, p in enumerate(args.paths)
        ]
        machines = [load([p.partition("=")[2] if "=" in p else p]) for p in args.paths]
        print_readme(machines, sizes or list(README_SIZES), labels, args.stat)
        return
    samples = load(args.paths)
    if args.table:
        print_table(samples, sizes, ops, args.stat)
        return
    print_runs(samples, select(samples, sizes, ops), args.stat)


if __name__ == "__main__":
    main()
