#!/bin/sh
# Builds bench/bench.cc at -O2 and runs it several times, keeping the raw output of each run.
# See docs/BENCHMARKING.md.
set -eu

usage() {
	cat <<'EOF'
Usage: bench/run.sh [options] [LIMBS...] [OPS...]

Builds bench/bench.cc at -O2 with GMP and Boost.Multiprecision when they are found (uinteger_t
alone otherwise), runs it RUNS times and writes each run's output to OUTDIR/run_N.txt, with the
host, compiler and load in OUTDIR/host.txt. LIMBS and OPS go to bench.cc: sizes are limb counts and
ops are add mul sqr div str10 parse10 str16 (default: 2 4 16 64 256 1024 and every op).

Options:
  -p PREFIX  where GMP and Boost live (PREFIX/include and PREFIX/lib); env PREFIX. Without it the
             compiler's own paths, then Homebrew's prefix, are tried.
  -n RUNS    number of runs (default 2)
  -o OUTDIR  output directory (default bench-out/run)
  -H DIR     directory holding uinteger_t.hh (default: this repository)
  -s FILE    benchmark source (default: bench.cc beside this script)
  -G         do not use GMP
  -B         do not use Boost
  -h         show this help

Environment: CXX (default c++), BENCH_CXXFLAGS (extra compiler flags, e.g. -march=native).
Run it on a quiet machine, under nice if the machine is shared.

Example: bench/run.sh -p /opt/homebrew -o out 4 64 1024 16384 add mul div str10 parse10
EOF
}

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
# shellcheck source-path=SCRIPTDIR
# shellcheck source=common.sh
. "$here/common.sh"

runs=2
out=bench-out/run
hdr=$root
src=$here/bench.cc
want_gmp=1
want_boost=1
while getopts p:n:o:H:s:GBh opt; do
	case $opt in
	p) PREFIX=$OPTARG ;;
	n) runs=$OPTARG ;;
	o) out=$OPTARG ;;
	H) hdr=$OPTARG ;;
	s) src=$OPTARG ;;
	G) want_gmp=0 ;;
	B) want_boost=0 ;;
	h) usage; exit 0 ;;
	*) usage >&2; exit 2 ;;
	esac
done
shift $((OPTIND - 1))

case $runs in
'' | *[!0-9]* | 0) bench_die "-n takes a positive number, not '$runs'" ;;
esac
[ -f "$hdr/uinteger_t.hh" ] || bench_die "no uinteger_t.hh in $hdr"
[ -f "$src" ] || bench_die "no benchmark source $src"

BENCH_TMP=$(mktemp -d "${TMPDIR:-/tmp}/uinteger-bench.XXXXXX")
trap 'rm -rf "$BENCH_TMP"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

bench_find "$want_gmp" "$want_boost"
printf 'building %s\n' "$src" >&2
bench_build "$src" "$hdr" "$BENCH_TMP/bench"

mkdir -p "$out"
rm -f "$out"/run_*.txt
{
	bench_host_info
	printf 'header: %s/uinteger_t.hh (%s)\n' "$hdr" "$(bench_header_id "$hdr/uinteger_t.hh")"
	printf 'args: %s\n' "$*"
	printf 'load before: %s\n' "$(uptime)"
} > "$out/host.txt"

i=1
while [ "$i" -le "$runs" ]; do
	printf 'run %s of %s\n' "$i" "$runs" >&2
	"$BENCH_TMP/bench" "$@" > "$out/run_$i.txt"
	i=$((i + 1))
done
printf 'load after: %s\n' "$(uptime)" >> "$out/host.txt"

printf 'wrote %s runs to %s\n' "$runs" "$out" >&2
if command -v python3 >/dev/null 2>&1; then
	python3 "$here/summarize.py" "$out"
fi
