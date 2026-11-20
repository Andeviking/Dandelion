#include <array>
#include <bit>
#include <bitset>
#include <cstdlib>
#include <random>
#include <iterator>
#include <string>

#include "test_support.h"

namespace dandelion::testing::util {
namespace {

static_assert(sizeof(BitVector) == 16);
static_assert(alignof(BitVector) == alignof(std::uint64_t));
static_assert(kNothrowOwnership<BitVector>);

using Binary = NumericResult<BitVector> (*)(const BitVector&, const BitVector&);
using Predicate = NumericResult<bool> (*)(const BitVector&, const BitVector&);

constexpr std::pair<const char*, Binary> kBinaryOps[] = {
    {"and", BitVector::And},   {"or", BitVector::Or},
    {"xor", BitVector::Xor},   {"nand", BitVector::Nand},
    {"nor", BitVector::Nor},   {"xnor", BitVector::Xnor},
    {"add", BitVector::Add},   {"sub", BitVector::Sub},
    {"mul", BitVector::Mul},   {"udiv", BitVector::Udiv},
    {"urem", BitVector::Urem}, {"sdiv", BitVector::Sdiv},
    {"srem", BitVector::Srem}, {"smod", BitVector::Smod},
    {"shl", BitVector::Shl},   {"lshr", BitVector::Lshr},
    {"ashr", BitVector::Ashr}, {"comp", BitVector::Comp}};
constexpr std::pair<const char*, Predicate> kPredicateOps[] = {
    {"ult", BitVector::Ult},     {"ule", BitVector::Ule},
    {"ugt", BitVector::Ugt},     {"uge", BitVector::Uge},
    {"slt", BitVector::Slt},     {"sle", BitVector::Sle},
    {"sgt", BitVector::Sgt},     {"sge", BitVector::Sge},
    {"uaddo", BitVector::Uaddo}, {"saddo", BitVector::Saddo},
    {"umulo", BitVector::Umulo}, {"smulo", BitVector::Smulo},
    {"usubo", BitVector::Usubo}, {"ssubo", BitVector::Ssubo},
    {"sdivo", BitVector::Sdivo}};

int Signed(int residue, int modulus) {
  return residue < modulus / 2 ? residue : residue - modulus;
}

std::uint64_t Residue(int value, int modulus) {
  return static_cast<std::uint64_t>((value % modulus + modulus) % modulus);
}

TEST(UtilBitVector, ConstructionIdentityInspectionAndText) {
  for (unsigned word = 0; word < 256; ++word) {
    SCOPED_TRACE(word);
    const auto bits = Bits(8, word);
    ExpectBits(bits, 8, word);
    EXPECT_EQ(bits.BackingBytes(), 0u);
    EXPECT_EQ(bits.EffectiveBitLength(), std::bit_width(word));
    EXPECT_EQ(bits.Chunk(0), word);
    EXPECT_EQ(bits.Chunk(1), 0u);
    EXPECT_EQ(bits.Chunk(UINT64_MAX), 0u);
    for (unsigned i = 0; i <= 8; ++i) {
      EXPECT_EQ(bits.TestBit(i), (word & (1u << i)) != 0);
    }
    EXPECT_FALSE(bits.TestBit(UINT64_MAX));
    const auto exact =
        Take(BitVector::FromIntegerExact(IntegerView(8), Unsigned(word)));
    EXPECT_EQ(bits, exact);
    EXPECT_EQ(bits.Hash(), exact.Hash());
    EXPECT_NE(bits, Bits(9, word));
  }
  const auto width_one_hash = Bits(1, 1).Hash();
  bool width_changes_hash = false;
  for (std::uint32_t width = 2; width <= 65; ++width) {
    width_changes_hash |= Bits(width, 1).Hash() != width_one_hash;
  }
  EXPECT_TRUE(width_changes_hash);
  ExpectBits(Take(BitVector::Zero(IntegerView(8))), 8, 0);
  ExpectBits(Take(BitVector::AllOnes(IntegerView(8))), 8, 255);
  ExpectError(BitVector::FromUint64(IntegerView(8), 256),
              NumericError::kValueOutOfRange);
  ExpectError(BitVector::FromIntegerExact(IntegerView(8), IntegerView(-1)),
              NumericError::kValueOutOfRange);
  ExpectError(BitVector::FromIntegerExact(IntegerView(8), IntegerView(256)),
              NumericError::kValueOutOfRange);
  ExpectBits(
      Take(BitVector::FromIntegerModulo(IntegerView(8), IntegerView(-1))), 8,
      255);
  ExpectBits(
      Take(BitVector::FromIntegerModulo(IntegerView(8), IntegerView(256))), 8,
      0);
  ExpectBits(Take(BitVector::FromDecimalModulo(IntegerView(8), "000257")), 8,
             1);
  ExpectBits(Take(BitVector::FromDecimalModulo(
                 IntegerView(8), "340282366920938463463374607431768211711")),
             8, 255);

  const auto binary = Take(BitVector::FromBinaryDigits("000010101011"));
  const auto hex = Take(BitVector::FromHexDigits("0aB"));
  ExpectBits(binary, 12, 0xab);
  EXPECT_EQ(binary, hex);
  EXPECT_EQ(binary.Hash(), hex.Hash());
  EXPECT_EQ(Take(hex.ToBinary()), "000010101011");
  EXPECT_NE(hex, Take(BitVector::FromHexDigits("aB")));
  ExpectBits(Take(BitVector::FromBinaryDigits("0")), 1, 0);
  ExpectBits(Take(BitVector::FromHexDigits("000")), 12, 0);
  for (const auto text : {"", "-1", "+1", " 1", "1 ", "#b1", "#x1", "g"}) {
    SCOPED_TRACE(text);
    ExpectError(BitVector::FromBinaryDigits(text), NumericError::kInvalidText);
    ExpectError(BitVector::FromHexDigits(text), NumericError::kInvalidText);
    ExpectError(BitVector::FromDecimalModulo(IntegerView(8), text),
                NumericError::kInvalidText);
  }
  ExpectError(BitVector::FromBinaryDigits("102"), NumericError::kInvalidText);
  ExpectError(BitVector::FromDecimalModulo(IntegerView(8), "1.0"),
              NumericError::kInvalidText);
  const std::string_view nul("1\0", 2);
  ExpectError(BitVector::FromBinaryDigits(nul), NumericError::kInvalidText);
  ExpectError(BitVector::FromHexDigits(nul), NumericError::kInvalidText);
  ExpectError(BitVector::FromDecimalModulo(IntegerView(8), nul),
              NumericError::kInvalidText);
}

TEST(UtilBitVector, WidthAndIndexErrorsUseCheckedExactInputs) {
  for (const auto width : {IntegerView(0), IntegerView(-1),
                           Unsigned(std::uint64_t{1} + UINT32_MAX)}) {
    const auto error = width.Sign() > 0 ? NumericError::kSizeLimit
                                        : NumericError::kInvalidArgument;
    ExpectError(BitVector::FromUint64(width, 0), error);
    ExpectError(BitVector::FromIntegerExact(width, IntegerView(0)), error);
    ExpectError(BitVector::FromIntegerModulo(width, IntegerView(0)), error);
    ExpectError(BitVector::FromDecimalModulo(width, "0"), error);
    ExpectError(BitVector::Zero(width), error);
    ExpectError(BitVector::AllOnes(width), error);
  }
  const auto small = Bits(2, 0);
  const auto different = Bits(3, 0);
  for (const auto& [name, operation] : kBinaryOps) {
    SCOPED_TRACE(name);
    ExpectError(operation(small, different), NumericError::kInvalidArgument);
  }
  for (const auto& [name, operation] : kPredicateOps) {
    SCOPED_TRACE(name);
    ExpectError(operation(small, different), NumericError::kInvalidArgument);
  }
  const auto huge = Int("18446744073709551616");
  ExpectError(BitVector::FromUint64(huge.Borrow(), 0),
              NumericError::kSizeLimit);
  ExpectError(BitVector::Extract(small, IntegerView(0), IntegerView(1)),
              NumericError::kInvalidArgument);
  ExpectError(BitVector::Extract(small, IntegerView(2), IntegerView(0)),
              NumericError::kInvalidArgument);
  ExpectError(BitVector::Extract(small, IntegerView(0), IntegerView(-1)),
              NumericError::kInvalidArgument);
  ExpectError(BitVector::Extract(small, huge.Borrow(), IntegerView(0)),
              NumericError::kInvalidArgument);
  ExpectError(BitVector::Repeat(small, IntegerView(0)),
              NumericError::kInvalidArgument);
  for (const auto index : {IntegerView(-1), huge.Borrow()}) {
    const auto error = index.Sign() < 0 ? NumericError::kInvalidArgument
                                        : NumericError::kSizeLimit;
    ExpectError(BitVector::Repeat(small, index), error);
    ExpectError(BitVector::ZeroExtend(small, index), error);
    ExpectError(BitVector::SignExtend(small, index), error);
  }
  ExpectError(BitVector::RotateLeft(small, IntegerView(-1)),
              NumericError::kInvalidArgument);
  ExpectError(BitVector::RotateRight(small, IntegerView(-1)),
              NumericError::kInvalidArgument);
}

TEST(UtilBitVector, ExhaustiveTinyUnaryAndBinaryOperations) {
  for (std::uint32_t width = 1; width <= 6; ++width) {
    SCOPED_TRACE(width);
    const int modulus = 1 << width;
    const int mask = modulus - 1;
    const int min = -modulus / 2;
    const int max = modulus / 2 - 1;
    const auto signed_overflow = [=](int result) {
      return result < min || result > max;
    };
    for (int u = 0; u < modulus; ++u) {
      SCOPED_TRACE(u);
      const auto a = Bits(width, static_cast<std::uint64_t>(u));
      const int s = Signed(u, modulus);
      ExpectBits(Take(BitVector::Not(a)), width, Residue(~u, modulus));
      ExpectBits(Take(BitVector::Neg(a)), width, Residue(-s, modulus));
      EXPECT_EQ(BitVector::Nego(a), signed_overflow(-s));
      for (int v = 0; v < modulus; ++v) {
        SCOPED_TRACE(v);
        const auto b = Bits(width, static_cast<std::uint64_t>(v));
        const int t = Signed(v, modulus);
        // Quotient signs and remainders are derived from unsigned magnitudes.
        int signed_q = s < 0 ? 1 : mask;
        int signed_r = s;
        int signed_mod = s;
        if (t != 0) {
          signed_q = std::abs(s) / std::abs(t);
          if ((s < 0) != (t < 0)) signed_q = -signed_q;
          signed_r = s - signed_q * t;
          signed_mod = signed_r;
          if (signed_r != 0 && (signed_r < 0) != (t < 0)) signed_mod += t;
        }
        int arithmetic_shift = s < 0 ? -1 : 0;
        if (v < static_cast<int>(width)) {
          const int scale = 1 << v;
          arithmetic_shift = s < 0 ? -((-s + scale - 1) / scale) : s / scale;
        }
        const std::array answers = {u & v,
                                    u | v,
                                    u ^ v,
                                    ~(u & v),
                                    ~(u | v),
                                    ~(u ^ v),
                                    u + v,
                                    u - v,
                                    u * v,
                                    v == 0 ? mask : u / v,
                                    v == 0 ? u : u % v,
                                    signed_q,
                                    signed_r,
                                    signed_mod,
                                    v < static_cast<int>(width) ? u << v : 0,
                                    v < static_cast<int>(width) ? u >> v : 0,
                                    arithmetic_shift,
                                    static_cast<int>(u == v)};
        static_assert(answers.size() == std::size(kBinaryOps));
        for (std::size_t i = 0; i < answers.size(); ++i) {
          const auto& [name, operation] = kBinaryOps[i];
          SCOPED_TRACE(name);
          ExpectBits(Take(operation(a, b)),
                     name == std::string_view("comp") ? 1 : width,
                     Residue(answers[i], modulus));
        }
        const std::array predicates = {u<v, u <= v, u> v,
                                       u >= v,
                                       s<t, s <= t, s>
                                           t,
                                       s >= t,
                                       u + v >= modulus,
                                       signed_overflow(s + t),
                                       u * v >= modulus,
                                       signed_overflow(s * t),
                                       u - v < 0,
                                       signed_overflow(s - t),
                                       t != 0 && signed_overflow(signed_q)};
        static_assert(predicates.size() == std::size(kPredicateOps));
        for (std::size_t i = 0; i < predicates.size(); ++i) {
          const auto& [name, operation] = kPredicateOps[i];
          EXPECT_EQ(Take(operation(a, b)), predicates[i]) << name;
        }
      }
    }
  }
}

TEST(UtilBitVector, ExhaustiveTinyStructuralOperationsAndIntegerConversions) {
  for (std::uint32_t width = 1; width <= 6; ++width) {
    SCOPED_TRACE(width);
    const int modulus = 1 << width;
    const auto mask = static_cast<std::uint64_t>(modulus - 1);
    for (int n = -2 * modulus; n <= 2 * modulus; ++n) {
      SCOPED_TRACE(n);
      ExpectBits(
          Take(BitVector::FromIntegerModulo(Unsigned(width), IntegerView(n))),
          width, Residue(n, modulus));
    }
    for (std::uint64_t u = 0; u <= mask; ++u) {
      SCOPED_TRACE(u);
      const auto a = Bits(width, u);
      const int s = Signed(static_cast<int>(u), modulus);
      EXPECT_EQ(Int64(BitVector::ToUnsignedInteger(a)), u);
      EXPECT_EQ(Int64(BitVector::ToSignedInteger(a)), s);
      for (std::uint32_t other_width = 1; other_width <= 6; ++other_width) {
        SCOPED_TRACE(other_width);
        for (std::uint64_t v = 0; v < (1u << other_width); ++v) {
          SCOPED_TRACE(v);
          ExpectBits(Take(BitVector::Concat(a, Bits(other_width, v))),
                     width + other_width, (u << other_width) | v);
        }
      }
      for (std::uint32_t low = 0; low < width; ++low) {
        for (std::uint32_t high = low; high < width; ++high) {
          SCOPED_TRACE(low);
          SCOPED_TRACE(high);
          const auto result_width = high - low + 1;
          ExpectBits(Take(BitVector::Extract(a, Unsigned(high), Unsigned(low))),
                     result_width, (u >> low) & ((1u << result_width) - 1));
        }
      }

      std::uint64_t repeated = 0;
      for (std::uint32_t count = 0; count <= 6; ++count) {
        SCOPED_TRACE(count);
        ExpectBits(Take(BitVector::ZeroExtend(a, Unsigned(count))),
                   width + count, u);
        ExpectBits(Take(BitVector::SignExtend(a, Unsigned(count))),
                   width + count, Residue(s, 1 << (width + count)));
        if (count != 0) {
          repeated = (repeated << width) + u;
          ExpectBits(Take(BitVector::Repeat(a, Unsigned(count))), width * count,
                     repeated);
        }
      }
      for (std::uint32_t n = 0; n <= 3 * width; ++n) {
        SCOPED_TRACE(n);
        std::uint64_t left = 0;
        std::uint64_t right = 0;
        for (std::uint32_t bit = 0; bit < width; ++bit) {
          if (((u >> bit) & 1) == 0) continue;
          left |= std::uint64_t{1} << ((bit + n) % width);
          right |= std::uint64_t{1} << ((bit + 3 * width - n) % width);
        }
        ExpectBits(Take(BitVector::RotateLeft(a, Unsigned(n))), width, left);
        ExpectBits(Take(BitVector::RotateRight(a, Unsigned(n))), width, right);
      }
    }
  }
}

TEST(UtilBitVector, WordAndMultiLimbBoundaryResults) {
  constexpr auto kHigh = std::uint64_t{1} << 63;
  const auto two64 = Int("18446744073709551616");
  const auto minus_two64 = Int("-18446744073709551616");
  ExpectError(BitVector::FromIntegerExact(IntegerView(64), two64.Borrow()),
              NumericError::kValueOutOfRange);
  for (const auto value : {kHigh, UINT64_MAX}) {
    const auto b = Bits(64, value);
    EXPECT_EQ(b.EffectiveBitLength(), 64u);
    EXPECT_EQ(b.BackingBytes(), 0u);
    EXPECT_EQ(b.TryUint64(), value);
    EXPECT_EQ(
        Take(BitVector::FromIntegerExact(IntegerView(64), Unsigned(value))), b);
    EXPECT_EQ(Take(BitVector::ToUnsignedInteger(b)).ToDecimal(),
              std::to_string(value));
  }
  EXPECT_EQ(Int64(BitVector::ToSignedInteger(Bits(64, kHigh))), INT64_MIN);
  EXPECT_EQ(Int64(BitVector::ToSignedInteger(Bits(64, UINT64_MAX))), -1);
  ExpectBits(Take(BitVector::Add(Bits(63, kHigh - 1), Bits(63, 1))), 63, 0);
  ExpectBits(Take(BitVector::Sub(Bits(64, 0), Bits(64, 1))), 64, UINT64_MAX);
  const auto wide =
      Take(BitVector::FromIntegerExact(IntegerView(65), two64.Borrow()));
  const auto max65 = Take(BitVector::AllOnes(IntegerView(65)));
  EXPECT_EQ(wide.EffectiveBitLength(), 65u);
  EXPECT_EQ(wide.Chunk(0), 0u);
  EXPECT_EQ(wide.Chunk(1), 1u);
  EXPECT_FALSE(wide.TryUint64());
  EXPECT_EQ(Take(BitVector::ToSignedInteger(wide)).ToDecimal(),
            "-18446744073709551616");
  EXPECT_EQ(Take(BitVector::ToUnsignedInteger(wide)).ToDecimal(),
            "18446744073709551616");
  EXPECT_EQ(
      Take(BitVector::FromIntegerModulo(IntegerView(65), minus_two64.Borrow())),
      wide);
  for (const auto value : {two64.Borrow(), minus_two64.Borrow()}) {
    ExpectBits(Take(BitVector::FromIntegerModulo(IntegerView(64), value)), 64,
               0);
  }
  const auto low_ones = Take(BitVector::Sub(wide, Bits(65, 1)));
  ExpectBits(low_ones, 65, UINT64_MAX);
  EXPECT_EQ(low_ones.BackingBytes(), 0u);
  EXPECT_EQ(Take(BitVector::Add(low_ones, Bits(65, 1))), wide);
  EXPECT_EQ(Take(BitVector::Neg(wide)), wide);
  EXPECT_EQ(Take(BitVector::Sdiv(wide, max65)), wide);
  EXPECT_TRUE(Take(BitVector::Sdivo(wide, max65)));
  for (const auto factor : {1u, 63u, 64u, 65u, 66u}) {
    const auto count = Bits(65, factor);
    ExpectBits(Take(BitVector::Lshr(wide, count)), 65,
               factor <= 64 ? std::uint64_t{1} << (64 - factor) : 0);
  }
  EXPECT_EQ(Take(BitVector::Shl(wide, Bits(65, 0))), wide);
  EXPECT_EQ(Take(BitVector::Lshr(wide, Bits(65, 0))), wide);
  EXPECT_EQ(Take(BitVector::Ashr(wide, Bits(65, 0))), wide);
  for (const auto* count : {&wide, &max65}) {
    ExpectBits(Take(BitVector::Shl(wide, *count)), 65, 0);
    ExpectBits(Take(BitVector::Lshr(wide, *count)), 65, 0);
    EXPECT_EQ(Take(BitVector::Ashr(wide, *count)), max65);
    ExpectBits(Take(BitVector::Ashr(Bits(65, 1), *count)), 65, 0);
  }
  const auto joined = Take(BitVector::Concat(Bits(4, 0xb), Bits(63, 1u)));
  ExpectBits(Take(BitVector::Extract(joined, IntegerView(66), IntegerView(62))),
             5, 0x16);
  const auto sign =
      Take(BitVector::SignExtend(Bits(64, kHigh), IntegerView(65)));
  EXPECT_EQ(sign.Width(), 129u);
  EXPECT_EQ(sign.Chunk(0), kHigh);
  EXPECT_EQ(sign.Chunk(1), UINT64_MAX);
  EXPECT_EQ(sign.Chunk(2), 1u);
  const auto repeat = Take(BitVector::Repeat(Bits(5, 21), IntegerView(30)));
  EXPECT_EQ(repeat.Width(), 150u);
  ExpectBits(Take(BitVector::Extract(repeat, IntegerView(69), IntegerView(60))),
             10, 0x2b5);
}

TEST(UtilBitVector, ExtractSingleWordAcrossStorageForms) {
  constexpr std::uint64_t kWord = 0x8192a3b4c5d6e7f8;
  const auto word = Bits(64, kWord);
  const auto wide = Take(BitVector::Concat(Bits(1, 1), word));
  for (const auto* value : {&word, &wide}) {
    ExpectBits(Take(BitVector::Extract(*value, IntegerView(63), Unsigned(0))),
               64, kWord);
    ExpectBits(Take(BitVector::Extract(*value, Unsigned(63), IntegerView(63))),
               1, 1);
    const std::array<std::uint64_t, 1> high = {55};
    const auto index = IntegerView::FromCanonicalMagnitude(high, false);
    ExpectBits(Take(BitVector::Extract(*value, index, IntegerView(48))), 8,
               0x92);
  }
  ExpectBits(Take(BitVector::Extract(wide, IntegerView(64), IntegerView(1))),
             64, 0xc0c951da62eb73fc);
  const auto sparse = Bits(UINT32_MAX, kWord);
  ExpectBits(Take(BitVector::Extract(sparse, IntegerView(64), IntegerView(1))),
             64, 0x40c951da62eb73fc);
  ExpectBits(Take(BitVector::Extract(sparse, Unsigned(UINT32_MAX - 1),
                                     Unsigned(UINT32_MAX - 64))),
             64, 0);
  ExpectError(BitVector::Extract(sparse, Unsigned(UINT32_MAX), IntegerView(0)),
              NumericError::kInvalidArgument);
}

TEST(UtilBitVector, SignedOverflowAndFullWordShiftBoundaries) {
  for (const auto width : {63u, 64u}) {
    SCOPED_TRACE(width);
    const auto sign = std::uint64_t{1} << (width - 1);
    const auto max = Bits(width, sign - 1);
    const auto min = Bits(width, sign);
    const auto one = Bits(width, 1);
    const auto all = Take(BitVector::AllOnes(Unsigned(width)));
    EXPECT_EQ(Take(BitVector::Add(max, one)), min);
    EXPECT_EQ(Take(BitVector::Sub(min, one)), max);
    EXPECT_TRUE(Take(BitVector::Saddo(max, one)));
    EXPECT_TRUE(Take(BitVector::Ssubo(min, one)));
    EXPECT_TRUE(Take(BitVector::Smulo(min, all)));
    EXPECT_TRUE(Take(BitVector::Sdivo(min, all)));
    EXPECT_TRUE(Take(BitVector::Ult(max, min)));
    EXPECT_TRUE(Take(BitVector::Sgt(max, min)));
    ExpectBits(Take(BitVector::Shl(one, Bits(width, width - 1))), width, sign);
    ExpectBits(Take(BitVector::Lshr(min, Bits(width, width - 1))), width, 1);
    EXPECT_EQ(Take(BitVector::Ashr(min, Bits(width, width - 1))), all);
    for (const auto shift : {width, width + 1}) {
      SCOPED_TRACE(shift);
      const auto count = Bits(width, shift);
      ExpectBits(Take(BitVector::Shl(min, count)), width, 0);
      ExpectBits(Take(BitVector::Lshr(min, count)), width, 0);
      EXPECT_EQ(Take(BitVector::Ashr(min, count)), all);
    }
  }
}

TEST(UtilBitVector, Bv65OverflowPredicatesAtSignedAndUnsignedLimits) {
  const auto signed_min =
      Take(BitVector::FromBinaryDigits("1" + std::string(64, '0')));
  const auto signed_max = Bits(65, UINT64_MAX);
  const auto unsigned_max = Take(BitVector::AllOnes(IntegerView(65)));
  const auto one = Bits(65, 1);
  const auto two = Bits(65, 2);

  EXPECT_TRUE(Take(BitVector::Uaddo(unsigned_max, one)));
  EXPECT_FALSE(Take(BitVector::Uaddo(signed_min, signed_max)));
  EXPECT_TRUE(Take(BitVector::Usubo(signed_max, signed_min)));
  EXPECT_FALSE(Take(BitVector::Usubo(signed_min, signed_max)));
  EXPECT_TRUE(Take(BitVector::Umulo(signed_min, two)));
  EXPECT_FALSE(Take(BitVector::Umulo(signed_min, one)));
  EXPECT_TRUE(Take(BitVector::Saddo(signed_max, one)));
  EXPECT_TRUE(Take(BitVector::Saddo(signed_min, unsigned_max)));
  EXPECT_FALSE(Take(BitVector::Saddo(signed_min, one)));
  EXPECT_TRUE(Take(BitVector::Ssubo(signed_min, one)));
  EXPECT_FALSE(Take(BitVector::Ssubo(signed_min, unsigned_max)));
  EXPECT_TRUE(Take(BitVector::Smulo(signed_min, unsigned_max)));
  EXPECT_FALSE(Take(BitVector::Smulo(signed_min, one)));
  EXPECT_TRUE(Take(BitVector::Sdivo(signed_min, unsigned_max)));
  EXPECT_FALSE(Take(BitVector::Sdivo(signed_min, one)));
  EXPECT_TRUE(BitVector::Nego(signed_min));
  EXPECT_FALSE(BitVector::Nego(unsigned_max));
}

TEST(UtilBitVector, UnsignedDivisionOfIdenticalWideOperands) {
  const auto value =
      Take(BitVector::FromBinaryDigits("1" + std::string(63, '0') + "1"));
  const auto copy = value;
  const auto zero = Bits(65, 0);

  ExpectBits(Take(BitVector::Udiv(value, value)), 65, 1);
  ExpectBits(Take(BitVector::Urem(value, value)), 65, 0);
  ExpectBits(Take(BitVector::Udiv(value, copy)), 65, 1);
  ExpectBits(Take(BitVector::Urem(value, copy)), 65, 0);
  EXPECT_EQ(Take(BitVector::Udiv(zero, zero)),
            Take(BitVector::AllOnes(IntegerView(65))));
  EXPECT_EQ(Take(BitVector::Urem(zero, zero)), zero);
}

TEST(UtilBitVector, DocumentedByteBoundariesAndHugeRotationIndices) {
  const auto ff = Bits(8, 0xff);
  const auto min = Bits(8, 0x80);
  const auto zero = Bits(8, 0);
  const auto one = Bits(8, 1);
  const auto two = Bits(8, 2);
  ExpectBits(Take(BitVector::Add(ff, one)), 8, 0);
  ExpectBits(Take(BitVector::Mul(min, two)), 8, 0);
  ExpectBits(Take(BitVector::Neg(min)), 8, 0x80);
  EXPECT_TRUE(Take(BitVector::Uaddo(ff, one)));
  EXPECT_TRUE(Take(BitVector::Umulo(min, two)));
  EXPECT_TRUE(BitVector::Nego(min));
  const auto positive = Bits(8, 0x35);
  ExpectBits(Take(BitVector::Udiv(positive, zero)), 8, 0xff);
  EXPECT_EQ(Take(BitVector::Urem(positive, zero)), positive);
  EXPECT_EQ(Take(BitVector::Sdiv(min, zero)), one);
  EXPECT_EQ(Take(BitVector::Sdiv(Bits(8, 0x7f), zero)), ff);
  EXPECT_EQ(Take(BitVector::Srem(min, zero)), min);
  EXPECT_EQ(Take(BitVector::Smod(min, zero)), min);
  ExpectBits(Take(BitVector::Srem(Bits(8, 251), Bits(8, 3))), 8, 254);
  ExpectBits(Take(BitVector::Smod(Bits(8, 251), Bits(8, 3))), 8, 1);
  ExpectBits(Take(BitVector::Smod(Bits(8, 5), Bits(8, 253))), 8, 255);
  EXPECT_EQ(Take(BitVector::Lshr(min, Bits(8, 8))), zero);
  EXPECT_EQ(Take(BitVector::Ashr(min, Bits(8, 8))), ff);
  const auto word = Bits(8, 0x81);
  const auto huge = Int("18446744073709551617");
  const auto multiple = Int("18446744073709551616");
  ExpectBits(Take(BitVector::RotateLeft(word, huge.Borrow())), 8, 0x03);
  ExpectBits(Take(BitVector::RotateRight(word, huge.Borrow())), 8, 0xc0);
  EXPECT_EQ(Take(BitVector::RotateLeft(word, multiple.Borrow())), word);
  EXPECT_EQ(Take(BitVector::RotateRight(word, multiple.Borrow())), word);
}

TEST(UtilBitVector, RotatesWithBorrowedMultiLimbIndex) {
  constexpr std::array<std::uint64_t, 2> limbs{1, 1};
  const auto count = IntegerView::FromCanonicalMagnitude(limbs, false);
  // (2^64 + 1) mod 7 = 3.
  const auto word = Bits(7, 0x41);
  ExpectBits(Take(BitVector::RotateLeft(word, count)), 7, 0x0c);
  ExpectBits(Take(BitVector::RotateRight(word, count)), 7, 0x18);

  // 274177 divides 2^64 + 1.
  const auto sparse = Bits(274177, 0x41);
  EXPECT_EQ(Take(BitVector::RotateLeft(sparse, count)), sparse);
  EXPECT_EQ(Take(BitVector::RotateRight(sparse, count)), sparse);
}

TEST(UtilBitVector, SparseMaximumWidthAndOwningCopies) {
  for (const auto value : {0u, 1u}) {
    const auto sparse = Bits(UINT32_MAX, value);
    auto copy = sparse;
    EXPECT_EQ(copy, sparse);
    EXPECT_EQ(copy.Hash(), sparse.Hash());
    EXPECT_EQ(copy.BackingBytes(), 0u);
    ExpectBits(copy, UINT32_MAX, value);
    EXPECT_EQ(copy.EffectiveBitLength(), value);
    EXPECT_FALSE(copy.TestBit(UINT32_MAX - 1));
    EXPECT_EQ(copy.Chunk(UINT32_MAX / 64), 0u);
    EXPECT_EQ(Take(BitVector::Add(sparse, sparse)).TryUint64(), 2 * value);
    EXPECT_EQ(Take(BitVector::Mul(sparse, sparse)).TryUint64(), value);
    EXPECT_EQ(Int64(BitVector::ToSignedInteger(sparse)), value);
    EXPECT_EQ(Take(BitVector::RotateLeft(sparse, IntegerView(1))).TryUint64(),
              2 * value);
    EXPECT_EQ(Take(BitVector::SignExtend(sparse, IntegerView(0))), sparse);
    EXPECT_EQ(Take(BitVector::Repeat(sparse, IntegerView(1))), sparse);
    ExpectError(BitVector::Concat(sparse, Bits(1, 0)),
                NumericError::kSizeLimit);
    ExpectError(BitVector::Concat(Bits(1, 0), sparse),
                NumericError::kSizeLimit);
    ExpectError(BitVector::ZeroExtend(sparse, IntegerView(1)),
                NumericError::kSizeLimit);
    ExpectError(BitVector::SignExtend(sparse, IntegerView(1)),
                NumericError::kSizeLimit);
    ExpectError(BitVector::Repeat(sparse, IntegerView(2)),
                NumericError::kSizeLimit);
    CheckSelfAssignment(copy);
    EXPECT_EQ(copy, sparse);
    auto moved = std::move(copy);
    EXPECT_EQ(moved, sparse);
    ExpectBits(copy, 1, 0);
  }
  const auto wide = Take(BitVector::FromHexDigits(std::string(64, 'f')));
  auto copy = wide;
  CheckSelfAssignment(copy);
  auto moved = std::move(copy);
  ExpectBits(copy, 1, 0);
  auto assigned = wide;
  assigned = std::move(moved);
  ExpectBits(moved, 1, 0);
  EXPECT_EQ(assigned, wide);
  EXPECT_EQ(assigned.Hash(), wide.Hash());
  EXPECT_GT(assigned.BackingBytes(), 0u);
  EXPECT_EQ(Take(assigned.ToBinary()), std::string(256, '1'));
}

// Binary long division and shift-and-add multiplication on a fixed bit array
// keep the large reference independent of util's word, limb, and GMP paths.
using RefBits = std::bitset<4096>;

RefBits AddRef(RefBits a, const RefBits& b, std::uint32_t width, bool carry) {
  for (std::uint32_t i = 0; i < width; ++i) {
    const bool ai = a[i];
    const bool bi = b[i];
    a[i] = (ai != bi) != carry;
    carry = (ai && bi) || (carry && (ai || bi));
  }
  return a;
}

RefBits SubRef(const RefBits& a, const RefBits& b, std::uint32_t width) {
  return AddRef(a, ~b, width, true);
}

RefBits WordMask(std::uint32_t width) {
  RefBits mask;
  for (std::uint32_t i = 0; i < width; ++i) mask.set(i);
  return mask;
}

bool LessRef(const RefBits& a, const RefBits& b, std::uint32_t width) {
  for (std::uint32_t i = width; i-- != 0;) {
    if (a[i] != b[i]) return b[i];
  }
  return false;
}

std::pair<RefBits, RefBits> DivRef(const RefBits& a, const RefBits& b,
                                   std::uint32_t width, const RefBits& mask) {
  if (b.none()) return {mask, a};
  RefBits quotient;
  RefBits remainder;
  for (std::uint32_t i = width; i-- != 0;) {
    remainder <<= 1;
    remainder[0] = a[i];
    if (!LessRef(remainder, b, width)) {
      quotient.set(i);
      remainder = SubRef(remainder, b, width);
    }
  }
  return {quotient, remainder};
}

std::string BinaryText(const RefBits& value, std::uint32_t width) {
  return value.to_string().substr(value.size() - width);
}

TEST(UtilBitVector, SeededWideArithmeticAgainstIndependentBitOracle) {
  constexpr std::uint64_t kSeed = 0xdad311a;
  for (const bool portable : {false, true}) {
    SCOPED_TRACE(portable);
    struct ResetBackend {
      ~ResetBackend() { NumericTestPeer::ForcePortableBitVector(false); }
    };
    const ResetBackend reset;
    NumericTestPeer::ForcePortableBitVector(portable);
    std::mt19937_64 random(kSeed);
    for (const auto width : {8u, 32u, 63u, 64u, 65u, 128u, 256u, 4096u}) {
      SCOPED_TRACE(width);
      const auto mask = WordMask(width);
      const auto from_ref = [width](const RefBits& bits) {
        return Take(BitVector::FromBinaryDigits(BinaryText(bits, width)));
      };
      RefBits a;
      RefBits b;
      for (std::uint32_t i = 0; i < width; ++i) {
        a[i] = (random() & 1u) != 0;
        b[i] = (random() & 1u) != 0;
      }
      a.set(width - 1);
      b.reset(width - 1);
      b.set(width - 2);
      const auto check = [&](const RefBits& left, const RefBits& right) {
        SCOPED_TRACE(::testing::Message()
                     << "seed " << kSeed << " a=" << BinaryText(left, width)
                     << " b=" << BinaryText(right, width));
        const auto x = from_ref(left);
        const auto y = from_ref(right);
        const auto [q, r] = DivRef(left, right, width, mask);
        RefBits product;
        auto shifted = left;
        for (std::uint32_t i = 0; i < width; ++i) {
          if (right[i]) product = AddRef(product, shifted, width, false);
          shifted <<= 1;
        }
        EXPECT_EQ(Take(BitVector::Add(x, y)),
                  from_ref(AddRef(left, right, width, false)));
        EXPECT_EQ(Take(BitVector::Sub(x, y)),
                  from_ref(SubRef(left, right, width)));
        EXPECT_EQ(Take(BitVector::Mul(x, y)), from_ref(product));
        EXPECT_EQ(Take(BitVector::Udiv(x, y)), from_ref(q));
        EXPECT_EQ(Take(BitVector::Urem(x, y)), from_ref(r));
        EXPECT_EQ(Take(BitVector::Ult(x, y)), LessRef(left, right, width));
        const bool neg_x = left[width - 1];
        const bool neg_y = right[width - 1];
        EXPECT_EQ(Take(BitVector::Slt(x, y)),
                  neg_x != neg_y ? neg_x : LessRef(left, right, width));
        const RefBits zero;
        const auto magnitude_x = neg_x ? SubRef(zero, left, width) : left;
        const auto magnitude_y = neg_y ? SubRef(zero, right, width) : right;
        const auto [mag_q, mag_r] =
            DivRef(magnitude_x, magnitude_y, width, mask);
        const auto signed_q =
            (neg_x != neg_y) ? SubRef(zero, mag_q, width) : mag_q;
        const auto signed_r = neg_x ? SubRef(zero, mag_r, width) : mag_r;
        const auto signed_mod = neg_x != neg_y && mag_r.any() && right.any()
                                    ? AddRef(signed_r, right, width, false)
                                    : signed_r;
        EXPECT_EQ(Take(BitVector::Sdiv(x, y)), from_ref(signed_q));
        EXPECT_EQ(Take(BitVector::Srem(x, y)), from_ref(signed_r));
        EXPECT_EQ(Take(BitVector::Smod(x, y)), from_ref(signed_mod));
      };
      check(a, b);
      check(b, a);
      check(a, a);
      if (width != 4096) {  // The larger limb-by-limb cases already run above.
        check(a, RefBits(3));
        check(a, RefBits(0));
      }
    }
  }
}

}  // namespace
}  // namespace dandelion::testing::util
