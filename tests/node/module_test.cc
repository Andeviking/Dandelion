#include <gtest/gtest.h>

import std;
import dandelion.node;

namespace dandelion::testing {
namespace {

TEST(NodeModule, NodeOnlyConsumerCanNameAndUseDiagnostics) {
  Result<NodeManager> manager = NodeManager::Create();
  ASSERT_TRUE(manager);
  Result<Node> literal = manager->MakeBool(true);
  ASSERT_TRUE(literal);
  EXPECT_EQ(literal->Borrow().GetValue()->GetBool(), true);

  const Result<Node> invalid = manager->RetainNode({});
  ASSERT_FALSE(invalid);
  const NodeError error = invalid.error();
  EXPECT_EQ(error.code, ErrorCode::kNullObject);
  EXPECT_EQ(error.resource, ResourceKind::kNone);
  EXPECT_EQ(error.datatype_reason, DatatypeReason::kNone);
  static_assert(std::derived_from<ReferenceLimit, std::exception>);
  EXPECT_NE(ReferenceLimit{}.what(), nullptr);
}

}  // namespace
}  // namespace dandelion::testing
