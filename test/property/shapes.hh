// The transform, recursive and Newton division kernels at the shapes that reach each path. Included by property.cc.

#pragma once

#include "common.hh"
#include "digits_reference.hh"

// Recursive-division shapes: divisor and quotient lengths (in limbs) around the threshold, with
// the quotient shorter than, equal to and many times longer than the divisor, over several
// recursion depths. Divisors and dividends that push the quotient estimate to its extremes
// (clamped quotient, two corrections) ride along.
static std::size_t words_for(std::size_t limbs) {
	return (limbs * sizeof(uinteger_t::digit) + 3) / 4;
}

// Products and squares at the transform cutoffs: balanced, one limb apart, within a factor of two, lopsided
// (chunked into transform-size products) and the square, through the operators.
static void check_ntt_shapes() {
	const std::size_t t = U::mul_ntt_threshold, q = U::sqr_ntt_threshold;
	const std::size_t limbs[][2] = {{t, t}, {t + 1, t}, {2 * t - 1, t}, {2 * t, t}, {2 * t + 1, t + 3}, {3 * t + 1, t + 2}, {t + t / 3, t}};
	for (const auto& s : limbs) {
		check_mul(random_words(words_for(s[0])), random_words(words_for(s[1])));
	}
	for (std::size_t n : {q, q + 1, 2 * q + 1}) check_sqr(random_words(words_for(n)));
	// Products too lopsided for one transform are chunked into products it takes.
	if (U::ntt_lopsided * t + 3 <= 3000) {
		check_mul(random_words(words_for(U::ntt_lopsided * t + 3)), random_words(words_for(t)));
		check_mul(random_words(words_for(U::ntt_lopsided * t - 1)), random_words(words_for(t + 1)));
	}
	// All ones: every coefficient of the convolution at its largest.
	words ones(words_for(t + 1), 0xffffffffu);
	check_mul(ones, ones);
	check_sqr(ones);
}

static void check_dc_shapes(int rounds) {
	const std::size_t bits = sizeof(uinteger_t::digit) * 8;
	const std::size_t t = DIV_DC_THRESHOLD;
	const std::size_t ts = DIV_DC_STEP_THRESHOLD;
	std::vector<std::size_t> dns = {t - 1, t, t + 1, 2 * t - 1, 2 * t, 2 * t + 1, 3 * t, 4 * t + 1, 6 * t - 1, 8 * t + 3};
	// Light mode at a threshold above 8 limbs (the default one, where the tiny multiplication cutoffs
	// make every product recurse) takes the divisor lengths at the threshold, twice it and four times it,
	// with quotients up to twice the divisor; the deepest recursions only at a tiny threshold, where they
	// are cheap.
	const bool few = light && t > 8;
	if (few) dns = {t - 1, t, t + 1, 2 * t, 4 * t + 1};
	if (t <= (light ? 8 : 32)) {
		dns.push_back(16 * t + 7);
		dns.push_back(33 * t + 1);
	}
	const uinteger_t one(1);
	for (auto dn : dns) {
		const std::size_t ms[] = {1, ts - 1, ts, ts + 1, t - 1, t, t + 1, dn / 2, dn - 1, dn, dn + 1, 2 * dn, 3 * dn + t / 2, 4 * dn + 1};
		for (auto m : ms) {
			if (!m || (few && m > 2 * dn)) continue;
			for (int k = 0; k < rounds; ++k) {
				words d = random_words(words_for(dn));
				if (k & 1) d.back() |= 0x80000000u;
				check_div(to_uint(random_words(words_for(dn + m))), to_uint(d), "dc shape");
			}

			// Divisors 100..0ff..f and ff..f, dividends just below d * B^m (quotient all ones).
			words e(words_for(dn), 0xffffffffu), f(e);
			e.back() = 0x80000000u;
			for (auto* w : {&e, &f}) {
				auto ud = to_uint(*w);
				check_div((ud << (m * bits)) - one, ud, "dc max quotient");
				check_div((ud << (m * bits)) - uinteger_t(3), ud, "dc max quotient - 2");
			}

			// Top limbs of the dividend equal to the top c limbs of the divisor times B^c - 1:
			// the quotient estimate is two too large.
			auto ue = to_uint(e);
			for (std::size_t c : {m < dn ? m : (dn + 1) / 2, (dn + 1) / 2, dn / 2}) {
				if (!c || c >= dn) continue;
				auto hi = ue >> ((dn - c) * bits);
				check_div((((one << (c * bits)) - one) * hi) << ((dn - c + m - c) * bits), ue, "dc double correction");
				check_div(((((one << (c * bits)) - one) * hi) << ((dn - c + m - c) * bits)) + ue - one, ue, "dc double correction + max rem");
			}
		}
	}
}

// Divisions shaped for the Newton path: divisor sizes around its threshold and quotients shorter than, equal
// to and several times longer than the divisor (one block, two, and a partial block over several), with the
// quotient all ones, a dividend whose top limbs equal the divisor's (the clamped estimate), and divisors that
// are the least and greatest of their size. The sizes are kept small at the default thresholds.
static void check_newton_shapes(int rounds) {
	const std::size_t bits = sizeof(uinteger_t::digit) * 8;
	const std::size_t t = uinteger_t::div_newton_threshold;
	const std::size_t ts = uinteger_t::div_newton_quotient_threshold;
	const bool tiny = t <= 64;
	std::vector<std::size_t> dns = {t, t + 1};
	if (tiny) {
		for (std::size_t dn : {t - 1, 2 * t + 1, 3 * t, 5 * t + 2, 9 * t + 3, 40 * t + 1}) dns.push_back(dn);
	}
	const uinteger_t one(1);
	for (auto dn : dns) {
		if (dn < 2) continue;
		std::vector<std::size_t> ms = {ts, dn - 1, dn + 1};
		if (tiny) {
			for (std::size_t m : {std::size_t(1), ts - 1, ts + 1, dn / 2, dn, 2 * dn - 1, 2 * dn, 2 * dn + 1, 3 * dn + t / 2, 7 * dn + 3}) ms.push_back(m);
		}
		for (auto m : ms) {
			if (!m) continue;
			for (int k = 0; k < (tiny ? rounds : 1); ++k) {
				words d = random_words(words_for(dn));
				if (k & 1) d.back() |= 0x80000000u;
				check_div(to_uint(random_words(words_for(dn + m))), to_uint(d), "newton shape");
			}
			words e(words_for(dn), 0xffffffffu), f(e), g(words_for(dn), 0u);
			e.back() = 0x80000000u;
			g.back() = 0x80000000u;
			g.front() = 1;
			for (auto* w : {&e, &f, &g}) {
				auto ud = to_uint(*w);
				check_div((ud << (m * bits)) - one, ud, "newton max quotient");
				check_div((ud << (m * bits)) - uinteger_t(3), ud, "newton max quotient - 2");
				check_div(ud << (m * bits), ud, "newton exact");
			}
			// The top c limbs of the dividend equal the divisor's top c limbs for the block size c.
			auto ue = to_uint(e);
			const std::size_t c = m < 2 ? 1 : (m - 1) / std::max<std::size_t>(2, (m - 1) / dn + 1) + 1;
			if (c <= dn) {
				auto hi = ue >> ((dn - c) * bits);
				check_div(((((one << (c * bits)) - one) * hi) << ((dn - c + m - c) * bits)) + ue - one, ue, "newton clamped block");
				check_div((hi << ((dn + m - c) * bits)) - one, ue, "newton top equal");
			}
		}
	}
}
