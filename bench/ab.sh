#!/bin/sh
# Interleaved A/B of two uinteger_t.hh headers over bench/bench.cc: A B A B ... for ROUNDS rounds.
# See docs/BENCHMARKING.md.
set -eu

usage() {
	cat <<'EOF'
Usage: bench/ab.sh [options] A B [LIMBS...] [OPS...]
       bench/ab.sh [options] -a A [LIMBS...] [OPS...]

Builds bench/bench.cc against two headers (the same bench.cc on both sides) and alternates them,
A B A B ..., for ROUNDS rounds. Each side's output goes to OUTDIR/a_N.txt and OUTDIR/b_N.txt, and
bench/summarize.py prints the comparison. A and B are each a directory holding uinteger_t.hh, a
header file, or a git ref of this repository (its uinteger_t.hh is taken with git show), e.g.
"HEAD" against "." (the working tree). LIMBS and OPS go to bench.cc (see run.sh).

Options:
  -a         A/A: run header A on both sides, to learn the noise (B is not given); the two sides
             are the same binary
  -r ROUNDS  rounds of A and B (default 3)
  -o OUTDIR  output directory (default bench-out/ab)
  -g         also link GMP, whose column runs the same code on both sides (a second noise reading)
  -p PREFIX  where GMP lives (PREFIX/include and PREFIX/lib); env PREFIX; implies -g
  -s FILE    benchmark source (default: bench.cc beside this script)
  -h         show this help

Environment: CXX (default c++), BENCH_CXXFLAGS (extra compiler flags, e.g. -march=native).
Run it on a quiet machine, under nice if the machine is shared.

Example: bench/ab.sh -r 3 -o out master HEAD 2 4 8 16 64 256 1024 add mul sqr
EOF
}

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
# shellcheck source-path=SCRIPTDIR
# shellcheck source=common.sh
. "$here/common.sh"

rounds=3
out=bench-out/ab
src=$here/bench.cc
aa=0
want_gmp=0
while getopts ar:o:gp:s:h opt; do
	case $opt in
	a) aa=1 ;;
	r) rounds=$OPTARG ;;
	o) out=$OPTARG ;;
	g) want_gmp=1 ;;
	p) PREFIX=$OPTARG; want_gmp=1 ;;
	s) src=$OPTARG ;;
	h) usage; exit 0 ;;
	*) usage >&2; exit 2 ;;
	esac
done
shift $((OPTIND - 1))

case $rounds in
'' | *[!0-9]* | 0) bench_die "-r takes a positive number, not '$rounds'" ;;
esac
[ -f "$src" ] || bench_die "no benchmark source $src"
if [ "$aa" = 1 ]; then
	[ $# -ge 1 ] || { usage >&2; exit 2; }
	a=$1
	b=$1
	shift 1
else
	[ $# -ge 2 ] || { usage >&2; exit 2; }
	a=$1
	b=$2
	shift 2
fi

BENCH_TMP=$(mktemp -d "${TMPDIR:-/tmp}/uinteger-bench.XXXXXX")
trap 'rm -rf "$BENCH_TMP"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# resolve ARG DIR: copy ARG's uinteger_t.hh to DIR and describe it in $desc.
resolve() {
	mkdir -p "$2"
	if [ -d "$1" ] && [ -f "$1/uinteger_t.hh" ]; then
		cp "$1/uinteger_t.hh" "$2/uinteger_t.hh"
		desc="$1/uinteger_t.hh ($(bench_header_id "$1/uinteger_t.hh"))"
	elif [ -f "$1" ]; then
		cp "$1" "$2/uinteger_t.hh"
		desc="$1 ($(bench_header_id "$1"))"
	elif rev=$(git -C "$root" rev-parse --verify --quiet "$1^{commit}" 2>/dev/null); then
		git -C "$root" show "$1:uinteger_t.hh" > "$2/uinteger_t.hh"
		desc="$1 (commit $(git -C "$root" rev-parse --short "$rev"))"
	else
		bench_die "$1 is not a directory holding uinteger_t.hh, a header file or a git ref"
	fi
}

resolve "$a" "$BENCH_TMP/a"
desc_a=$desc
resolve "$b" "$BENCH_TMP/b"
desc_b=$desc

bench_find "$want_gmp" 0
printf 'building %s\n' "$src" >&2
bench_build "$src" "$BENCH_TMP/a" "$BENCH_TMP/bench_a"
if [ "$aa" = 1 ]; then
	cp "$BENCH_TMP/bench_a" "$BENCH_TMP/bench_b"
else
	bench_build "$src" "$BENCH_TMP/b" "$BENCH_TMP/bench_b"
fi

mkdir -p "$out"
rm -f "$out"/a_*.txt "$out"/b_*.txt
{
	bench_host_info
	printf 'A: %s\n' "$desc_a"
	printf 'B: %s\n' "$desc_b"
	if [ "$aa" = 1 ]; then printf 'mode: A/A (both sides are the same binary)\n'; fi
	printf 'rounds: %s\n' "$rounds"
	printf 'args: %s\n' "$*"
	printf 'load before: %s\n' "$(uptime)"
} > "$out/host.txt"

r=1
while [ "$r" -le "$rounds" ]; do
	for side in a b; do
		printf 'round %s of %s: %s\n' "$r" "$rounds" "$side" >&2
		"$BENCH_TMP/bench_$side" "$@" > "$out/${side}_$r.txt"
	done
	r=$((r + 1))
done
printf 'load after: %s\n' "$(uptime)" >> "$out/host.txt"

printf 'wrote %s rounds to %s\n' "$rounds" "$out" >&2
if command -v python3 >/dev/null 2>&1; then
	python3 "$here/summarize.py" --ab "$out"
fi
