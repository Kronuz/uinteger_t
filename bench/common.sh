# Shared by run.sh and ab.sh; sourced, not run.
#
# Finds GMP and Boost, builds bench.cc and describes the host. The caller sets BENCH_TMP to a
# scratch directory it removes, and may set PREFIX (where GMP and Boost live, as PREFIX/include and
# PREFIX/lib). Environment: CXX (default c++) and BENCH_CXXFLAGS (extra compiler flags, appended
# after -O2 so they win).

CXX=${CXX:-c++}
BENCH_CXXFLAGS=${BENCH_CXXFLAGS:-}
PREFIX=${PREFIX:-}
use_gmp=0
gmp_prefix=
use_boost=0
boost_prefix=

bench_die() {
	printf '%s: %s\n' "${0##*/}" "$*" >&2
	exit 1
}

# bench_probe KIND PREFIX: succeed when a program using KIND (gmp or boost) builds with PREFIX's
# include and lib directories (none when PREFIX is empty).
bench_probe() {
	_kind=$1
	_where=$2
	# shellcheck disable=SC2086
	set -- $CXX -std=c++17 -x c++ -
	if [ -n "$_where" ]; then
		set -- "$@" "-I$_where/include" "-L$_where/lib"
	fi
	case $_kind in
	gmp)
		printf '%s\n' '#include <gmp.h>' \
			'int main() { mpz_t z; mpz_init(z); mpz_clear(z); return 0; }' |
			"$@" -lgmp -o "$BENCH_TMP/probe" >/dev/null 2>&1
		;;
	boost)
		printf '%s\n' '#include <boost/multiprecision/cpp_int.hpp>' \
			'int main() { boost::multiprecision::cpp_int z = 1; return z == 0; }' |
			"$@" -fsyntax-only >/dev/null 2>&1
		;;
	esac
}

# bench_scan KIND: print the first prefix where KIND builds (an empty line for the compiler's own
# search paths) and succeed, or fail when none does. Tries $PREFIX, the compiler's own paths, then
# Homebrew's prefix.
bench_scan() {
	_brew=
	if command -v brew >/dev/null 2>&1; then
		_brew=$(brew --prefix 2>/dev/null || true)
	fi
	for _cand in "${PREFIX:-.none}" .system "${_brew:-.none}"; do
		case $_cand in
		.none) continue ;;
		.system) _p= ;;
		*) _p=$_cand ;;
		esac
		if bench_probe "$1" "$_p"; then
			printf '%s\n' "$_p"
			return 0
		fi
	done
	return 1
}

# bench_find WANT_GMP WANT_BOOST: sets use_gmp, gmp_prefix, use_boost and boost_prefix, and says
# what it found on stderr.
bench_find() {
	if [ "$1" = 1 ] && gmp_prefix=$(bench_scan gmp); then
		use_gmp=1
		printf 'GMP: %s\n' "${gmp_prefix:-compiler default paths}" >&2
	else
		printf 'GMP: not used\n' >&2
	fi
	if [ "$2" = 1 ] && boost_prefix=$(bench_scan boost); then
		use_boost=1
		printf 'Boost: %s\n' "${boost_prefix:-compiler default paths}" >&2
	else
		printf 'Boost: not used\n' >&2
	fi
}

# bench_build SRC HEADER_DIR OUT: compile SRC at -O2 against HEADER_DIR/uinteger_t.hh, with the
# libraries bench_find chose.
bench_build() {
	_src=$1
	_out=$3
	# shellcheck disable=SC2086
	set -- $CXX -std=c++17 -O2 "-I$2"
	if [ "$use_gmp" = 1 ]; then
		set -- "$@" -DHAVE_GMP
		if [ -n "$gmp_prefix" ]; then set -- "$@" "-I$gmp_prefix/include"; fi
	fi
	if [ "$use_boost" = 1 ]; then
		set -- "$@" -DHAVE_BOOST
		if [ -n "$boost_prefix" ]; then set -- "$@" "-I$boost_prefix/include"; fi
	fi
	# shellcheck disable=SC2086
	set -- "$@" $BENCH_CXXFLAGS "$_src" -o "$_out"
	if [ "$use_gmp" = 1 ]; then
		if [ -n "$gmp_prefix" ]; then set -- "$@" "-L$gmp_prefix/lib" "-Wl,-rpath,$gmp_prefix/lib"; fi
		set -- "$@" -lgmp
	fi
	"$@"
}

# bench_header_id FILE: the commit FILE is at (marked when it differs from it) and its checksum.
bench_header_id() {
	_dir=$(dirname "$1")
	_id=
	if _rev=$(git -C "$_dir" rev-parse --short HEAD 2>/dev/null); then
		_id="commit $_rev"
		if [ -n "$(git -C "$_dir" status --porcelain -- "$(basename "$1")" 2>/dev/null)" ]; then
			_id="$_id, modified"
		fi
		_id="$_id, "
	fi
	_sum=$(cksum "$1")
	printf '%scksum %s\n' "$_id" "${_sum%% *}"
}

bench_cpu() {
	_cpu=
	if [ -r /proc/cpuinfo ]; then
		_cpu=$(sed -n 's/^model name[[:space:]]*:[[:space:]]*//p' /proc/cpuinfo | head -n 1)
	fi
	if [ -z "$_cpu" ] && command -v lscpu >/dev/null 2>&1; then
		_cpu=$(lscpu | sed -n 's/^Model name:[[:space:]]*//p' | head -n 1)
	fi
	if [ -z "$_cpu" ] && command -v sysctl >/dev/null 2>&1; then
		_cpu=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || true)
	fi
	printf '%s\n' "${_cpu:-unknown}"
}

# bench_host_info: the lines host.txt starts with.
bench_host_info() {
	printf 'date: %s\n' "$(date -u '+%Y-%m-%d %H:%M:%SZ')"
	printf 'system: %s\n' "$(uname -srm)"
	printf 'cpu: %s\n' "$(bench_cpu)"
	# shellcheck disable=SC2086
	printf 'compiler: %s\n' "$($CXX --version 2>&1 | head -n 1)"
	printf 'flags: -std=c++17 -O2 %s\n' "$BENCH_CXXFLAGS"
	if [ "$use_gmp" = 1 ]; then
		printf 'gmp: %s\n' "${gmp_prefix:-compiler default paths}"
	else
		printf 'gmp: not used\n'
	fi
	if [ "$use_boost" = 1 ]; then
		printf 'boost: %s\n' "${boost_prefix:-compiler default paths}"
	else
		printf 'boost: not used\n'
	fi
}
