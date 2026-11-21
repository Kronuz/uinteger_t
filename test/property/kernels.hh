// Multiplication kernels on dirty buffers with guard limbs, Toom-4 patterns and size classes. Included by property.cc.

static void check_kernel_mul(const std::vector<digit>& a, const std::vector<digit>& b) {
	const std::size_t an = a.size(), bn = b.size();
	const std::size_t sn = U::k_mul_scratch_size(an, bn);
	std::vector<digit> r(an + bn + guard_limbs), s(sn + guard_limbs);
	dirty(r, an + bn);
	dirty(s, sn);
	U::k_mul(r.data(), a.data(), an, b.data(), bn, s.data());
	CHECK(limbs_to_words(r.data(), an + bn) == ref_mul(limbs_to_words(a.data(), an), limbs_to_words(b.data(), bn)), "k_mul %zu x %zu limbs", an, bn);
	CHECK(guard_intact(r, an + bn) && guard_intact(s, sn), "k_mul %zu x %zu limbs wrote past its buffers (scratch %zu)", an, bn, sn);
	// Karatsuba and Toom-3 need about 3.3 limbs per limb of product; the transform, at most 5.33 N words for a
	// length N under 1.5 times the product's words, is within the same bound.
	CHECK(sn <= 8 * (an + bn) + 64, "k_mul %zu x %zu limbs asks for %zu scratch limbs", an, bn, sn);
}

static void check_kernel_sqr(const std::vector<digit>& a) {
	const std::size_t n = a.size();
	const std::size_t sn = U::k_sqr_scratch_size(n);
	std::vector<digit> r(2 * n + guard_limbs), s(sn + guard_limbs);
	dirty(r, 2 * n);
	dirty(s, sn);
	U::k_sqr(r.data(), a.data(), n, s.data());
	auto w = limbs_to_words(a.data(), n);
	CHECK(limbs_to_words(r.data(), 2 * n) == ref_mul(w, w), "k_sqr %zu limbs", n);
	CHECK(guard_intact(r, 2 * n) && guard_intact(s, sn), "k_sqr %zu limbs wrote past its buffers (scratch %zu)", n, sn);
	// Squaring below the transform needs about 5n limbs; the transform, at most 4.33 N words, up to 13 n limbs,
	// within 8 limbs per limb of the square.
	CHECK(sn <= (U::k_sqr_uses_ntt(n) ? 8 * (2 * n) + 64 : 8 * n + 64), "k_sqr %zu limbs asks for %zu scratch limbs", n, sn);
}

// Operands built piece by piece (k limbs per piece, as the Toom splits do), each piece one of several patterns, so
// the evaluations at 1, -1, 2, -2 and 1/2 reach zero, maximal and tied values and the interpolation both signs.
static std::vector<digit> pattern_limbs(std::size_t n, std::size_t k) {
	const digit ones = static_cast<digit>(~digit(0));
	std::vector<digit> v(n);
	for (std::size_t p = 0; p * k < n; ++p) {
		const std::size_t hi = std::min(n, (p + 1) * k);
		const unsigned style = static_cast<unsigned>(rng() % 6);
		for (std::size_t i = p * k; i < hi; ++i) {
			switch (style) {
				case 0: v[i] = 0; break;
				case 1: v[i] = i == p * k ? digit(1) : digit(0); break;
				case 2: v[i] = ones; break;
				case 3: v[i] = static_cast<digit>(rng()); break;
				case 4: v[i] = static_cast<digit>(0xaaaaaaaaaaaaaaaaull); break;
				default: v[i] = i + 1 == hi ? ones : digit(0); break;
			}
		}
	}
	return v;
}

// Toom-4 on every split remainder and on the shapes around its fourth-piece boundary (bn = 3k + 1, +2, halfway,
// balanced), with patterned pieces.
static void check_toom4_patterns() {
	const std::size_t tm = U::mul_toom4_threshold;
	auto sizes = [](std::size_t t) {
		std::vector<std::size_t> v;
		if (t > 1200) {
			// A cutoff out of reach (a build that disables Toom-4) has no sizes to visit.
			return v;
		}
		if (t <= 24) {
			for (std::size_t n = std::max<std::size_t>(t, 2); n <= 56; ++n) v.push_back(n);
		} else {
			v = {t, t + 1, t + 2, t + 3, t + 4, 2 * t + 5};
		}
		return v;
	};
	// Light mode keeps the shapes and drops the repeats (one patterned product per shape).
	const int reps = light ? 1 : (tm <= 24 ? 6 : 10);
	for (std::size_t an : sizes(tm)) {
		const std::size_t k = (an + 3) / 4;
		for (std::size_t bn : {an, an - 1, 3 * k + 1, 3 * k + 2, (an + 3 * k + 1) / 2}) {
			if (bn <= 3 * k || bn > an) continue;
			for (int rep = 0; rep < reps; ++rep) check_kernel_mul(pattern_limbs(an, k), pattern_limbs(bn, k));
			check_kernel_mul(std::vector<digit>(an, static_cast<digit>(~digit(0))), std::vector<digit>(bn, static_cast<digit>(~digit(0))));
		}
	}
}

// The size cap on Toom-4 keeps its scratch, in bytes, below the range of std::size_t: with it the dispatch
// refuses the cap's neighbour and still takes the cap itself (when the cutoff allows).
static void check_toom4_cap() {
	const std::size_t cap = U::toom4_max_limbs;
	const std::size_t most = std::numeric_limits<std::size_t>::max();
	CHECK(cap <= most / (32 * sizeof(digit)), "toom4_max_limbs %zu is above the size_t bound", cap);
	CHECK(!U::k_mul_uses_toom4(cap + 1, cap + 1), "Toom-4 taken above its size cap");
	CHECK(!U::k_mul_uses_toom4(most / 2, most / 2), "Toom-4 taken for a half-range operand");
	if (U::mul_toom4_threshold <= cap) CHECK(U::k_mul_uses_toom4(cap, cap), "Toom-4 refused at its size cap");
}

// The Toom-4 predicates at their edges: the cutoff (where a balanced product is big enough to have a fourth
// piece) and the fourth-piece boundary bn = 3k, 3k + 1 for k = ceil(an / 4). A wrong edge still multiplies
// correctly, so only the predicates themselves show it.
static void check_toom4_dispatch() {
	const std::size_t tm = U::mul_toom4_threshold;
	if (tm >= 14 && tm <= 100000) {
		CHECK(U::k_mul_uses_toom4(tm, tm), "Toom-4 refused for a balanced %zu-limb product", tm);
		CHECK(!U::k_mul_uses_toom4(tm - 1, tm - 1), "Toom-4 taken below its cutoff");
	}
	const std::size_t lo = std::max<std::size_t>(13, 4 * tm / 3 + 4);
	for (std::size_t an = lo; an <= lo + 40 && tm <= 100000; ++an) {
		const std::size_t k = (an + 3) / 4;
		CHECK(U::k_mul_uses_toom4(an, 3 * k + 1), "Toom-4 refused for %zu x %zu limbs", an, 3 * k + 1);
		CHECK(!U::k_mul_uses_toom4(an, 3 * k), "Toom-4 taken for %zu x %zu limbs, which has no fourth piece", an, 3 * k);
	}
}

// Every size pair up to a small bound, then the sizes around each cutoff (and its multiples)
// against the shapes that steer the dispatch: balanced, one or two short, the Toom-3 and Toom-4
// third- and fourth-piece boundaries, the Karatsuba half boundary, and the lopsided chunking.
static void check_kernel_sizes() {
	for (std::size_t an = 1; an <= 40; ++an) {
		check_kernel_sqr(random_limbs(an));
		for (std::size_t bn = 1; bn <= an; ++bn) check_kernel_mul(random_limbs(an), random_limbs(bn));
	}
	std::vector<std::size_t> sizes;
	// The transform cutoffs join the list when the kernel checks against the schoolbook reference stay cheap;
	// at the default cutoffs check_ntt_shapes covers them.
	std::vector<std::size_t> cutoffs = {U::mul_karatsuba_threshold, U::mul_toom3_threshold, U::sqr_karatsuba_threshold, U::sqr_toom3_threshold};
	// (by value: push_back would bind a reference to the constexpr member, which needs a definition before C++17)
	const std::size_t toom4_mul = U::mul_toom4_threshold;
	// A cutoff out of reach (a build that disables Toom-4) has no sizes to visit.
	if (toom4_mul <= 600) cutoffs.push_back(toom4_mul);
	const std::size_t ntt_mul = U::mul_ntt_threshold, ntt_sqr = U::sqr_ntt_threshold;
	if (ntt_mul <= 200) cutoffs.push_back(ntt_mul);
	if (ntt_sqr <= 200) cutoffs.push_back(ntt_sqr);
	for (std::size_t t : cutoffs) {
		for (std::size_t m : {1, 2, 3}) {
			// The multiples reach the recursion of an algorithm into itself; light mode runs
			// them up to 150 limbs (the reference multiply is quadratic), and the builds with
			// tiny cutoffs reach every such recursion at small sizes.
			if (light && m * t > 150 && m > 1) continue;
			for (std::size_t d = 0; d < 5; ++d) {
				if (m * t + d >= 2) sizes.push_back(m * t + d - 2);
			}
		}
	}
	std::sort(sizes.begin(), sizes.end());
	sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
	for (std::size_t n : sizes) {
		if (!n) continue;
		check_kernel_sqr(random_limbs(n));
		std::vector<std::size_t> others = {n, n - 1, n - 2, n - n / 6, (2 * n + 2) / 3, 2 * n / 3, n / 2 + 1, (n + 1) / 2, n / 2, n / 3 + 1,
			3 * ((n + 3) / 4), 3 * ((n + 3) / 4) + 1, 3 * ((n + 3) / 4) + 2, 3 * n / 4};
		for (std::size_t bn : others) {
			if (!bn || bn > n) continue;
			check_kernel_mul(random_limbs(n), random_limbs(bn));
			check_kernel_mul(random_limbs(2 * n + 1), random_limbs(bn));
		}
	}
}
