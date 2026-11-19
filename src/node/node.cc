module dandelion.node;
import :internal;
import std;

namespace dandelion::detail {
#ifdef DANDELION_NODE_TESTING
std::atomic<std::uint64_t> destroyed_states{0};
thread_local std::optional<std::size_t> creation_failure;
#endif
ManagerState::~ManagerState() noexcept {
  if (table.Size() != 0) std::terminate();
#ifdef DANDELION_NODE_TESTING
  ++destroyed_states;
#endif
}
void RetainState(ManagerState* state) {
  RetainCheckpoint(state);
  if (state->references >= ReferenceCapacity(state)) throw ReferenceLimit{};
  ++state->references;
}
void ReleaseState(ManagerState* state) noexcept {
  if (state->references == 0) std::terminate();
  if (--state->references == 0) delete state;
}
namespace {
template <class View>
StateHandle<View> Retained(ManagerState* state, View view) {
  CheckView(state, view);
  RetainState(state);
  return Access::Own<View>(state, view.GetId());
}
bool Bit(IntegerView value, std::uint64_t position) noexcept {
  return ((value.MagnitudeChunk(position / 64) >> (position % 64)) & 1) != 0;
}
bool Field(IntegerView value, std::uint64_t start, std::uint64_t count,
           bool all) noexcept {
  if (all && value.BitLength() < start + count) return false;
  const auto end = std::min(start + count, value.BitLength());
  for (auto position = start; position < end;) {
    const auto take =
        std::min(std::uint64_t{64} - position % 64, end - position);
    const auto mask = take == 64 ? std::numeric_limits<std::uint64_t>::max()
                                 : (std::uint64_t{1} << take) - 1;
    const auto part =
        (value.MagnitudeChunk(position / 64) >> (position % 64)) & mask;
    if (all && part != mask) return false;
    if (!all && part != 0) return true;
    position += take;
  }
  return all;
}
}  // namespace

SortId CompleteKeySort(ManagerState* state, const QueryKey& key) {
  if (key.kind >= Kind::kNot) return InferBuiltinSort(state, key);
  if (key.kind == Kind::kApply) {
#ifdef DANDELION_NODE_TESTING
    ++state->type_evaluations;
#endif
    const auto [domain, range] = SignatureAt(state, key.declaration);
    Require(domain.size() == key.children.size(), ErrorCode::kInvalidArity);
    for (std::size_t i = 0; i < domain.size(); ++i) {
      const auto operand = i;
      const auto* child = CheckNode(state, key.children[i], operand);
      Require(child->sort_id == domain[i], ErrorCode::kSortMismatch, operand);
    }
    return range;
  }
  if (key.kind == Kind::kBvValue) {
    const auto& sort = SortAt(state, key.input_sort);
    Require(sort.kind == SortKind::kBitVector, ErrorCode::kSortMismatch);
    if (key.tag == PayloadTag::kBvWord) {
      Require(
          static_cast<std::uint32_t>(std::bit_width(key.word)) <= sort.first,
          ErrorCode::kInvalidLiteral);
    } else {
      Require(key.integer.Sign() >= 0 && key.integer.BitLength() <= sort.first,
              ErrorCode::kInvalidLiteral);
    }
  }
  return key.input_sort;
}
}  // namespace dandelion::detail

namespace dandelion {
using namespace detail;
ManagerRef::ManagerRef(ManagerState* state) noexcept : state_(state) {}
ManagerRef::ManagerRef(const ManagerRef& other) : state_(other.state_) {
  if (state_) detail::RetainState(state_);
}
ManagerRef& ManagerRef::operator=(const ManagerRef& other) {
  if (this != &other) {
    if (other.state_) detail::RetainState(other.state_);
    if (state_) ReleaseState(state_);
    state_ = other.state_;
  }
  return *this;
}
ManagerRef::ManagerRef(ManagerRef&& other) noexcept
    : state_(std::exchange(other.state_, nullptr)) {}
ManagerRef& ManagerRef::operator=(ManagerRef&& other) noexcept {
  if (this != &other) {
    if (state_) ReleaseState(state_);
    state_ = std::exchange(other.state_, nullptr);
  }
  return *this;
}
ManagerRef::~ManagerRef() noexcept {
  if (state_) ReleaseState(state_);
}
bool ManagerRef::IsNull() const noexcept { return state_ == nullptr; }
Result<NodeManager> NodeManager::Create() {
  return Guard<NodeManager>([] {
    NodeManager manager;
#ifdef DANDELION_NODE_TESTING
    CreationAllocationCheckpoint();
#endif
    manager.state_ = Access::OwnState(new ManagerState);
    return manager;
  });
}
Result<NodeManager> NodeManager::Attach(const ManagerRef& reference) {
  return Guard<NodeManager>([&] {
    Require(!reference.IsNull(), ErrorCode::kNullObject);
    NodeManager manager;
    manager.state_ = reference;
    return manager;
  });
}
Result<NodeStorageStats> NodeManager::GetStorageStats() const noexcept {
  const auto* state = Access::State(*this);
  if (!state) return std::unexpected(NodeError{ErrorCode::kNullObject});
  NodeStorageStats stats;
  state->table.MeasureStorage(stats);
  stats.declarations = state->declarations.size();
  stats.instances = state->instances.size();
  stats.sort_index_entries = state->sort_index.size();
  stats.instance_index_entries = state->instance_index.size();
  stats.metadata_index_buckets =
      state->sort_index.bucket_count() + state->instance_index.bucket_count();
  auto& bytes = stats.metadata_bytes;
  bytes = sizeof(ManagerState);
  bytes += VectorBytes(state->sorts) + VectorBytes(state->sort_constructors) +
           VectorBytes(state->declarations) + VectorBytes(state->instances) +
           VectorBytes(state->declaration_domain_spans) +
           VectorBytes(state->substituted_signatures) +
           VectorBytes(state->groups);
  for (const auto& sort : state->sorts)
    bytes += sizeof(*sort) + VectorBytes(sort->arguments);
  for (const auto& constructor : state->sort_constructors)
    bytes += sizeof(*constructor) + StringBytes(constructor->name);
  bytes += state->declaration_names.StorageBytes() +
           state->declaration_domains.StorageBytes();
  for (const auto& decl : state->declarations) {
    if (decl.datatype)
      bytes +=
          sizeof(*decl.datatype) + VectorBytes(decl.datatype->domain_recipes);
  }
  for (const auto& instance : state->substituted_signatures)
    bytes += sizeof(*instance) + VectorBytes(instance->arguments) +
             VectorBytes(instance->domain);
  for (const auto& group : state->groups) {
    bytes += sizeof(*group) + VectorBytes(group->datatypes) +
             VectorBytes(group->recipes);
    for (const auto& recipe : group->recipes)
      bytes += VectorBytes(recipe.arguments);
    for (const auto& datatype : group->datatypes) {
      bytes += VectorBytes(datatype.constructors);
      for (const auto& constructor : datatype.constructors)
        bytes += VectorBytes(constructor.selectors);
    }
  }
  return stats;
}
Result<void> NodeManager::CompactNodeTable() {
  return Guard<void>([&] {
    auto* state = CheckedState(*this);
    state->table.Compact(state);
  });
}
Result<ManagerRef> NodeManager::RetainState() const {
  return Guard<ManagerRef>([&] {
    auto* state = CheckedState(*this);
    detail::RetainState(state);
    return Access::OwnState(state);
  });
}
Result<Node> NodeManager::RetainNode(NodeView node) {
  return Guard<Node>([&] {
    auto* state = CheckedState(*this);
    auto* data = CheckNode(state, node, 0);
    RetainNodeData(data);
    return Access::OwnNode(data);
  });
}
Result<void> NodeManager::ReleaseRoots(std::span<Node> roots) {
  return Guard<void>([&] {
    auto* state = CheckedState(*this);
    for (std::size_t i = 0; i < roots.size(); ++i) {
      const auto* node = Access::Data(roots[i].Borrow());
      Require(!node || node->state == state, ErrorCode::kForeignManager, i);
    }
    ReleaseRootBatch(state, roots);
  });
}
Result<Sort> NodeManager::RetainSort(SortView sort) {
  return Guard<Sort>([&] { return Retained(CheckedState(*this), sort); });
}
Result<SortConstructor> NodeManager::RetainSortConstructor(
    SortConstructorView constructor) {
  return Guard<SortConstructor>(
      [&] { return Retained(CheckedState(*this), constructor); });
}
Result<Decl> NodeManager::RetainDeclaration(DeclView declaration) {
  return Guard<Decl>(
      [&] { return Retained(CheckedState(*this), declaration); });
}
Result<DeclInstance> NodeManager::RetainDeclInstance(
    DeclInstanceView instance) {
  return Guard<DeclInstance>(
      [&] { return Retained(CheckedState(*this), instance); });
}
Result<DatatypeGroup> NodeManager::RetainDatatypeGroup(
    DatatypeGroupView group) {
  return Guard<DatatypeGroup>(
      [&] { return Retained(CheckedState(*this), group); });
}

bool operator==(const Node& left, const Node& right) noexcept {
  return left.node_ == right.node_;
}
bool operator==(NodeView left, NodeView right) noexcept {
  return left.node_ == right.node_;
}
bool operator==(const SortView& left, const SortView& right) noexcept {
  return left.state_ == right.state_ && left.id_ == right.id_;
}
bool operator==(const SortConstructorView& left,
                const SortConstructorView& right) noexcept {
  return left.state_ == right.state_ && left.id_ == right.id_;
}
bool operator==(const DeclView& left, const DeclView& right) noexcept {
  return left.state_ == right.state_ && left.id_ == right.id_;
}
bool operator==(const DeclInstanceView& left,
                const DeclInstanceView& right) noexcept {
  return left.state_ == right.state_ && left.id_ == right.id_;
}
bool operator==(const DatatypeGroupView& left,
                const DatatypeGroupView& right) noexcept {
  return left.state_ == right.state_ && left.id_ == right.id_;
}
bool operator==(const NodeChildren::Iterator& left,
                const NodeChildren::Iterator& right) noexcept {
  return left.current_ == right.current_;
}
Node::Node(NodeData* node) noexcept : node_(node) {}
Node::Node(const Node& other) : node_(other.node_) {
  if (node_) RetainNodeData(node_);
}
Node& Node::operator=(const Node& other) {
  if (this != &other) {
    if (other.node_) RetainNodeData(other.node_);
    if (node_) ReleaseNodeData(node_);
    node_ = other.node_;
  }
  return *this;
}
Node::Node(Node&& other) noexcept
    : node_(std::exchange(other.node_, nullptr)) {}
Node& Node::operator=(Node&& other) noexcept {
  if (this != &other) {
    if (node_) ReleaseNodeData(node_);
    node_ = std::exchange(other.node_, nullptr);
  }
  return *this;
}
Node::~Node() noexcept {
  if (node_) ReleaseNodeData(node_);
}
NodeView Node::Borrow() const& noexcept { return NodeView(node_); }
NodeId Node::GetId() const noexcept { return node_ ? node_->id : 0; }
Sort Node::GetSort() const {
  detail::RetainState(node_->state);
  return Access::Own<SortView>(node_->state, node_->sort_id);
}
Result<Node> Node::Child(std::size_t index) const {
  return Guard<Node>([&] {
    Require(node_ != nullptr, ErrorCode::kNullObject);
    Require(index < node_->ChildCount(), ErrorCode::kInvalidParameter);
    auto* child = Children(node_)[index];
    RetainNodeData(child);
    return Access::OwnNode(child);
  });
}
std::size_t NodeHash::operator()(const Node& node) const noexcept {
  if (!node.GetId()) return 0;
  return static_cast<std::size_t>(
      Mix(reinterpret_cast<std::uintptr_t>(Access::Data(node.Borrow()))));
}
NodeView::NodeView(NodeData* node) noexcept : node_(node) {}
bool NodeView::IsNull() const noexcept { return node_ == nullptr; }
NodeId NodeView::GetId() const noexcept { return node_ ? node_->id : 0; }
Kind NodeView::GetKind() const noexcept { return node_->GetKind(); }
SortView NodeView::GetSort() const noexcept {
  return Access::ViewOf<SortView>(node_->state, node_->sort_id);
}
NodeChildren NodeView::GetChildren() const noexcept {
  return NodeChildren(Children(node_));
}
NodeIndices NodeView::GetIndices() const noexcept { return NodeIndices(node_); }
std::size_t NodeIndices::size() const noexcept {
  switch (node_->payload_tag) {
    case PayloadTag::kIndex1:
    case PayloadTag::kIndexWide:
      return 1;
    case PayloadTag::kIndex2:
    case PayloadTag::kRationalConversion:
      return 2;
    default:
      return 0;
  }
}
bool NodeIndices::empty() const noexcept { return size() == 0; }
NodeIndices::Index NodeIndices::operator[](std::size_t index) const noexcept {
  switch (node_->payload_tag) {
    case PayloadTag::kIndex1:
      return Index(IntegerView(Value<std::uint32_t>(node_)));
    case PayloadTag::kIndexWide:
      return Index(Value<Integer>(node_).Borrow());
    case PayloadTag::kIndex2:
      return Index(
          IntegerView(Value<std::array<std::uint32_t, 2>>(node_)[index]));
    case PayloadTag::kRationalConversion:
      return Index(IntegerView(Value<RationalPayload>(node_).indices[index]));
    default:
      std::unreachable();  // A valid index requires a nonempty range.
  }
}
NodeIndices::Iterator::Iterator(const NodeData* node,
                                std::size_t index) noexcept
    : node_(node), index_(index) {}
NodeIndices::Index NodeIndices::Iterator::operator*() const noexcept {
  return NodeIndices(node_)[index_];
}
NodeIndices::Iterator& NodeIndices::Iterator::operator++() noexcept {
  ++index_;
  return *this;
}
bool operator==(const NodeIndices::Iterator& left,
                const NodeIndices::Iterator& right) noexcept {
  return left.node_ == right.node_ && left.index_ == right.index_;
}
NodeIndices::Iterator NodeIndices::begin() const noexcept {
  return Iterator(node_, 0);
}
NodeIndices::Iterator NodeIndices::end() const noexcept {
  return Iterator(node_, size());
}
std::optional<DeclInstanceView> NodeView::GetDeclaration() const noexcept {
  if (node_->payload_tag != PayloadTag::kDeclaration) return std::nullopt;
  return Access::ViewOf<DeclInstanceView>(node_->state,
                                          Value<DeclInstanceId>(node_));
}
std::optional<LiteralView> NodeView::GetValue() const noexcept {
  switch (node_->payload_tag) {
    case PayloadTag::kBool:
    case PayloadTag::kInteger:
    case PayloadTag::kBvWord:
    case PayloadTag::kFpSpecial:
    case PayloadTag::kFpWord:
    case PayloadTag::kFpWide:
    case PayloadTag::kRoundingMode:
      return LiteralView(node_);
    default:
      return std::nullopt;
  }
}
std::optional<RationalView> NodeView::GetRationalParameter() const noexcept {
  if (node_->payload_tag != PayloadTag::kRationalConversion)
    return std::nullopt;
  return Value<RationalPayload>(node_).value.Borrow();
}
bool NodeView::SameManager(NodeView other) const noexcept {
  return node_ && other.node_ && node_->state == other.node_->state;
}
NodeChildren::NodeChildren(std::span<NodeData* const> nodes) noexcept
    : nodes_(nodes) {}
std::size_t NodeChildren::size() const noexcept { return nodes_.size(); }
bool NodeChildren::empty() const noexcept { return nodes_.empty(); }
NodeView NodeChildren::operator[](std::size_t index) const noexcept {
  return Access::ViewNode(nodes_[index]);
}
NodeChildren::Iterator::Iterator(NodeData* const* current) noexcept
    : current_(current) {}
NodeView NodeChildren::Iterator::operator*() const noexcept {
  return Access::ViewNode(*current_);
}
NodeChildren::Iterator& NodeChildren::Iterator::operator++() noexcept {
  ++current_;
  return *this;
}
NodeChildren::Iterator NodeChildren::begin() const noexcept {
  return Iterator(nodes_.data());
}
NodeChildren::Iterator NodeChildren::end() const noexcept {
  return Iterator(nodes_.empty() ? nodes_.data()
                                 : nodes_.data() + nodes_.size());
}
LiteralView::LiteralView(const NodeData* node) noexcept : node_(node) {}
std::optional<bool> LiteralView::GetBool() const noexcept {
  if (node_->payload_tag != PayloadTag::kBool) return std::nullopt;
  return Value<bool>(node_);
}
std::optional<IntegerView> LiteralView::GetInteger() const noexcept {
  if (node_->payload_tag == PayloadTag::kBvWord)
    return IntegerView::FromUnsigned(Value<std::uint64_t>(node_));
  if (node_->payload_tag != PayloadTag::kInteger) return std::nullopt;
  return Value<Integer>(node_).Borrow();
}
std::optional<FpCategory> LiteralView::GetFpCategory() const noexcept {
  switch (node_->payload_tag) {
    case PayloadTag::kFpSpecial:
      return Value<FpCategory>(node_);
    case PayloadTag::kFpWord:
    case PayloadTag::kFpWide:
      return FpCategory::kFiniteBits;
    default:
      return std::nullopt;
  }
}
std::optional<IntegerView> LiteralView::GetFpBits() const noexcept {
  if (node_->payload_tag == PayloadTag::kFpWord)
    return IntegerView::FromUnsigned(Value<std::uint64_t>(node_));
  if (node_->payload_tag != PayloadTag::kFpWide) return std::nullopt;
  return Value<Integer>(node_).Borrow();
}
std::optional<RoundingMode> LiteralView::GetRoundingMode() const noexcept {
  if (node_->payload_tag != PayloadTag::kRoundingMode) return std::nullopt;
  return Value<RoundingMode>(node_);
}

Result<Node> NodeManager::MakeBool(bool value) {
  return Guard<Node>([&] {
    auto* state = CheckedState(*this);
    QueryKey key{Kind::kBoolValue, ScalarSortId(state, SortKind::kBool), {}};
    key.tag = PayloadTag::kBool;
    key.boolean = value;
    return InternNode(state, key);
  });
}
Result<Node> NodeManager::MakeInt(IntegerView value) {
  return Guard<Node>([&] {
    auto* state = CheckedState(*this);
    QueryKey key{Kind::kIntValue, ScalarSortId(state, SortKind::kInt), {}};
    key.tag = PayloadTag::kInteger;
    key.integer = value;
    return InternNode(state, key);
  });
}
Result<Node> NodeManager::MakeBvValue(SortView sort, IntegerView value) {
  return Guard<Node>([&] {
    auto* state = CheckedState(*this);
    CheckView(state, sort);
    QueryKey key{Kind::kBvValue, sort.GetId(), {}};
    if (const auto word = value.TryUint64()) {
      key.tag = PayloadTag::kBvWord;
      key.word = *word;
    } else {
      key.tag = PayloadTag::kInteger;
      key.integer = value;
    }
    return InternNode(state, key);
  });
}
Result<Node> NodeManager::MakeFpBits(SortView sort, IntegerView bits) {
  return Guard<Node>([&] {
    auto* state = CheckedState(*this);
    CheckView(state, sort);
    Require(sort.GetKind() == SortKind::kFloatingPoint,
            ErrorCode::kSortMismatch);
    const auto e = *sort.GetFpExponentWidth();
    const auto s = *sort.GetFpSignificandWidth();
    Require(bits.Sign() >= 0 &&
                bits.BitLength() <= static_cast<std::uint64_t>(e) + s,
            ErrorCode::kInvalidLiteral);
    const bool negative = Bit(bits, static_cast<std::uint64_t>(e) + s - 1);
    const bool fraction = Field(bits, 0, s - 1, false);
    FpCategory category = FpCategory::kFiniteBits;
    if (Field(bits, s - 1, e, true)) {
      category = fraction   ? FpCategory::kNaN
                 : negative ? FpCategory::kNegativeInfinity
                            : FpCategory::kPositiveInfinity;
    } else if (!fraction && !Field(bits, s - 1, e, false)) {
      category =
          negative ? FpCategory::kNegativeZero : FpCategory::kPositiveZero;
    }
    QueryKey key{Kind::kFpValue, sort.GetId(), {}};
    if (category != FpCategory::kFiniteBits) {
      key.tag = PayloadTag::kFpSpecial;
      key.fp_category = category;
    } else if (const auto word = bits.TryUint64()) {
      key.tag = PayloadTag::kFpWord;
      key.word = *word;
    } else {
      key.tag = PayloadTag::kFpWide;
      key.integer = bits;
    }
    return InternNode(state, key);
  });
}
Result<Node> NodeManager::MakeFpSpecial(SortView sort, FpSpecial value) {
  return Guard<Node>([&] {
    auto* state = CheckedState(*this);
    CheckView(state, sort);
    Require(sort.GetKind() == SortKind::kFloatingPoint,
            ErrorCode::kSortMismatch);
    Require(std::to_underlying(value) <=
                std::to_underlying(FpSpecial::kNegativeZero),
            ErrorCode::kInvalidLiteral);
    QueryKey key{Kind::kFpValue, sort.GetId(), {}};
    key.tag = PayloadTag::kFpSpecial;
    key.fp_category = static_cast<FpCategory>(value);
    return InternNode(state, key);
  });
}
Result<Node> NodeManager::MakeRoundingMode(RoundingMode value) {
  return Guard<Node>([&] {
    auto* state = CheckedState(*this);
    Require(std::to_underlying(value) <= std::to_underlying(RoundingMode::kRtz),
            ErrorCode::kInvalidLiteral);
    QueryKey key{Kind::kRoundingModeValue,
                 ScalarSortId(state, SortKind::kRoundingMode),
                 {}};
    key.tag = PayloadTag::kRoundingMode;
    key.rounding_mode = value;
    return InternNode(state, key);
  });
}
Result<Sort> NodeManager::CheckBuiltin(BuiltinOpView op,
                                       std::span<const NodeView> children) {
  return Guard<Sort>([&] {
    auto* state = CheckedState(*this);
    StateLease lease(state);
    const auto id = InferBuiltinSort(state, BuiltinKey(state, op, children));
    lease.Transfer();
    return Access::Own<SortView>(state, id);
  });
}
Result<Node> NodeManager::MakeBuiltin(BuiltinOpView op,
                                      std::span<const NodeView> children) {
  return Guard<Node>([&] {
    auto* state = CheckedState(*this);
    // Const-array stores its explicit qualifier in the result sort, even
    // though its node has no additional payload.
    if (op.kind >= Kind::kNot && op.kind < Kind::kArrayConst &&
        op.indices.empty() && !op.qualifier && !op.rational)
      return InternPlainBuiltin(state, op.kind, children);
    return InternNode(state, BuiltinKey(state, op, children));
  });
}
Result<Node> NodeManager::MakeApply(DeclInstanceView declaration,
                                    std::span<const NodeView> arguments) {
  return Guard<Node>([&] {
    auto* state = CheckedState(*this);
    CheckView(state, declaration);
    QueryKey key{Kind::kApply, 0, arguments};
    key.tag = PayloadTag::kDeclaration;
    key.declaration = declaration.GetId();
    return InternNode(state, key);
  });
}
Result<Node> NodeManager::MakeFreshConstant(std::string_view name,
                                            SortView sort) {
  return Guard<Node>([&] {
    auto* state = CheckedState(*this);
    CheckView(state, sort);
    CheckNodeIdentity(state);
    const auto decl_id = NextStoreId(state, state->declarations.size());
    const auto instance_id = NextStoreId(state, state->instances.size());
    DeclarationTransaction transaction(state);
    auto decl = PrepareDeclaration(state, DeclKind::kInternalFunction, name,
                                   sort.GetId());
    ReserveAppend(state, state->declarations);
    ReserveAppend(state, state->instances);
    state->table.EnsureSpace(state);
    QueryKey key{Kind::kApply, 0, {}};
    key.tag = PayloadTag::kDeclaration;
    key.declaration = instance_id;
    auto hash = HashKey(key);
#ifdef DANDELION_NODE_TESTING
    if (state->collision_hash) hash = *state->collision_hash;
#endif
    auto candidate = PrepareNode(state, key, sort.GetId(), hash);
    // All capacity, allocation and retain operations precede this publication.
    PublishSingleInstance(state, decl_id, decl, instance_id);
    state->declarations.push_back(std::move(decl));
    transaction.Commit();
    return PublishNode(state, state->table.FindVacant(hash),
                       std::move(candidate));
  });
}
}  // namespace dandelion
