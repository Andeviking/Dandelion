#include "test_support.h"

namespace dandelion::testing {
namespace {

TEST_F(NodeTest, EmptyDomainsStayCompactAndOnlyPublishNonemptySpans) {
  const auto sort = Take(manager_.IntSort());
  const auto empty_bytes = Take(manager_.GetStorageStats()).metadata_bytes;
  constexpr std::size_t count = 2048;
  for (std::size_t i = 0; i < count; ++i) {
    const auto empty = Take(manager_.DeclareFunction("", {}, sort.Borrow()));
    EXPECT_TRUE(empty.Borrow().GetDomain().empty());
  }
  const auto filled_bytes = Take(manager_.GetStorageStats()).metadata_bytes;
  // A name span, a datatype pointer, three 64-bit IDs, and a kind require
  // seven aligned words. Empty domains must not allocate a separate span.
  constexpr std::size_t kRecordBytes = 56;
  EXPECT_LE(filled_bytes - empty_bytes, kRecordBytes * count);
  EXPECT_EQ(NodeTestPeer::NonemptyDeclarationDomains(manager_), 0);

  const auto b = Take(manager_.BoolSort());
  const std::array first_domain{sort.Borrow(), b.Borrow()};
  const auto first =
      Take(manager_.DeclareFunction("first", first_domain, sort.Borrow()));
  const auto instance =
      Take(manager_.InstantiateDeclaration(first.Borrow(), {}));
  const auto borrowed = instance.Borrow().GetDomain();
  const std::array next_domain{b.Borrow(), sort.Borrow(), b.Borrow()};
  const auto used = NodeTestPeer::DeclarationPayloadUsed(manager_);
  const auto before = NodeTestPeer::GetStats(manager_);
  EXPECT_EQ(NodeTestPeer::NonemptyDeclarationDomains(manager_), 1);
  bool published = false;
  std::size_t failures = 0;
  for (std::size_t checkpoint = 0; checkpoint < 16; ++checkpoint) {
    NodeTestPeer::FailAllocationAfter(manager_, checkpoint);
    const auto next = manager_.DeclareFunction("next", next_domain, b.Borrow());
    NodeTestPeer::ClearFailures(manager_);
    ASSERT_EQ(borrowed.size(), 2);
    EXPECT_EQ(borrowed[0], sort.Borrow());
    EXPECT_EQ(borrowed[1], b.Borrow());
    if (next) {
      EXPECT_EQ(next->GetId(), before.declarations + 1);
      ASSERT_EQ(next->Borrow().GetDomain().size(), next_domain.size());
      EXPECT_EQ(next->Borrow().GetDomain()[1], sort.Borrow());
      EXPECT_EQ(NodeTestPeer::NonemptyDeclarationDomains(manager_), 2);
      published = true;
      break;
    }
    Resource(next, ResourceKind::kCppAllocation);
    EXPECT_EQ(NodeTestPeer::DeclarationPayloadUsed(manager_), used);
    EXPECT_EQ(NodeTestPeer::GetStats(manager_).declarations,
              before.declarations);
    EXPECT_EQ(NodeTestPeer::NonemptyDeclarationDomains(manager_), 1);
    ++failures;
  }
  EXPECT_GE(failures, 2);
  EXPECT_TRUE(published);
}

TEST_F(NodeTest,
       DeclarationNamesAndSignaturesSurviveGrowthAndFailedPublication) {
  const auto z = Take(manager_.IntSort());
  const auto b = Take(manager_.BoolSort());
  std::string name(70000, 'n');
  name[200] = '\0';
  std::vector<SortView> domain(8500, z.Borrow());
  domain.back() = b.Borrow();
  const auto decl = Take(manager_.DeclareFunction(name, domain, z.Borrow()));
  const auto instance =
      Take(manager_.InstantiateDeclaration(decl.Borrow(), {}));
  const auto borrowed_name = decl.Borrow().GetNameHint();
  const auto borrowed_domain = instance.Borrow().GetDomain();
  auto builder =
      Take(manager_.BeginDatatypeGroup(std::array{DatatypeHeader{"Enum", 0}}));
  Take(builder.AddConstructor(0, "leaf", {}));
  const auto group = Take(builder.Commit());
  const auto ctor = Take(group.Borrow().GetConstructor(0, 0));
  const auto ctor_name = ctor.GetNameHint();
  for (std::size_t i = 0; i < 320; ++i)
    Take(manager_.DeclareFunction(std::string(450, 'a'), {}, b.Borrow()));
  EXPECT_EQ(borrowed_name, std::string_view(name));
  EXPECT_EQ(ctor_name, "leaf");
  ASSERT_EQ(borrowed_domain.size(), domain.size());
  EXPECT_EQ(borrowed_domain[0], z.Borrow());
  EXPECT_EQ(borrowed_domain[domain.size() - 1], b.Borrow());
  const auto used_bytes = NodeTestPeer::DeclarationPayloadUsed(manager_);
  const auto before = NodeTestPeer::GetStats(manager_);
  NodeTestPeer::FailRetainAfter(manager_, 0);
  const auto failed = manager_.MakeFreshConstant(name, z.Borrow());
  NodeTestPeer::ClearFailures(manager_);
  Resource(failed, ResourceKind::kReferenceCount);
  EXPECT_EQ(NodeTestPeer::DeclarationPayloadUsed(manager_), used_bytes);
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).declarations, before.declarations);
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).decl_instances,
            before.decl_instances);
  const auto fresh = Take(manager_.MakeFreshConstant(name, z.Borrow()));
  EXPECT_EQ(fresh.Borrow().GetDeclaration()->GetDeclaration().GetNameHint(),
            std::string_view(name));
}

TEST_F(NodeTest, OrdinaryInstancesShareTheirOwnDeclarationsSignature) {
  const auto sort = Take(manager_.IntSort());
  const std::array domain{sort.Borrow(), sort.Borrow()};
  const auto f = Take(manager_.DeclareFunction("f", domain, sort.Borrow()));
  const auto g = Take(manager_.DeclareFunction("f", domain, sort.Borrow()));
  const auto a = Take(manager_.InstantiateDeclaration(f.Borrow(), {}));
  const auto b = Take(manager_.InstantiateDeclaration(g.Borrow(), {}));
  EXPECT_NE(a, b);
  EXPECT_EQ(a, Take(manager_.InstantiateDeclaration(f.Borrow(), {})));
  EXPECT_EQ(a.Borrow().GetDeclaration(), f.Borrow());
  const auto borrowed = a.Borrow().GetDomain();
  for (int i = 0; i < 48; ++i) {
    const auto decl = Take(manager_.FreshFunction("", {}, sort.Borrow()));
    Take(manager_.InstantiateDeclaration(decl.Borrow(), {}));
  }
  ASSERT_EQ(borrowed.size(), 2);
  EXPECT_EQ(borrowed[0], sort.Borrow());
  EXPECT_EQ(borrowed[1], sort.Borrow());
  EXPECT_EQ(manager_.GetStorageStats()->instance_index_entries, 0);
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).decl_instances, 50);
}

TEST_F(NodeTest, EveryOrdinaryDeclarationCategoryHasFreshIdentity) {
  const auto b = Take(manager_.BoolSort());
  const auto z = Take(manager_.IntSort());
  const std::array domain = {b.Borrow(), z.Borrow()};
  const auto first = Take(manager_.DeclareFunction("f", domain, z.Borrow()));
  const auto second = Take(manager_.DeclareFunction("f", domain, z.Borrow()));
  const auto internal = Take(manager_.FreshFunction("f", domain, z.Borrow()));
  const auto formal = Take(manager_.MakeFormal("f", z.Borrow()));
  EXPECT_NE(first, second);
  EXPECT_EQ(first.Borrow().GetKind(), DeclKind::kFunction);
  EXPECT_EQ(internal.Borrow().GetKind(), DeclKind::kInternalFunction);
  EXPECT_EQ(formal.Borrow().GetKind(), DeclKind::kFormal);
  EXPECT_EQ(first.Borrow().GetNameHint(), "f");
  EXPECT_EQ(first.Borrow().GetParameterCount(), 0);
  EXPECT_EQ(first.Borrow().GetDomain()[1].GetId(), z.GetId());
  EXPECT_EQ(first.Borrow().GetRange()->GetId(), z.GetId());
  EXPECT_FALSE(first.Borrow().GetConstructor());
  const auto a = Take(manager_.InstantiateDeclaration(first.Borrow(), {}));
  EXPECT_EQ(a, Take(manager_.InstantiateDeclaration(first.Borrow(), {})));
  const std::array type_args = {z.Borrow()};
  Error(manager_.InstantiateDeclaration(first.Borrow(), type_args),
        ErrorCode::kInvalidArity);
  const auto truth = Take(manager_.MakeBool(true));
  const auto number = Int(3);
  const std::array children = {truth.Borrow(), number.Borrow()};
  const auto application = Take(manager_.MakeApply(a.Borrow(), children));
  EXPECT_EQ(application.GetSort(), z);
  EXPECT_EQ(application.Borrow().GetDeclaration()->GetId(), a.GetId());
  EXPECT_EQ(application, Take(manager_.MakeApply(a.Borrow(), children)));
  const auto f = Take(manager_.InstantiateDeclaration(formal.Borrow(), {}));
  const auto placeholder = Take(manager_.MakeApply(f.Borrow(), {}));
  EXPECT_EQ(placeholder.Borrow().GetDeclaration()->GetDeclaration().GetKind(),
            DeclKind::kFormal);
  const auto constant = Take(manager_.DeclareFunction("c", {}, b.Borrow()));
  const auto c = Take(manager_.InstantiateDeclaration(constant.Borrow(), {}));
  EXPECT_EQ(Take(manager_.MakeApply(c.Borrow(), {})).GetSort(), b);
  Error(manager_.MakeApply(a.Borrow(), {}), ErrorCode::kInvalidArity);
  const std::array wrong = {number.Borrow(), truth.Borrow()};
  Error(manager_.MakeApply(a.Borrow(), wrong), ErrorCode::kSortMismatch);
  auto other = Take(NodeManager::Create());
  Error(other.MakeApply(a.Borrow(), children), ErrorCode::kForeignManager);
  Error(other.DeclareFunction("bad", domain, z.Borrow()),
        ErrorCode::kForeignManager);
  Error(manager_.MakeApply({}, {}), ErrorCode::kNullObject);
  EXPECT_NE(Const(z.Borrow()), Const(z.Borrow()));
}

TEST_F(NodeTest, UfSignaturesCoverAllTheoryCombinations) {
  const auto b = Take(manager_.BoolSort());
  const auto z = Take(manager_.IntSort());
  const auto v = Bv(8);
  const auto f = Fp(8, 24);
  const auto r = Take(manager_.RoundingModeSort());
  const auto a = Take(manager_.MakeArraySort(v.Borrow(), f.Borrow()));
  const auto u_ctor = Take(manager_.DeclareUninterpretedSort("U", 0));
  const auto u = Take(manager_.InstantiateSort(u_ctor.Borrow(), {}));
  const std::array sorts = {b.Borrow(), z.Borrow(), v.Borrow(), f.Borrow(),
                            r.Borrow(), a.Borrow(), u.Borrow()};
  std::vector<Node> arguments;
  std::vector<NodeView> views;
  for (const auto sort : sorts) arguments.push_back(Const(sort));
  for (const auto& node : arguments) views.push_back(node.Borrow());
  for (const auto range : sorts) {
    const auto decl = Take(manager_.DeclareFunction("mix", sorts, range));
    const auto instance =
        Take(manager_.InstantiateDeclaration(decl.Borrow(), {}));
    const auto node = Take(manager_.MakeApply(instance.Borrow(), views));
    EXPECT_EQ(node.Borrow().GetSort().GetId(), range.GetId());
  }
}

}  // namespace
}  // namespace dandelion::testing
