#include <gtest/gtest.h>

#include "uint256_t.hh"

TEST(Arithmetic, subtract) {
    uint256_t big  (0xffffffffffffffffULL, 0xffffffffffffffffULL, 0xffffffffffffffffULL, 0xffffffffffffffffULL);
    uint256_t small(1);

    EXPECT_EQ(small - small, 0);
    EXPECT_EQ(small - big,   uint256_t(0, 2));
    EXPECT_EQ(big   - small, uint256_t(0xffffffffffffffffULL, 0xffffffffffffffffULL, 0xffffffffffffffffULL, 0xfffffffffffffffeULL));
    EXPECT_EQ(big   - big,   0);
}

TEST(External, subtract) {
    bool     t   = true;
    bool     f   = false;
    uint8_t  u8  = 0xaaULL;
    uint16_t u16 = 0xaaaaULL;
    uint32_t u32 = 0xaaaaaaaaULL;
    uint64_t u64 = 0xaaaaaaaaaaaaaaaaULL;

    const uint256_t val(0xf0f0f0f0f0f0f0f0ULL, 0xf0f0f0f0f0f0f0f0ULL, 0xf0f0f0f0f0f0f0f0ULL, 0xf0f0f0f0f0f0f0f0ULL);

    EXPECT_EQ(t   -  val, uint256_t(0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f11ULL));
    EXPECT_EQ(f   -  val, uint256_t(0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f10ULL));
    EXPECT_EQ(u8  -  val, uint256_t(0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0fbaULL));
    EXPECT_EQ(u16 -  val, uint256_t(0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0fb9baULL));
    EXPECT_EQ(u32 -  val, uint256_t(0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0fb9b9b9baULL));
    EXPECT_EQ(u64 -  val, uint256_t(0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f0fULL, 0xb9b9b9b9b9b9b9baULL));

    EXPECT_EQ(t   -= val, true);
    EXPECT_EQ(f   -= val, true);
    EXPECT_EQ(u8  -= val, (uint8_t)  0xbaULL);
    EXPECT_EQ(u16 -= val, (uint16_t) 0xb9baULL);
    EXPECT_EQ(u32 -= val, (uint32_t) 0xb9b9b9baULL);
    EXPECT_EQ(u64 -= val, (uint64_t) 0xb9b9b9b9b9b9b9baULL);
}