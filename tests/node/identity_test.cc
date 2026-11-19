#include "test_support.h"

namespace dandelion::testing {
namespace {

class Identity : public NodeTest {};

TEST_F(Identity, AllApplicableKeyFieldsSurviveForcedCollision) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto z = Take(manager_.IntSort());
  const auto b = Take(manager_.BoolSort());
  const auto x = Int(1);
  const auto y = Int(2);
  EXPECT_NE(x, y);
  EXPECT_NE(Op(Kind::kIntAdd, {x.Borrow(), y.Borrow()}),
            Op(Kind::kIntSub, {x.Borrow(), y.Borrow()}));
  EXPECT_NE(Op(Kind::kIntAdd, {x.Borrow(), y.Borrow()}),
            Op(Kind::kIntAdd, {y.Borrow(), x.Borrow()}));
  EXPECT_NE(Op(Kind::kIntAdd, {x.Borrow(), y.Borrow()}),
            Op(Kind::kIntAdd, {x.Borrow(), y.Borrow(), y.Borrow()}));
  const auto a = Take(manager_.MakeArraySort(z.Borrow(), z.Borrow()));
  const auto c = Take(manager_.MakeArraySort(b.Borrow(), z.Borrow()));
  EXPECT_NE(Op(Kind::kArrayConst, {x.Borrow()}, {}, a.Borrow()),
            Op(Kind::kArrayConst, {x.Borrow()}, {}, c.Borrow()));
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const Integer one(1), two(2), three(3), four(4), six(6), eight(8),
      twenty_four(24);
  const auto q1 = Take(Rational::Make(two.Borrow(), three.Borrow()));
  const auto q2 = Take(Rational::Make(four.Borrow(), six.Borrow()));
  const auto q3 = Take(Rational::Make(one.Borrow(), three.Borrow()));
  const std::array fmt = {eight, twenty_four};
  const auto first =
      Op(Kind::kFpFromRational, {rm.Borrow()}, fmt, {}, q1.Borrow());
  EXPECT_EQ(first,
            Op(Kind::kFpFromRational, {rm.Borrow()}, fmt, {}, q2.Borrow()));
  EXPECT_NE(first,
            Op(Kind::kFpFromRational, {rm.Borrow()}, fmt, {}, q3.Borrow()));
  const auto fp = Fp(8, 24);
  const auto other_format = Fp(2, 2);
  EXPECT_NE(
      Take(manager_.MakeFpSpecial(fp.Borrow(), FpSpecial::kNaN)),
      Take(manager_.MakeFpSpecial(other_format.Borrow(), FpSpecial::kNaN)));
  const std::array domain = {z.Borrow()};
  const auto d1 = Take(manager_.DeclareFunction("f", domain, z.Borrow()));
  const auto d2 = Take(manager_.DeclareFunction("f", domain, z.Borrow()));
  const auto i1 = Take(manager_.InstantiateDeclaration(d1.Borrow(), {}));
  const auto i2 = Take(manager_.InstantiateDeclaration(d2.Borrow(), {}));
  const std::array args = {x.Borrow()};
  EXPECT_NE(Take(manager_.MakeApply(i1.Borrow(), args)),
            Take(manager_.MakeApply(i2.Borrow(), args)));
  const std::array pair_domain = {z.Borrow(), z.Borrow()};
  const auto ordered_decl =
      Take(manager_.DeclareFunction("ordered", pair_domain, z.Borrow()));
  const auto ordered_instance =
      Take(manager_.InstantiateDeclaration(ordered_decl.Borrow(), {}));
  const std::array forward = {x.Borrow(), y.Borrow()};
  const std::array reverse = {y.Borrow(), x.Borrow()};
  EXPECT_NE(Take(manager_.MakeApply(ordered_instance.Borrow(), forward)),
            Take(manager_.MakeApply(ordered_instance.Borrow(), reverse)));
}

TEST_F(Identity, NullCrossStateAndReleasedStructureIdentity) {
  Node empty;
  Node other_empty;
  EXPECT_EQ(empty, other_empty);
  EXPECT_EQ(empty.GetId(), 0);
  EXPECT_EQ(NodeHash{}(empty), NodeHash{}(other_empty));
  auto other = Take(NodeManager::Create());
  const auto a = Take(manager_.MakeBool(true));
  const auto b = Take(other.MakeBool(true));
  EXPECT_NE(a, b);
  const auto sort = Take(manager_.BoolSort());
  NodeId old = 0;
  {
    const auto node = Take(manager_.MakeBool(false));
    old = node.GetId();
  }
  const auto reconstructed = Take(manager_.MakeBool(false));
  EXPECT_GT(reconstructed.GetId(), old);
  EXPECT_EQ(reconstructed.GetSort(), sort);
}

}  // namespace

TEST_F(Identity, RationalKeyKeepsEachChangedComponentLive) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const std::array format{Integer(8), Integer(24)};
  const auto make = [&](std::int64_t n, std::int64_t d) {
    const auto value = Take(Rational::Make(IntegerView(n), IntegerView(d)));
    return Op(Kind::kFpFromRational, {rm.Borrow()}, format, {}, value.Borrow());
  };
  const auto two_thirds = make(2, 3);
  const auto two_fifths = make(2, 5);
  const auto one_third = make(1, 3);
  EXPECT_NE(two_thirds, two_fifths);
  EXPECT_NE(two_thirds, one_third);
  EXPECT_EQ(two_thirds, make(4, 6));
  EXPECT_EQ(two_fifths, make(6, 15));
  EXPECT_EQ(one_third, make(2, 6));
}

}  // namespace dandelion::testing
