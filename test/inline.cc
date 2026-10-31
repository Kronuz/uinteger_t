// The inline/heap boundary of the limb container: values of up to `inline_capacity` limbs live in
// the object, longer ones in a heap block. Each scenario crosses the boundary in both directions
// (grow past it, shrink back by a shift or a subtraction, copy, move and assign values on each
// side of it, self-assignment, a moved-from value in use) and checks the limbs, the trimmed
// invariant, where the storage is, and that nothing is written past the inline buffer (every
// object sits in front of guard limbs). Build it with UINTEGER_T_INLINE_LIMBS set to 1, 2 or 3 to
// put the boundary at the sizes the arithmetic tests visit. It opens up `private` to read the
// capacity.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <utility>
#include <vector>

#define private public
#include "uinteger_t.hh"
#undef private

using U = uinteger_t;
using digit = U::digit;
static constexpr std::size_t N = U::container::inline_capacity;
static constexpr std::size_t bits = U::digit_bits;
static int failures = 0;
static std::string ctx;
static std::mt19937_64 rng(20261031);

#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: [%s] ", __FILE__, __LINE__, ctx.c_str()); std::printf(__VA_ARGS__); std::printf("\n"); if (++failures > 20) std::exit(1); } } while (0)

using Limbs = std::vector<digit>;

// A value in front of guard limbs: an overflow of the inline buffer would land on them.
struct G {
	static constexpr digit sentinel = static_cast<digit>(0x5ca1ab1e5ca1ab1eull);
	U x;
	digit tail[4];
	G() { for (auto& t : tail) t = sentinel; }
	explicit G(const U& v) : x(v) { for (auto& t : tail) t = sentinel; }
	explicit G(U&& v) : x(std::move(v)) { for (auto& t : tail) t = sentinel; }
	~G() {
		for (auto t : tail) {
			CHECK(t == sentinel, "a limb was written past the inline buffer");
		}
	}
};

static Limbs random_limbs(std::size_t n) {
	Limbs v(n);
	for (auto& d : v) {
		do { d = static_cast<digit>(rng()); } while (!d);
	}
	return v;
}

// The value with these limbs (all nonzero), built through the base-256 constructor.
static U make(const Limbs& v) {
	if (v.empty()) return U();
	std::string bytes;
	for (std::size_t i = v.size(); i-- > 0;) {
		for (std::size_t b = U::digit_octets; b-- > 0;) bytes.push_back(static_cast<char>(v[i] >> (8 * b)));
	}
	return U(bytes, 256);
}

static bool in_object(const U& x) {
	const char* p = reinterpret_cast<const char*>(x.data());
	const char* o = reinterpret_cast<const char*>(&x);
	return p >= o && p < o + sizeof(U);
}

static std::size_t cap(const U& x) { return x._value.capacity(); }

// The value is `ref`, trimmed, and its storage is where the capacity says it is.
static void check(const U& x, const Limbs& ref, const char* what) {
	CHECK(x.size() == ref.size(), "%s: size %zu, expected %zu", what, x.size(), ref.size());
	CHECK(x.size() == ref.size() && std::equal(ref.begin(), ref.end(), x.data()), "%s: limbs differ", what);
	CHECK(!x.size() || x.back() != 0, "%s: leading zero limb", what);
	CHECK(cap(x) >= x.size() && cap(x) >= N, "%s: capacity %zu for %zu limbs", what, cap(x), x.size());
	CHECK(in_object(x) == (cap(x) == N), "%s: storage in object %d, capacity %zu", what, in_object(x), cap(x));
	CHECK(U::container::inline_capacity == N, "inline capacity");
}

// A value with the limbs of `ref` that sits on the heap: built with `extra` more limbs below
// (N + 1 of them, so over the buffer), then shifted down.
static U heap_born(const Limbs& ref) {
	const std::size_t extra = N + 1;
	Limbs all = random_limbs(extra);
	all.insert(all.end(), ref.begin(), ref.end());
	U x = make(all);
	x >>= extra * bits;
	return x;
}

static std::string name(const char* what, std::size_t k) { return std::string(what) + " k=" + std::to_string(k); }

// Appending limb by limb: the first N stay inline, the next spills, later ones grow the block.
static void grow_by_append() {
	for (std::size_t total = 0; total <= 4 * N + 8; total += (total < N + 3 ? 1 : 3)) {
		ctx = name("append", total);
		G g;
		Limbs ref = random_limbs(total);
		for (std::size_t i = 0; i < total; ++i) {
			g.x.append(ref[i]);
			CHECK(in_object(g.x) == (i + 1 <= N), "after %zu limbs: storage in object %d", i + 1, in_object(g.x));
		}
		check(g.x, ref, "appended");
		g.x.append(make(random_limbs(2))); // a value of two limbs on top
		CHECK(g.x.size() == total + 2 && cap(g.x) >= total + 2 && in_object(g.x) == (cap(g.x) == N), "appended a value");
	}
}

// push_back crosses the boundary too: the first N stay inline, the next spills, later ones grow the block.
static void grow_by_push_back() {
	for (std::size_t total = 0; total <= 4 * N + 8; total += (total < N + 3 ? 1 : 3)) {
		ctx = name("push_back", total);
		G g;
		Limbs ref = random_limbs(total);
		for (std::size_t i = 0; i < total; ++i) {
			g.x._value.push_back(ref[i]);
			CHECK(in_object(g.x) == (i + 1 <= N), "after %zu limbs: storage in object %d", i + 1, in_object(g.x));
		}
		check(g.x, ref, "pushed back");
	}
}

// Prepending shifts the limbs up: inline in place, past the buffer into a block.
static void grow_by_prepend() {
	for (std::size_t k = 1; k <= N + 3; ++k) {
		for (std::size_t add = 1; add <= N + 2; add += (add < 3 ? 1 : 2)) {
			ctx = name("prepend", k) + " add=" + std::to_string(add);
			for (int born = 0; born < 2; ++born) {
				Limbs ref = random_limbs(k);
				G g(born ? heap_born(ref) : make(ref));
				Limbs want(add, 0);
				want.insert(want.end(), ref.begin(), ref.end());
				g.x.prepend(add, 0);
				check(g.x, want, "prepended zeros");
				Limbs low = random_limbs(2);
				g.x.prepend(make(low));
				Limbs want2 = low;
				want2.insert(want2.end(), want.begin(), want.end());
				check(g.x, want2, "prepended a value");
			}
		}
	}
}

// Shrinking keeps the block: a value brought down to a few limbs by a shift or a subtraction
// stays on the heap, and keeps working there.
static void shrink_back() {
	for (std::size_t k = 0; k <= N + 2; ++k) {
		ctx = name("shrink", k);
		Limbs ref = random_limbs(k);
		// by a right shift
		G g(heap_born(ref));
		check(g.x, ref, "shifted down");
		CHECK(!in_object(g.x), "a shifted-down value moved back inline");
		// by a subtraction: big - (big - small) = small, trimmed in place
		const Limbs big = random_limbs(N + 3);
		U b = make(big);
		const U small = make(ref);
		U d = b - small;
		U diff = b;
		diff -= d;
		check(diff, ref, "subtracted down");
		CHECK(!in_object(diff), "a subtracted-down value moved back inline");
		// it keeps working on the heap, and regrows in place
		diff += U(7);
		U seven = make(ref);
		seven += U(7);
		CHECK(diff == seven, "sum on a shrunk value");
		diff -= U(7);
		check(diff, ref, "round trip on a shrunk value");
		diff <<= (N + 2) * bits;
		CHECK(!ref.empty() ? diff.size() == ref.size() + N + 2 : diff.size() == 0, "regrown to %zu limbs", diff.size());
		diff >>= (N + 2) * bits;
		check(diff, ref, "regrown and shifted back");
		if (!ref.empty()) {
			diff -= diff;
			check(diff, Limbs(), "subtracted to zero");
			CHECK(!in_object(diff), "zero from a heap value moved inline");
			diff += U(1);
			check(diff, Limbs{1}, "one after zero");
		}
	}
}

static void copy_construct() {
	for (std::size_t k = 0; k <= N + 3; ++k) {
		ctx = name("copy", k);
		const Limbs ref = random_limbs(k);
		for (int born = 0; born < 2; ++born) {
			G src(born ? heap_born(ref) : make(ref));
			G dst(src.x);
			check(dst.x, ref, "copy");
			check(src.x, ref, "source after copy");
			CHECK(in_object(dst.x) == (k <= N), "a copy of %zu limbs: in object %d", k, in_object(dst.x));
			CHECK(dst.x.data() != src.x.data() || k == 0, "a copy shares the source's limbs");
		}
	}
}

// Assignment into every kind of target from every kind of source, either side of the boundary.
static void copy_assign() {
	for (std::size_t k = 0; k <= N + 3; ++k) {
		const Limbs ref = random_limbs(k);
		for (int target = 0; target < 5; ++target) {
			for (int born = 0; born < 2; ++born) {
				ctx = name("copy-assign", k) + " target=" + std::to_string(target) + " heap-born=" + std::to_string(born);
				G src(born ? heap_born(ref) : make(ref));
				Limbs old_limbs;
				switch (target) {
					case 0: break;                                 // zero
					case 1: old_limbs = random_limbs(1); break;    // one limb
					case 2: old_limbs = random_limbs(N); break;    // a full buffer
					case 3: old_limbs = random_limbs(N + 1); break; // just over it
					default: old_limbs = random_limbs(2 * N + 5); break; // a long block
				}
				G dst(target == 4 ? heap_born(old_limbs) : make(old_limbs));
				const digit* before = dst.x.data();
				const std::size_t cap_before = cap(dst.x);
				dst.x = src.x;
				check(dst.x, ref, "assigned");
				check(src.x, ref, "source after assignment");
				if (k <= cap_before) {
					CHECK(dst.x.data() == before && cap(dst.x) == cap_before, "assignment of %zu limbs into room for %zu moved the storage", k, cap_before);
				} else {
					CHECK(cap(dst.x) == k, "assignment past the room allocated %zu, wanted %zu", cap(dst.x), k);
				}
			}
		}
	}
}

// A move takes the block of a heap value and copies an inline one; the source is left zero and
// is still a usable value.
static void move_construct_and_assign() {
	for (std::size_t k = 0; k <= N + 3; ++k) {
		const Limbs ref = random_limbs(k);
		for (int born = 0; born < 2; ++born) {
			ctx = name("move", k) + " heap-born=" + std::to_string(born);
			{
				G src(born ? heap_born(ref) : make(ref));
				const digit* block = src.x.data();
				const bool was_heap = !in_object(src.x);
				G dst(std::move(src.x));
				check(dst.x, ref, "moved to a new value");
				CHECK(!was_heap || dst.x.data() == block, "a heap value was copied, not taken");
				CHECK(in_object(dst.x) == !was_heap, "the destination's storage differs from the source's");
				CHECK(src.x.size() == 0 && !src.x, "moved-from value not zero");
				CHECK(in_object(src.x) && cap(src.x) == N, "moved-from value keeps a block");
				check(src.x, Limbs(), "moved-from");
				src.x += U(7);
				check(src.x, Limbs{7}, "moved-from, then += 7");
				src.x = make(ref);
				check(src.x, ref, "moved-from, then assigned");
			}
			for (int target = 0; target < 3; ++target) {
				ctx = name("move-assign", k) + " target=" + std::to_string(target) + " heap-born=" + std::to_string(born);
				G src(born ? heap_born(ref) : make(ref));
				const digit* block = src.x.data();
				const bool was_heap = !in_object(src.x);
				Limbs old_limbs = target == 0 ? Limbs() : random_limbs(target == 1 ? 1 : 2 * N + 5);
				G dst(target == 2 ? heap_born(old_limbs) : make(old_limbs));
				const digit* dst_block = dst.x.data();
				const std::size_t dst_cap = cap(dst.x);
				dst.x = std::move(src.x);
				check(dst.x, ref, "move-assigned");
				if (was_heap) {
					CHECK(dst.x.data() == block, "a heap source was copied, not taken");
				} else {
					CHECK(dst.x.data() == dst_block && cap(dst.x) == dst_cap, "an inline source moved the destination's storage");
				}
				check(src.x, Limbs(), "moved-from after assignment");
				src.x += U(3);
				check(src.x, Limbs{3}, "moved-from after assignment, then += 3");
			}
		}
	}
}

static void self_assign() {
	for (std::size_t k = 0; k <= N + 3; ++k) {
		for (int born = 0; born < 2; ++born) {
			ctx = name("self", k) + " heap-born=" + std::to_string(born);
			const Limbs ref = random_limbs(k);
			G g(born ? heap_born(ref) : make(ref));
			U& alias = g.x;
			const digit* data = g.x.data();
			g.x = alias;
			check(g.x, ref, "x = x");
			CHECK(g.x.data() == data, "x = x moved the storage");
			g.x = std::move(alias);
			check(g.x, ref, "x = std::move(x)");
			CHECK(g.x.data() == data, "x = std::move(x) moved the storage");
			g.x += alias;
			U twice = make(ref);
			twice += make(ref);
			CHECK(g.x == twice, "x += x");
			g.x -= alias;
			CHECK(!g.x, "x -= x");
		}
	}
}

// Results that land on either side of the buffer: a sum that carries out of the top limb, a
// product twice as long, shifts by a limb, with the result also being an operand.
static void cross_by_arithmetic() {
	for (std::size_t k = 1; k <= N + 2; ++k) {
		ctx = name("arith", k);
		const U ones = (U(1) << (k * bits)) - U(1);
		const U p = U(1) << (k * bits);
		G s(ones + U(1));
		CHECK(s.x == p && s.x.size() == k + 1, "ones + 1: %zu limbs", s.x.size());
		CHECK(in_object(s.x) == (k + 1 <= N), "ones + 1 in object %d", in_object(s.x));
		s.x -= U(1);
		CHECK(s.x == ones && s.x.size() == k, "(ones + 1) - 1");
		CHECK(in_object(s.x) == (k + 1 <= N), "the subtracted-down value moved");
		G c(s.x);
		CHECK(c.x == ones && in_object(c.x) == (k <= N), "copy of (ones + 1) - 1");

		G sq(ones * ones);
		CHECK(sq.x == ((U(1) << (2 * k * bits)) - (U(1) << (k * bits + 1)) + U(1)) && sq.x.size() == 2 * k, "ones * ones: %zu limbs", sq.x.size());
		CHECK(in_object(sq.x) == (2 * k <= N), "ones * ones in object %d", in_object(sq.x));

		G x(ones);
		x.x += x.x;
		CHECK(x.x == (U(1) << (k * bits + 1)) - U(2), "x += x");
		x.x = ones;
		x.x *= x.x;
		CHECK(x.x == sq.x, "x *= x");
		x.x = ones;
		x.x <<= bits;
		CHECK(x.x == ones * (U(1) << bits), "x <<= limb");
		x.x >>= bits;
		CHECK(x.x == ones, "x >>= limb");
		x.x = p;
		x.x >>= 1;
		CHECK(x.x.size() == k && x.x == p / U(2), "2^(k limbs) >> 1");
		x.x = ones;
		x.x = x.x * U(3) + U(1);
		CHECK(x.x == ones * U(3) + U(1), "x = x * 3 + 1");
		x.x = x.x - ones - ones;
		CHECK(x.x == ones + U(1), "x = x - ones - ones");
		G q(ones);
		U r = q.x % U(1000003);
		U quo = q.x / U(1000003);
		CHECK(quo * U(1000003) + r == ones, "quotient and remainder");
		CHECK(in_object(quo) == (k <= N), "quotient storage");
	}
}

// The capacity policy: exact from the inline buffer, at least doubling from a block, so growing
// one limb at a time does a number of allocations logarithmic in the final size.
static void growth_policy() {
	ctx = "growth policy";
	G g;
	g.x.resize(N);
	CHECK(cap(g.x) == N, "resize to N left capacity %zu", cap(g.x));
	g.x.resize(N + 1);
	CHECK(cap(g.x) == N + 1, "first spill allocated %zu limbs, wanted exactly N + 1", cap(g.x));
	g.x.resize(N + 2);
	CHECK(cap(g.x) >= 2 * (N + 1), "growth from a block allocated %zu limbs, wanted at least %zu", cap(g.x), 2 * (N + 1));
	std::size_t blocks = 0;
	std::size_t last = cap(g.x);
	for (std::size_t n = N + 3; n < 4096; ++n) {
		g.x.resize(n);
		if (cap(g.x) != last) {
			++blocks;
			last = cap(g.x);
		}
	}
	CHECK(blocks <= 12, "growing to 4096 limbs one at a time took %zu allocations", blocks);
	g.x.reserve(10);
	CHECK(cap(g.x) >= 4095, "reserve below the capacity shrank it");
	U fresh;
	fresh.reserve(N);
	CHECK(in_object(fresh), "reserve(N) left the buffer");
	fresh.reserve(N + 1);
	CHECK(!in_object(fresh) && cap(fresh) == N + 1, "reserve(N + 1): capacity %zu", cap(fresh));
	// zero-filled growth
	U z;
	z.resize(N + 3);
	CHECK(std::all_of(z.data(), z.data() + z.size(), [](digit d) { return d == 0; }), "resize did not zero-fill");
	std::fill(z.data(), z.data() + z.size(), digit(7));
	z.resize(N + 1);
	z.resize(N + 3);
	CHECK(z.data()[N] == 7 && z.data()[N + 1] == 0 && z.data()[N + 2] == 0, "regrowing a shrunk value did not zero-fill");
}

// The object is a pointer, a size, a capacity and the inline buffer.
static_assert(sizeof(U) == (3 * sizeof(std::size_t) + N * sizeof(digit) + sizeof(std::size_t) - 1) / sizeof(std::size_t) * sizeof(std::size_t), "a uinteger_t is a pointer, a size, a capacity and the inline buffer");
static_assert(sizeof(U) == sizeof(U::container), "a uinteger_t is just its limbs");

int main() {
	grow_by_append();
	grow_by_push_back();
	grow_by_prepend();
	shrink_back();
	copy_construct();
	copy_assign();
	move_construct_and_assign();
	self_assign();
	cross_by_arithmetic();
	growth_policy();
	if (failures) {
		std::printf("inline: %d failures\n", failures);
		return 1;
	}
	std::printf("inline OK (inline capacity %zu limbs, sizeof %zu)\n", N, sizeof(U));
	return 0;
}
