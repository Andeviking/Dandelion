#include "test_support.h"

namespace dandelion::testing {
namespace {

class Value : public NodeTest {
 protected:
  Node BvLiteral(SortView sort, IntegerView value) {
    const auto exact = ExactBv(*sort.GetWidth(), value);
    return Take(manager_.MakeBvValue(sort, exact.Borrow()));
  }

  Node FpLiteral(SortView sort, IntegerView bits) {
    const auto exact = FpBits(*sort.GetFpFormat(), bits);
    return Take(manager_.MakeFpValue(sort, exact.Borrow()));
  }

  Node FpLiteral(SortView sort, FpSpecial special) {
    const auto exact = SpecialFp(*sort.GetFpFormat(), special);
    return Take(manager_.MakeFpValue(sort, exact.Borrow()));
  }
};

TEST_F(Value, UnsignedWordAndWideBitVectorValuesRemainExact) {
  NodeTestPeer::ForceCollisions(manager_, 2);
  const auto bv64 = Bv(64);
  const auto bv65 = Bv(65);
  const auto word = IntegerView::FromUnsigned(UINT64_MAX);
  const auto wide = Number("18446744073709551616");
  const auto a = BvLiteral(bv64.Borrow(), word);
  const auto b = BvLiteral(bv65.Borrow(), word);
  const auto c = BvLiteral(bv65.Borrow(), wide.Borrow());
  EXPECT_NE(a, b);
  EXPECT_NE(b, c);
  const auto value = *b.Borrow().GetValue()->GetBitVector();
  EXPECT_EQ(value.Width(), 65);
  EXPECT_EQ(value.EffectiveBitLength(), 64);
  EXPECT_EQ(value.TryUint64(), UINT64_MAX);
  const auto owned = ExactBv(65, word);
  EXPECT_EQ(value.Hash(), owned.Borrow().Hash());
  const auto copied = value.Copy();
  EXPECT_EQ(copied.Borrow(), owned.Borrow());
  EXPECT_EQ(Take(manager_.MakeBvValue(bv65.Borrow(), value)), b);
  const auto wide_value = *c.Borrow().GetValue()->GetBitVector();
  EXPECT_EQ(wide_value.Width(), 65);
  EXPECT_EQ(wide_value.EffectiveBitLength(), 65);
  EXPECT_EQ(wide_value.Chunk(0), 0);
  EXPECT_EQ(wide_value.Chunk(1), 1);
  EXPECT_FALSE(wide_value.TryUint64());

  const auto fp = Fp(11, 53);
  constexpr std::uint64_t kNegativeOne = 0xbff0000000000000;
  const auto finite =
      FpLiteral(fp.Borrow(), IntegerView::FromUnsigned(kNegativeOne));
  const auto floating = *finite.Borrow().GetValue()->GetFloatingPoint();
  EXPECT_EQ(floating.Format(), Format(11, 53));
  EXPECT_EQ(floating.Category(), FpCategory::kFiniteBits);
  ASSERT_TRUE(floating.FiniteBits());
  EXPECT_EQ(floating.FiniteBits()->TryUint64(), kNegativeOne);
}

TEST_F(Value, EveryLiteralKindHasOnlyItsOwnTypedView) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto check = [](const Node& node, Kind kind, SortKind sort) {
    const auto view = node.Borrow();
    EXPECT_EQ(view.GetKind(), kind);
    EXPECT_EQ(view.GetSort().GetKind(), sort);
    ASSERT_TRUE(view.GetValue());
    EXPECT_EQ(view.GetValue()->GetInteger().has_value(),
              kind == Kind::kIntValue);
    EXPECT_EQ(view.GetValue()->GetBitVector().has_value(),
              kind == Kind::kBvValue);
    EXPECT_EQ(view.GetValue()->GetFloatingPoint().has_value(),
              kind == Kind::kFpValue);
    EXPECT_TRUE(view.GetChildren().empty());
    EXPECT_TRUE(view.GetIndices().empty());
    EXPECT_FALSE(view.GetRationalParameter());
  };
  const auto t = Take(manager_.MakeBool(true));
  const auto f = Take(manager_.MakeBool(false));
  EXPECT_NE(t, f);
  check(t, Kind::kBoolValue, SortKind::kBool);
  EXPECT_EQ(t.Borrow().GetValue()->GetBool(), true);

  for (const auto text :
       {"-9223372036854775809", "-9223372036854775808", "9223372036854775807",
        "9223372036854775808", "340282366920938463463374607431768211456"}) {
    auto external = Number(text);
    const auto node = Take(manager_.MakeInt(external.Borrow()));
    const auto identity_hash = NodeHash{}(node);
    external = Integer(0);
    check(node, Kind::kIntValue, SortKind::kInt);
    EXPECT_EQ(node.Borrow().GetValue()->GetInteger()->ToDecimal(), text);
    const auto restored = Number(text);
    EXPECT_EQ(node, Take(manager_.MakeInt(restored.Borrow())));
    EXPECT_EQ(NodeHash{}(node), identity_hash);
  }
  const auto twice = Number("200000000000000000000000000000000000000");
  const auto unit = Number("100000000000000000000000000000000000000");
  const auto reduced = Take(Rational::Make(twice.Borrow(), unit.Borrow()));
  EXPECT_TRUE(reduced.Borrow().Numerator().IsSmall());
  EXPECT_EQ(Take(manager_.MakeInt(reduced.Borrow().Numerator())), Int(2));

  const auto bv65 = Bv(65);
  const auto big = Number("18446744073709551616");
  const auto exact = BvLiteral(bv65.Borrow(), big.Borrow());
  check(exact, Kind::kBvValue, SortKind::kBitVector);
  EXPECT_EQ(exact.GetSort(), bv65);
  EXPECT_EQ(exact.Borrow().GetValue()->GetBitVector()->Chunk(1), 1);
  const auto bv8 = Bv(8);
  EXPECT_NE(Int(0), BvLiteral(bv8.Borrow(), IntegerView(0)));

  for (const auto mode :
       {RoundingMode::kRne, RoundingMode::kRna, RoundingMode::kRtp,
        RoundingMode::kRtn, RoundingMode::kRtz}) {
    const auto node = Take(manager_.MakeRoundingMode(mode));
    check(node, Kind::kRoundingModeValue, SortKind::kRoundingMode);
    EXPECT_EQ(node.Borrow().GetValue()->GetRoundingMode(), mode);
    EXPECT_EQ(node, Take(manager_.MakeRoundingMode(mode)));
  }
  Error(manager_.MakeRoundingMode(static_cast<RoundingMode>(5)),
        ErrorCode::kInvalidLiteral);

  const auto fp = Fp(2, 64);
  auto input = FpBits(Format(2, 64), big.Borrow());
  const auto floating = Take(manager_.MakeFpValue(fp.Borrow(), input.Borrow()));
  input = SpecialFp(Format(2, 64), FpSpecial::kNaN);
  check(floating, Kind::kFpValue, SortKind::kFloatingPoint);
  const auto value = *floating.Borrow().GetValue()->GetFloatingPoint();
  EXPECT_EQ(value.Category(), FpCategory::kFiniteBits);
  ASSERT_TRUE(value.FiniteBits());
  EXPECT_EQ(value.FiniteBits()->Chunk(0), 0);
  EXPECT_EQ(value.FiniteBits()->Chunk(1), 1);
}

TEST_F(Value, EveryTinyFpPatternHasTheHandDerivedCategory) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto sort = Fp(2, 3);
  const auto nan = FpLiteral(sort.Borrow(), FpSpecial::kNaN);
  std::vector<Node> live;
  for (int bits = 0; bits < 32; ++bits) {
    const auto node = FpLiteral(sort.Borrow(), IntegerView(bits));
    live.push_back(node);
    EXPECT_EQ(node.Borrow().GetKind(), Kind::kFpValue);
    EXPECT_EQ(node.GetSort(), sort);
    EXPECT_TRUE(node.Borrow().GetChildren().empty());
    const bool sign = bits >= 16;
    const int magnitude = bits % 16;
    FpCategory expected = FpCategory::kFiniteBits;
    if (magnitude > 12) expected = FpCategory::kNaN;
    if (magnitude == 12)
      expected =
          sign ? FpCategory::kNegativeInfinity : FpCategory::kPositiveInfinity;
    if (magnitude == 0)
      expected = sign ? FpCategory::kNegativeZero : FpCategory::kPositiveZero;
    const auto value = *node.Borrow().GetValue()->GetFloatingPoint();
    EXPECT_EQ(value.Format(), Format(2, 3));
    EXPECT_EQ(value.Category(), expected) << bits;
    if (magnitude > 12) {
      EXPECT_EQ(node, nan);
    }
    if (expected == FpCategory::kFiniteBits) {
      ASSERT_TRUE(value.FiniteBits());
      EXPECT_EQ(value.FiniteBits()->TryUint64(), static_cast<unsigned>(bits));
    } else {
      EXPECT_FALSE(value.FiniteBits());
    }
  }
  for (const auto [special, bits] :
       std::array{std::pair{FpSpecial::kNaN, 13},
                  std::pair{FpSpecial::kPositiveInfinity, 12},
                  std::pair{FpSpecial::kNegativeInfinity, 28},
                  std::pair{FpSpecial::kPositiveZero, 0},
                  std::pair{FpSpecial::kNegativeZero, 16}}) {
    EXPECT_EQ(FpLiteral(sort.Borrow(), special),
              live[static_cast<std::size_t>(bits)]);
  }
  EXPECT_NE(live[0], live[16]);
  EXPECT_NE(Op(Kind::kEqual, {live[0].Borrow(), live[16].Borrow()}),
            Op(Kind::kFpEq, {live[0].Borrow(), live[16].Borrow()}));

  const auto format = Format(2, 3);
  Error(FloatingPointValue::FromBits(format, IntegerView(32)),
        NumericError::kValueOutOfRange);
  Error(FloatingPointValue::FromBits(format, IntegerView(-1)),
        NumericError::kValueOutOfRange);
  Error(FloatingPointValue::Special(format, FpCategory::kFiniteBits),
        NumericError::kInvalidArgument);
  const auto huge = Fp(UINT32_MAX - 2, 2);
  EXPECT_EQ(FpLiteral(huge.Borrow(), IntegerView(0)),
            FpLiteral(huge.Borrow(), FpSpecial::kPositiveZero));
}

TEST_F(Value, Binary32SpecialsAndSubnormalUseCanonicalTypedKeys) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto sort = Fp(8, 24);
  const auto nan = FpLiteral(sort.Borrow(), FpSpecial::kNaN);
  EXPECT_EQ(nan, FpLiteral(sort.Borrow(), IntegerView(0x7fc00000)));
  EXPECT_EQ(nan, FpLiteral(sort.Borrow(), IntegerView(0xff800001)));
  EXPECT_EQ(nan.Borrow().GetValue()->GetFloatingPoint()->Category(),
            FpCategory::kNaN);
  std::vector<Node> live{nan};
  for (const auto& [special, category, bits] :
       std::array{std::tuple{FpSpecial::kPositiveZero,
                             FpCategory::kPositiveZero, 0x00000000U},
                  std::tuple{FpSpecial::kNegativeZero,
                             FpCategory::kNegativeZero, 0x80000000U},
                  std::tuple{FpSpecial::kPositiveInfinity,
                             FpCategory::kPositiveInfinity, 0x7f800000U},
                  std::tuple{FpSpecial::kNegativeInfinity,
                             FpCategory::kNegativeInfinity, 0xff800000U}}) {
    const auto node = FpLiteral(sort.Borrow(), IntegerView(bits));
    EXPECT_EQ(node, FpLiteral(sort.Borrow(), special));
    const auto value = *node.Borrow().GetValue()->GetFloatingPoint();
    EXPECT_EQ(value.Format(), Format(8, 24));
    EXPECT_EQ(value.Category(), category);
    EXPECT_FALSE(value.FiniteBits());
    for (const auto& other : live) EXPECT_NE(node, other);
    live.push_back(node);
  }
  const auto subnormal = FpLiteral(sort.Borrow(), IntegerView(1));
  const auto value = *subnormal.Borrow().GetValue()->GetFloatingPoint();
  EXPECT_EQ(value.Format(), Format(8, 24));
  EXPECT_EQ(value.Category(), FpCategory::kFiniteBits);
  ASSERT_TRUE(value.FiniteBits());
  EXPECT_EQ(value.FiniteBits()->Width(), 32);
  EXPECT_EQ(value.FiniteBits()->TryUint64(), 1);
}

TEST_F(Value, TypedFactoriesRejectIncompatibleSortsWithoutPublishing) {
  const auto bv = Bv(8);
  const auto bool_sort = Take(manager_.BoolSort());
  const auto fp = Fp(8, 24);
  const auto same_bits_fp = Fp(9, 23);
  auto other = Take(NodeManager::Create());
  const auto foreign_bv = Take(other.MakeBvSort(8));
  const auto foreign_fp =
      Take(other.MakeFpSort(IntegerView(8), IntegerView(24)));
  const auto zero = ExactBv(8, IntegerView(0));
  const auto big = Number("18446744073709551616");
  Error(BitVector::FromIntegerExact(IntegerView(64), big.Borrow()),
        NumericError::kValueOutOfRange);
  Error(BitVector::FromIntegerExact(IntegerView(8), IntegerView(256)),
        NumericError::kValueOutOfRange);
  Error(BitVector::FromIntegerExact(IntegerView(8), IntegerView(-1)),
        NumericError::kValueOutOfRange);
  const std::array floats{FpBits(Format(8, 24), IntegerView(1)),
                          SpecialFp(Format(8, 24), FpSpecial::kNaN)};
  const auto before = NodeTestPeer::GetStats(manager_);
  const auto storage = Take(manager_.GetStorageStats());
  Error(manager_.MakeBvValue(bool_sort.Borrow(), zero.Borrow()),
        ErrorCode::kSortMismatch);
  Error(manager_.MakeBvValue({}, zero.Borrow()), ErrorCode::kNullObject);
  Error(manager_.MakeBvValue(foreign_bv.Borrow(), zero.Borrow()),
        ErrorCode::kForeignManager);
  for (const auto& value : floats) {
    Error(manager_.MakeFpValue(same_bits_fp.Borrow(), value.Borrow()),
          ErrorCode::kSortMismatch);
    Error(manager_.MakeFpValue(bool_sort.Borrow(), value.Borrow()),
          ErrorCode::kSortMismatch);
    Error(manager_.MakeFpValue({}, value.Borrow()), ErrorCode::kNullObject);
    Error(manager_.MakeFpValue(foreign_fp.Borrow(), value.Borrow()),
          ErrorCode::kForeignManager);
  }
  const auto after = NodeTestPeer::GetStats(manager_);
  EXPECT_EQ(after.next_node_id, before.next_node_id);
  EXPECT_EQ(after.live_nodes, before.live_nodes);
  EXPECT_EQ(Take(manager_.GetStorageStats()).node_bytes, storage.node_bytes);
  EXPECT_EQ(Take(manager_.MakeBvValue(bv.Borrow(), zero.Borrow())).GetId(),
            before.next_node_id);
  EXPECT_EQ(Take(manager_.MakeFpValue(fp.Borrow(), floats[0].Borrow())).GetId(),
            before.next_node_id + 1);
}

TEST_F(Value, LiveWidePayloadsAndRoundingModesHaveDistinctKeysAndBytes) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto bv = Bv(65);
  const auto fp = Fp(2, 64);
  const auto first = Number("18446744073709551617");
  const auto second = Number("18446744073709551618");
  std::array nodes{BvLiteral(bv.Borrow(), first.Borrow()),
                   BvLiteral(bv.Borrow(), second.Borrow()),
                   FpLiteral(fp.Borrow(), first.Borrow()),
                   FpLiteral(fp.Borrow(), second.Borrow())};
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const auto view = *nodes[i].Borrow().GetValue();
    const auto bits =
        i < 2 ? *view.GetBitVector() : *view.GetFloatingPoint()->FiniteBits();
    EXPECT_EQ(bits.Chunk(0), 1 + i % 2);
    EXPECT_EQ(bits.Chunk(1), 1);
    for (std::size_t j = 0; j < i; ++j) EXPECT_NE(nodes[i], nodes[j]);
    const auto exact = i % 2 == 0 ? first.Borrow() : second.Borrow();
    EXPECT_EQ(nodes[i], i < 2 ? BvLiteral(bv.Borrow(), exact)
                              : FpLiteral(fp.Borrow(), exact));
  }
  const auto storage = Take(manager_.GetStorageStats());
  EXPECT_EQ(storage.node_bytes, 4 * 72);
  EXPECT_EQ(storage.numeric_backing_bytes, 0);
  Take(manager_.ReleaseRoots(nodes));
  EXPECT_EQ(Take(manager_.GetStorageStats()).node_bytes, 0);
  EXPECT_EQ(Take(manager_.GetStorageStats()).numeric_backing_bytes, 0);

  constexpr std::array modes{RoundingMode::kRne, RoundingMode::kRna,
                             RoundingMode::kRtp, RoundingMode::kRtn,
                             RoundingMode::kRtz};
  std::vector<Node> live;
  for (const auto mode : modes)
    live.push_back(Take(manager_.MakeRoundingMode(mode)));
  for (std::size_t i = 0; i < live.size(); ++i) {
    EXPECT_EQ(Take(manager_.MakeRoundingMode(modes[i])), live[i]);
    for (std::size_t j = 0; j < i; ++j) EXPECT_NE(live[i], live[j]);
  }
}

TEST_F(Value, MultiChunkFpClassificationUsesAllExponentAndFractionBits) {
  const auto fp = Fp(65, 66);
  // Sign is bit 130, the exponent occupies bits 65..129, fraction 0..64.
  const auto bits = [&](std::string_view hex) {
    const auto value = Take(Integer::ParseRadix(hex, 16));
    return FpLiteral(fp.Borrow(), value.Borrow());
  };
  const auto negative_zero = bits("400000000000000000000000000000000");
  const auto positive_inf = bits("3fffffffffffffffe0000000000000000");
  const auto negative_inf = bits("7fffffffffffffffe0000000000000000");
  const auto nan = bits("3ffffffffffffffff0000000000000000");
  const auto max_finite = bits("3fffffffffffffffdffffffffffffffff");
  const auto view = [](const Node& node) {
    return *node.Borrow().GetValue()->GetFloatingPoint();
  };
  EXPECT_EQ(view(negative_zero).Category(), FpCategory::kNegativeZero);
  EXPECT_EQ(view(positive_inf).Category(), FpCategory::kPositiveInfinity);
  EXPECT_EQ(view(negative_inf).Category(), FpCategory::kNegativeInfinity);
  EXPECT_EQ(nan, FpLiteral(fp.Borrow(), FpSpecial::kNaN));
  EXPECT_EQ(view(max_finite).Category(), FpCategory::kFiniteBits);
  const auto finite_bits = *view(max_finite).FiniteBits();
  EXPECT_EQ(finite_bits.Width(), 131);
  EXPECT_EQ(finite_bits.Chunk(0), UINT64_MAX);
  EXPECT_EQ(finite_bits.Chunk(1), UINT64_MAX - 2);
  EXPECT_EQ(finite_bits.Chunk(2), 3);
}

}  // namespace
}  // namespace dandelion::testing
