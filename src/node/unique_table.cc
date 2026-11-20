module dandelion.node;
import :internal;
import dandelion.util.numeric;
import std;

namespace dandelion::detail {
namespace {
constexpr std::size_t AlignUp(std::size_t value,
                              std::size_t alignment) noexcept {
  return (value + alignment - 1) & ~(alignment - 1);
}
constexpr std::size_t ChildOffset(std::size_t count) noexcept {
  const auto prefix =
      count >= NodeData::kExternalChildCount ? sizeof(std::uint64_t) : 0;
  return AlignUp(sizeof(NodeData) + prefix, alignof(NodeData*));
}
static_assert(std::to_underlying(Kind::kArrayConst) <=
              std::numeric_limits<std::uint8_t>::max());
static_assert(sizeof(void*) != 8 || sizeof(NodeData) == 56);
constexpr auto kDefaultAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
static_assert(alignof(NodeData) <= kDefaultAlignment);
static_assert(alignof(Integer) <= kDefaultAlignment);
static_assert(alignof(RationalWords) <= kDefaultAlignment);
static_assert(alignof(std::uint64_t) <= kDefaultAlignment);
static_assert(sizeof(RationalPackedPrefix) % alignof(std::uint64_t) == 0);

struct PayloadLayout {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t size;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t alignment;
};

template <class T>
constexpr PayloadLayout LayoutOf() noexcept {
  return {sizeof(T), alignof(T)};
}
constexpr PayloadLayout Layout(PayloadTag tag) noexcept {
  switch (tag) {
    case PayloadTag::kNone:
    case PayloadTag::kFpNaN:
    case PayloadTag::kFpPositiveInfinity:
    case PayloadTag::kFpNegativeInfinity:
    case PayloadTag::kFpPositiveZero:
    case PayloadTag::kFpNegativeZero:
      return {0, 1};
    case PayloadTag::kBool:
      return LayoutOf<bool>();
    case PayloadTag::kRoundingMode:
      return LayoutOf<RoundingMode>();
    case PayloadTag::kDeclaration:
      return LayoutOf<DeclInstanceId>();
    case PayloadTag::kIndex1:
      return LayoutOf<std::uint32_t>();
    case PayloadTag::kIndex2:
      return LayoutOf<std::array<std::uint32_t, 2>>();
    case PayloadTag::kIntWord:
      return LayoutOf<std::int64_t>();
    case PayloadTag::kBvWord:
    case PayloadTag::kFpWord:
      return LayoutOf<std::uint64_t>();
    case PayloadTag::kIntWidePositive:
    case PayloadTag::kIntWideNegative:
    case PayloadTag::kBvWide:
    case PayloadTag::kFpWide:
      return {0, alignof(std::uint64_t)};
    case PayloadTag::kIndexWide:
      return LayoutOf<Integer>();
    case PayloadTag::kRationalConversion:
      return LayoutOf<RationalWords>();
    case PayloadTag::kRationalWidePositive:
    case PayloadTag::kRationalWideNegative:
      return {sizeof(RationalPackedPrefix), alignof(std::uint64_t)};
  }
  std::unreachable();
}
constexpr std::size_t PayloadOffset(std::size_t count,
                                    PayloadTag tag) noexcept {
  return AlignUp(ChildOffset(count) + count * sizeof(NodeData*),
                 Layout(tag).alignment);
}

struct CandidateLayout {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t size;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t child_offset;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t payload_offset;
};
std::uint32_t CheckedLimbCount(std::uint64_t count) {
  if (count > std::numeric_limits<std::uint32_t>::max())
    Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
  return static_cast<std::uint32_t>(count);
}

std::uint32_t LimbCount(IntegerView value) {
  const auto bits = value.BitLength();
  return CheckedLimbCount(bits / 64 + (bits % 64 != 0));
}

std::size_t TailEnd(std::size_t prefix, std::size_t limbs) noexcept {
  return prefix + limbs * sizeof(std::uint64_t);
}

std::size_t CheckedTailEnd(std::size_t prefix, std::uint64_t limbs,
                           std::size_t limit) {
  if (prefix > limit || limbs > (limit - prefix) / sizeof(std::uint64_t))
    Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
  return TailEnd(prefix, static_cast<std::size_t>(limbs));
}

CandidateLayout CheckedLayout(std::size_t count, PayloadTag tag,
                              std::uint64_t limbs, std::size_t limit) {
  const auto [size, alignment] = Layout(tag);
  const auto child_offset = ChildOffset(count);
  if (count > (std::numeric_limits<std::size_t>::max() - child_offset) /
                  sizeof(NodeData*)) {
    Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
  }
  const auto child_end = child_offset + count * sizeof(NodeData*);
  if (child_end > std::numeric_limits<std::size_t>::max() - (alignment - 1))
    Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
  const auto payload_offset = AlignUp(child_end, alignment);
  if (payload_offset > limit || size > limit - payload_offset)
    Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
  return {CheckedTailEnd(payload_offset + size, limbs, limit), child_offset,
          payload_offset};
}
void FreeStorage(NodeData* node) noexcept {
  auto* storage = node->storage;
  std::destroy_at(node);
  ::operator delete(storage);
}
void CopyMagnitude(IntegerView value, std::span<std::uint64_t> out) noexcept {
  const auto limbs = value.MagnitudeLimbs();
  if (limbs.empty()) {
    out.front() = value.MagnitudeChunk(0);
  } else {
    std::ranges::copy(limbs, out.begin());
  }
}

void CopyIndex(ManagerState* state, void* address, IntegerView value) {
  try {
    AllocationCheckpoint(state);
    std::construct_at(static_cast<Integer*>(address), value.Copy());
  } catch (const std::bad_alloc&) {
    throw Failure{
        TranslateNumericError(numeric::NumericError::kCppAllocation, false,
                              std::numeric_limits<std::uint64_t>::max(), 0)};
  }
}

template <class Child>
void ConstructPayload(ManagerState* state, void* address,
                      const QueryKey<Child>& key, std::uint32_t first_limbs,
                      std::uint32_t second_limbs) {
  switch (key.tag) {
    case PayloadTag::kNone:
    case PayloadTag::kFpNaN:
    case PayloadTag::kFpPositiveInfinity:
    case PayloadTag::kFpNegativeInfinity:
    case PayloadTag::kFpPositiveZero:
    case PayloadTag::kFpNegativeZero:
      break;
    case PayloadTag::kBool:
      std::construct_at(static_cast<bool*>(address), key.boolean);
      break;
    case PayloadTag::kIntWord:
      std::construct_at(static_cast<std::int64_t*>(address),
                        static_cast<std::int64_t>(key.word));
      break;
    case PayloadTag::kIntWidePositive:
    case PayloadTag::kIntWideNegative:
    case PayloadTag::kBvWide:
    case PayloadTag::kFpWide: {
      auto* limbs = ::new (address) std::uint64_t[first_limbs];
      CopyMagnitude(key.integer, {limbs, first_limbs});
      break;
    }
    case PayloadTag::kIndexWide:
      CopyIndex(state, address, key.integer);
      break;
    case PayloadTag::kBvWord:
    case PayloadTag::kFpWord:
      std::construct_at(static_cast<std::uint64_t*>(address), key.word);
      break;
    case PayloadTag::kRoundingMode:
      std::construct_at(static_cast<RoundingMode*>(address), key.rounding_mode);
      break;
    case PayloadTag::kDeclaration:
      // Preserve the application preparation checkpoint for rollback tests.
      AllocationCheckpoint(state);
      std::construct_at(static_cast<DeclInstanceId*>(address), key.declaration);
      break;
    case PayloadTag::kIndex1:
      std::construct_at(static_cast<std::uint32_t*>(address), key.indices[0]);
      break;
    case PayloadTag::kIndex2:
      std::construct_at(static_cast<std::array<std::uint32_t, 2>*>(address),
                        key.indices);
      break;
    case PayloadTag::kRationalConversion:
      std::construct_at(static_cast<RationalWords*>(address), key.indices,
                        *key.rational->Numerator().TryInt64(),
                        *key.rational->Denominator().TryUint64());
      break;
    case PayloadTag::kRationalWidePositive:
    case PayloadTag::kRationalWideNegative: {
      std::construct_at(static_cast<RationalPackedPrefix*>(address),
                        key.indices, first_limbs, second_limbs);
      auto* tail =
          static_cast<std::byte*>(address) + sizeof(RationalPackedPrefix);
      auto* limbs =
          ::new (tail) std::uint64_t[std::size_t{first_limbs} + second_limbs];
      CopyMagnitude(key.rational->Numerator(), {limbs, first_limbs});
      CopyMagnitude(key.rational->Denominator(),
                    {limbs + first_limbs, second_limbs});
      break;
    }
  }
}
}  // namespace

#ifdef DANDELION_NODE_TESTING
Result<std::size_t> CheckNumericNodeSize(PayloadTag tag, std::uint64_t limbs,
                                         std::uint64_t denominator_limbs,
                                         std::size_t limit) {
  return Guard<std::size_t>([&] {
    const bool wide_rational = IsWideRational(tag);
    const auto first =
        IsWideLiteral(tag) || wide_rational ? CheckedLimbCount(limbs) : 0;
    const auto second = wide_rational ? CheckedLimbCount(denominator_limbs) : 0;
    const auto children =
        wide_rational || tag == PayloadTag::kRationalConversion
            ? std::size_t{1}
            : std::size_t{0};
    return CheckedLayout(children, tag, std::uint64_t{first} + second, limit)
        .size;
  });
}

Result<std::size_t> CheckNumericTailSize(std::size_t prefix,
                                         std::uint32_t limbs,
                                         std::size_t limit) {
  return Guard<std::size_t>(
      [&] { return CheckedTailEnd(prefix, limbs, limit); });
}
#endif

std::uint64_t Append(std::uint64_t hash, std::uint64_t value) noexcept {
  return Mix(hash ^ Mix(value + 0x9e3779b97f4a7c15ULL));
}
namespace {
std::uint64_t FoldKeyWord(std::uint64_t hash, std::uint64_t word) noexcept {
  return (hash ^ word) * 0x9e3779b185ebca87ULL;
}

// SipHash's compression keeps independent lanes until finalization, so one
// adversarial chunk cannot directly cancel the state left by its predecessor.
class WideIntegerHash {
 public:
  explicit WideIntegerHash(std::uint64_t seed) noexcept
      : a_(0x736f6d6570736575ULL ^ seed),
        b_(0x646f72616e646f6dULL),
        c_(0x6c7967656e657261ULL ^ seed),
        d_(0x7465646279746573ULL) {}

  void Append(std::uint64_t word) noexcept {
    d_ ^= word;
    Round();
    Round();
    a_ ^= word;
  }
  std::uint64_t Finish(std::uint64_t chunks) noexcept {
    // The message consists entirely of complete 64-bit chunks.
    Append((chunks * 8) << 56);
    c_ ^= 0xff;
    for (int i = 0; i < 4; ++i) Round();
    return a_ ^ b_ ^ c_ ^ d_;
  }

 private:
  void Round() noexcept {
    a_ += b_;
    c_ += d_;
    b_ = std::rotl(b_, 13) ^ a_;
    d_ = std::rotl(d_, 16) ^ c_;
    a_ = std::rotl(a_, 32);
    c_ += b_;
    a_ += d_;
    b_ = std::rotl(b_, 17) ^ c_;
    d_ = std::rotl(d_, 21) ^ a_;
    c_ = std::rotl(c_, 32);
  }

  std::uint64_t a_;
  std::uint64_t b_;
  std::uint64_t c_;
  std::uint64_t d_;
};

std::uint64_t FoldSignedWord(std::uint64_t hash, std::int64_t word) noexcept {
  if (word == 0) return FoldKeyWord(hash, 0);
  const auto bits = static_cast<std::uint64_t>(word);
  const bool negative = word < 0;
  return FoldKeyWord(FoldKeyWord(hash, negative ? 3 : 2),
                     negative ? std::uint64_t{0} - bits : bits);
}

std::uint64_t FoldWideInteger(std::uint64_t hash, IntegerView value,
                              bool negative) noexcept {
  const auto limbs = value.MagnitudeLimbs();
  // An inline unsigned value above INT64_MAX has one limb but no span.
  if (limbs.empty())
    return FoldKeyWord(FoldKeyWord(hash, 2), *value.TryUint64());
  const std::uint64_t chunks = limbs.size();
  hash = FoldKeyWord(hash, chunks * 2 + negative);
  if (chunks == 1) return FoldKeyWord(hash, limbs.front());
  WideIntegerHash wide(hash);
  for (const auto limb : limbs) wide.Append(limb);
  return FoldKeyWord(hash, wide.Finish(chunks));
}

std::uint64_t FoldInteger(std::uint64_t hash, IntegerView value) noexcept {
  if (const auto small = value.TryInt64()) return FoldSignedWord(hash, *small);
  return FoldWideInteger(hash, value, value.Sign() < 0);
}
}  // namespace
std::span<NodeData* const> Children(const NodeData* node) noexcept {
  const auto count = node->ChildCount();
  if (count == 0) return {};
  return {std::launder(reinterpret_cast<NodeData* const*>(node->storage +
                                                          ChildOffset(count))),
          count};
}
const void* Payload(const NodeData* node) noexcept {
  return node->storage + PayloadOffset(node->ChildCount(), node->payload_tag);
}
void* Payload(NodeData* node) noexcept {
  return const_cast<void*>(Payload(static_cast<const NodeData*>(node)));
}
std::span<const std::uint64_t> LiteralLimbs(const NodeData* node) noexcept {
  return {std::launder(static_cast<const std::uint64_t*>(Payload(node))),
          node->literal_limb_count};
}

std::array<std::uint32_t, 2> RationalIndices(const NodeData* node) noexcept {
  if (node->payload_tag == PayloadTag::kRationalConversion)
    return Value<RationalWords>(node).indices;
  return Value<RationalPackedPrefix>(node).indices;
}

RationalView ReadRational(const NodeData* node) noexcept {
  if (node->payload_tag == PayloadTag::kRationalConversion) {
    const auto& value = Value<RationalWords>(node);
    return RationalView::FromCanonicalComponents(
        IntegerView(value.numerator),
        IntegerView::FromUnsigned(value.denominator));
  }
  const auto& prefix = Value<RationalPackedPrefix>(node);
  const auto* tail = static_cast<const std::byte*>(Payload(node)) +
                     sizeof(RationalPackedPrefix);
  const auto* limbs =
      std::launder(reinterpret_cast<const std::uint64_t*>(tail));
  return RationalView::FromCanonicalComponents(
      IntegerView::FromCanonicalMagnitude(
          {limbs, prefix.numerator_limbs},
          node->payload_tag == PayloadTag::kRationalWideNegative),
      IntegerView::FromCanonicalMagnitude(
          {limbs + prefix.numerator_limbs, prefix.denominator_limbs}, false));
}

void DestroyPayload(NodeData* node) noexcept {
  // Trivial objects end their lifetimes with the backing storage.
  if (node->payload_tag == PayloadTag::kIndexWide)
    std::destroy_at(&Value<Integer>(node));
}
namespace {
template <class Child>
std::uint64_t HashShape([[maybe_unused]] ManagerState* state, Kind kind,
                        SortId explicit_sort,
                        std::span<Child> children) noexcept {
#ifdef DANDELION_NODE_TESTING
  ++state->hash_computations;
#endif
  auto hash = FoldKeyWord(0, explicit_sort);
  hash = FoldKeyWord(hash, children.size());
  hash = FoldKeyWord(hash, std::to_underlying(kind));
  for (const auto& child : children)
    hash = FoldKeyWord(hash,
                       reinterpret_cast<std::uintptr_t>(Access::Data(child)));
  return hash;
}
template <class Child>
bool EqualChildren(const NodeData* node, std::span<Child> requested) noexcept {
  const auto children = Children(node);
  for (std::size_t i = 0; i < children.size(); ++i)
    if (children[i] != Access::Data(requested[i])) return false;
  return true;
}
}  // namespace

template <class Child>
std::uint64_t HashKey(ManagerState* state,
                      const QueryKey<Child>& key) noexcept {
  auto hash = HashShape(state, key.kind, key.input_sort, key.children);
  // The plain lookup shares the no-payload hash. A different tag must
  // continue probing even if its hash matches.
  if (key.tag != PayloadTag::kNone)
    hash = FoldKeyWord(hash, std::to_underlying(key.tag));
  switch (key.tag) {
    case PayloadTag::kNone:
    case PayloadTag::kFpNaN:
    case PayloadTag::kFpPositiveInfinity:
    case PayloadTag::kFpNegativeInfinity:
    case PayloadTag::kFpPositiveZero:
    case PayloadTag::kFpNegativeZero:
      break;
    case PayloadTag::kBool:
      hash = FoldKeyWord(hash, key.boolean);
      break;
    case PayloadTag::kIntWord:
      hash = FoldSignedWord(hash, static_cast<std::int64_t>(key.word));
      break;
    case PayloadTag::kIntWidePositive:
    case PayloadTag::kIntWideNegative:
    case PayloadTag::kBvWide:
    case PayloadTag::kFpWide:
      hash = FoldWideInteger(hash, key.integer,
                             key.tag == PayloadTag::kIntWideNegative);
      break;
    case PayloadTag::kIndexWide:
      hash = FoldInteger(hash, key.integer);
      break;
    case PayloadTag::kBvWord:
    case PayloadTag::kFpWord:
      hash = FoldKeyWord(hash, key.word);
      break;
    case PayloadTag::kRoundingMode:
      hash = FoldKeyWord(hash, std::to_underlying(key.rounding_mode));
      break;
    case PayloadTag::kDeclaration:
      hash = FoldKeyWord(hash, key.declaration);
      break;
    case PayloadTag::kIndex1:
      hash = FoldKeyWord(hash, key.indices[0]);
      break;
    case PayloadTag::kIndex2:
      hash = FoldKeyWord(FoldKeyWord(hash, key.indices[0]), key.indices[1]);
      break;
    case PayloadTag::kRationalConversion:
    case PayloadTag::kRationalWidePositive:
    case PayloadTag::kRationalWideNegative:
      hash = FoldKeyWord(FoldKeyWord(hash, key.indices[0]), key.indices[1]);
      hash = FoldInteger(hash, key.rational->Numerator());
      hash = FoldInteger(hash, key.rational->Denominator());
      break;
  }
  return Mix(hash);
}
namespace {
template <class Child>
bool EqualKey(const NodeData* node, const QueryKey<Child>& key) noexcept {
  if (node->GetKind() != key.kind ||
      (key.input_sort && node->sort_id != key.input_sort) ||
      node->ChildCount() != key.children.size() || node->payload_tag != key.tag)
    return false;
  if (!EqualChildren(node, key.children)) return false;
  switch (key.tag) {
    case PayloadTag::kNone:
    case PayloadTag::kFpNaN:
    case PayloadTag::kFpPositiveInfinity:
    case PayloadTag::kFpNegativeInfinity:
    case PayloadTag::kFpPositiveZero:
    case PayloadTag::kFpNegativeZero:
      return true;
    case PayloadTag::kBool:
      return Value<bool>(node) == key.boolean;
    case PayloadTag::kIntWord: {
      const auto* address =
          node->storage + PayloadOffset(0, PayloadTag::kIntWord);
      return *std::launder(reinterpret_cast<const std::int64_t*>(address)) ==
             static_cast<std::int64_t>(key.word);
    }
    case PayloadTag::kIntWidePositive:
    case PayloadTag::kIntWideNegative:
    case PayloadTag::kBvWide:
    case PayloadTag::kFpWide: {
      const auto stored = LiteralLimbs(node);
      const auto requested = key.integer.MagnitudeLimbs();
      // A positive Int outside the signed word range can be an inline
      // unsigned view with one significant limb.
      if (requested.empty())
        return stored.size() == 1 &&
               stored.front() == key.integer.MagnitudeChunk(0);
      return std::ranges::equal(stored, requested);
    }
    case PayloadTag::kIndexWide:
      return Value<Integer>(node).Borrow().Compare(key.integer) == 0;
    case PayloadTag::kBvWord:
    case PayloadTag::kFpWord:
      return Value<std::uint64_t>(node) == key.word;
    case PayloadTag::kRoundingMode:
      return Value<RoundingMode>(node) == key.rounding_mode;
    case PayloadTag::kDeclaration:
      return Value<DeclInstanceId>(node) == key.declaration;
    case PayloadTag::kIndex1:
      return Value<std::uint32_t>(node) == key.indices[0];
    case PayloadTag::kIndex2:
      return Value<std::array<std::uint32_t, 2>>(node) == key.indices;
    case PayloadTag::kRationalConversion:
    case PayloadTag::kRationalWidePositive:
    case PayloadTag::kRationalWideNegative:
      return RationalIndices(node) == key.indices &&
             ReadRational(node) == *key.rational;
  }
  std::unreachable();
}

}  // namespace

UniqueTable::UniqueTable() {
#ifdef DANDELION_NODE_TESTING
  CreationAllocationCheckpoint();
#endif
  slots_.resize(kInitialCapacity);
}
void UniqueTable::MeasureStorage(NodeStorageStats& stats) const noexcept {
  stats.table_bytes = slots_.capacity() * sizeof(NodeData*);
  stats.peak_table_bytes = peak_capacity_ * sizeof(NodeData*);
  stats.live_nodes = size_;
  for (const auto* node : slots_) {
    if (!node) continue;
    const auto count = node->ChildCount();
    stats.live_edges += count;
    const auto tag = node->payload_tag;
    std::size_t limbs = node->literal_limb_count;
    if (IsWideRational(tag)) {
      const auto& prefix = Value<RationalPackedPrefix>(node);
      limbs = std::size_t{prefix.numerator_limbs} + prefix.denominator_limbs;
    }
    stats.node_bytes +=
        TailEnd(PayloadOffset(count, tag) + Layout(tag).size, limbs);
    if (tag == PayloadTag::kIndexWide)
      stats.numeric_backing_bytes +=
          Value<Integer>(node).Borrow().BackingBytes();
  }
}
#ifdef DANDELION_NODE_TESTING
void UniqueTable::RecordProbe(std::size_t count) noexcept {
  const auto bucket =
      std::min(static_cast<std::size_t>(std::bit_width(count) - 1),
               probe_histogram_.size() - 1);
  ++probe_histogram_[bucket];
  probe_slots_ += count;
  probe_max_ = std::max(probe_max_, count);
}
#endif
template <class Matches>
UniqueTable::ProbeResult UniqueTable::ProbeMatching(std::uint64_t hash,
                                                    Matches matches) noexcept {
  if (slots_.empty()) {
#ifdef DANDELION_NODE_TESTING
    RecordProbe(1);
#endif
    return {nullptr, 0};
  }
  const auto mask = slots_.size() - 1;
  const auto home = static_cast<std::size_t>(hash) & mask;
  auto slot = home;
  while (slots_[slot]) {
    auto* node = slots_[slot];
    if (node->structural_hash == hash && matches(node)) {
#ifdef DANDELION_NODE_TESTING
      RecordProbe(((slot - home) & mask) + 1);
#endif
      return {node, slot};
    }
    slot = (slot + 1) & mask;
  }
#ifdef DANDELION_NODE_TESTING
  RecordProbe(((slot - home) & mask) + 1);
#endif
  return {nullptr, slot};
}
template <class Child>
UniqueTable::ProbeResult UniqueTable::Probe(const QueryKey<Child>& key,
                                            std::uint64_t hash) noexcept {
  return ProbeMatching(hash,
                       [&](const auto* node) { return EqualKey(node, key); });
}
template <class Child>
UniqueTable::ProbeResult UniqueTable::ProbePlain(Kind kind,
                                                 std::span<Child> children,
                                                 std::uint64_t hash) noexcept {
  return ProbeMatching(hash, [&](const auto* node) {
    return node->GetKind() == kind && node->payload_tag == PayloadTag::kNone &&
           node->ChildCount() == children.size() &&
           EqualChildren(node, children);
  });
}
bool UniqueTable::EnsureSpace(ManagerState* state) {
  const auto capacity = slots_.size();
  if (size_ < capacity - capacity / 4) return false;
  if (capacity > slots_.max_size() / 2)
    Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
  const auto next = capacity ? capacity * 2 : kInitialCapacity;
  if (next > TableCapacity(state))
    Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
  Rehash(state, next);
  return true;
}
void UniqueTable::ReleaseEmpty() noexcept {
  if (size_ == 0 && slots_.size() > kInitialCapacity)
    std::vector<NodeData*>{}.swap(slots_);
}
void UniqueTable::Compact(ManagerState* state) {
  if (size_ == 0) {
    ReleaseEmpty();
    return;
  }
  const auto next = CompactedCapacity(size_);
  if (next < slots_.size()) Rehash(state, next);
}
std::size_t UniqueTable::CompactedCapacity(
    std::size_t survivors) const noexcept {
  auto capacity = slots_.size();
  while (capacity > kInitialCapacity && survivors <= capacity / 4)
    capacity /= 2;
  return capacity;
}
std::vector<NodeData*> UniqueTable::Rehash(ManagerState* state,
                                           std::size_t capacity,
                                           bool skip_retired) {
  AllocationCheckpoint(state);
  std::vector<NodeData*> fresh(capacity, nullptr);
  const auto mask = fresh.size() - 1;
  for (auto* node : slots_) {
    if (!node || (skip_retired && node->IsRetiring())) continue;
    auto slot = static_cast<std::size_t>(node->structural_hash) & mask;
    while (fresh[slot]) slot = (slot + 1) & mask;
    fresh[slot] = node;
  }
  peak_capacity_ =
      std::max(peak_capacity_, slots_.capacity() + fresh.capacity());
  slots_.swap(fresh);
  return fresh;
}
std::size_t UniqueTable::FindVacant(std::uint64_t hash) noexcept {
#ifdef DANDELION_NODE_TESTING
  ++insertion_searches_;
#endif
  const auto mask = slots_.size() - 1;
  auto slot = static_cast<std::size_t>(hash) & mask;
  while (slots_[slot]) slot = (slot + 1) & mask;
  return slot;
}
void UniqueTable::Insert(std::size_t slot, NodeData* node) noexcept {
  slots_[slot] = node;
  ++size_;
}
void UniqueTable::Erase(NodeData* node) noexcept {
  const auto mask = slots_.size() - 1;
  auto hole = static_cast<std::size_t>(node->structural_hash) & mask;
  while (slots_[hole] != node) {
    if (!slots_[hole]) std::terminate();
    hole = (hole + 1) & mask;
  }
  auto current = (hole + 1) & mask;
  while (auto* entry = slots_[current]) {
    const auto home = static_cast<std::size_t>(entry->structural_hash) & mask;
    if (((hole - home) & mask) < ((current - home) & mask)) {
      slots_[hole] = entry;
      hole = current;
    }
    current = (current + 1) & mask;
  }
  slots_[hole] = nullptr;
  --size_;
}
std::vector<NodeData*> UniqueTable::EraseRetired(ManagerState* state,
                                                 NodeData* retired,
                                                 std::size_t count) noexcept {
  const auto survivors = size_ - count;
  if (survivors == 0) {
    size_ = 0;
    if (slots_.size() > kInitialCapacity) return std::exchange(slots_, {});
    std::ranges::fill(slots_, nullptr);
    return {};
  }
  const auto capacity = CompactedCapacity(survivors);
  // A rebuild is worthwhile when a substantial part of the table died and
  // the survivors would fit in a smaller table.
  if (capacity < slots_.size() && count >= slots_.size() / 8 &&
      count >= survivors / 2) {
    try {
      auto old_slots = Rehash(state, capacity, true);
      size_ = survivors;
      return old_slots;
    } catch (const std::bad_alloc&) {
      // The existing table is still intact. Continue without allocating.
    }
  }
  for (auto* node = retired; node; node = node->reclaim_next) Erase(node);
  return {};
}
void CheckNodeIdentity(ManagerState* state) {
  if (state->node_ids_exhausted)
    Fail(ErrorCode::kResourceLimit, ResourceKind::kIdentitySpace);
}
template <class Child>
Candidate PrepareNode(ManagerState* state, const QueryKey<Child>& key,
                      SortId result_sort, std::uint64_t hash) {
  const bool wide_literal = IsWideLiteral(key.tag);
  std::uint32_t first_limbs = 0;
  std::uint32_t second_limbs = 0;
  if (wide_literal) {
    first_limbs = LimbCount(key.integer);
  } else if (IsWideRational(key.tag)) {
    first_limbs = LimbCount(key.rational->Numerator());
    second_limbs = LimbCount(key.rational->Denominator());
  }
  const auto count = key.children.size();
  const auto [size, child_offset, payload_offset] =
      CheckedLayout(count, key.tag, std::uint64_t{first_limbs} + second_limbs,
                    SizeCapacity(state));
  const auto inline_count = static_cast<std::uint16_t>(
      std::min<std::uint64_t>(count, NodeData::kExternalChildCount));
  AllocationCheckpoint(state);
  void* raw = ::operator new(size);
  auto* storage = ::new (raw) std::byte[size];
  auto* node = std::construct_at(
      reinterpret_cast<NodeData*>(storage),
      NodeData{.state = state,
               .storage = storage,
               .id = 0,
               .ref_count = 0,
               .structural_hash = hash,
               .sort_id = result_sort,
               .inline_child_count = inline_count,
               .kind_code = static_cast<std::uint8_t>(key.kind),
               .payload_tag = key.tag,
               .literal_limb_count = wide_literal ? first_limbs : 0});
  if (inline_count == NodeData::kExternalChildCount) {
    std::construct_at(
        reinterpret_cast<std::uint64_t*>(storage + sizeof(NodeData)),
        static_cast<std::uint64_t>(count));
  }
#ifdef DANDELION_NODE_TESTING
  ++state->allocations;
#endif
  auto* children =
      count ? ::new (storage + child_offset) NodeData*[count] : nullptr;
  bool payload_ready = false;
  bool state_held = false;
  std::size_t retained = 0;
  try {
    ConstructPayload(state, storage + payload_offset, key, first_limbs,
                     second_limbs);
    payload_ready = true;
    RetainState(state);
    state_held = true;
    if constexpr (!std::is_same_v<Child, Node>) {
      for (const auto& view : key.children) {
        auto* child = Access::Data(view);
        RetainNodeData(child);
        children[retained++] = child;
      }
    }
  } catch (...) {
    // Only the retained prefix has initialized pointers.
    for (std::size_t i = 0; i < retained; ++i) ReleaseNodeData(children[i]);
    if (payload_ready) DestroyPayload(node);
#ifdef DANDELION_NODE_TESTING
    ++state->candidate_frees;
#endif
    FreeStorage(node);
    if (state_held) ReleaseState(state);
    throw;
  }
  if constexpr (std::is_same_v<Child, Node>) {
    // Payload views may borrow from an input child. Copying the payload and
    // retaining the state must finish before the first ownership transfer.
    for (auto& child : key.children)
      children[retained++] = Access::TakeNode(child);
  }
  node->ref_count = 1;
  return Candidate(node);
}
void CandidateDeleter::operator()(NodeData* node) const noexcept {
  auto* state = node->state;
  for (auto* child : Children(node)) ReleaseNodeData(child);
  DestroyPayload(node);
#ifdef DANDELION_NODE_TESTING
  ++state->candidate_frees;
#endif
  FreeStorage(node);
  ReleaseState(state);
}
Node PublishNode(ManagerState* state, std::size_t slot,
                 Candidate candidate) noexcept {
  auto* node = candidate.release();
  node->id = state->next_node_id;
  if (state->next_node_id == std::numeric_limits<NodeId>::max())
    state->node_ids_exhausted = true;
  else
    ++state->next_node_id;
  state->table.Insert(slot, node);
  return Access::OwnNode(node);
}
namespace {
std::uint64_t ProbeHash([[maybe_unused]] const ManagerState* state,
                        std::uint64_t hash) noexcept {
#ifdef DANDELION_NODE_TESTING
  if (state->collision_hash) return *state->collision_hash;
#endif
  return hash;
}
template <class Child>
Node PublishMiss(ManagerState* state, const QueryKey<Child>& key,
                 std::uint64_t hash, std::size_t slot, SortId result_sort) {
  CheckNodeIdentity(state);
  if (state->table.EnsureSpace(state)) slot = state->table.FindVacant(hash);
  return PublishNode(state, slot, PrepareNode(state, key, result_sort, hash));
}
}  // namespace

template <class Child>
Node InternNode(ManagerState* state, const QueryKey<Child>& key,
                SortId established_sort) {
  const auto hash = ProbeHash(state, HashKey(state, key));
  auto [found, slot] = state->table.Probe(key, hash);
  if (found) {
    RetainNodeData(found);
    return Access::OwnNode(found);
  }
  const auto result_sort =
      established_sort ? established_sort : CompleteKeySort(state, key);
  return PublishMiss(state, key, hash, slot, result_sort);
}
template <class Child>
Node InternPlainBuiltin(ManagerState* state, Kind kind,
                        std::span<Child> children) {
  const auto hash = ProbeHash(state, Mix(HashShape(state, kind, 0, children)));
  auto [found, slot] = state->table.ProbePlain(kind, children, hash);
  if (found) {
    RetainNodeData(found);
    return Access::OwnNode(found);
  }
  CheckPlainBuiltinShape(state, kind, children.size());
  const QueryKey<Child> key{kind, 0, children};
  return PublishMiss(state, key, hash, slot, CompleteKeySort(state, key));
}

template std::uint64_t HashKey(ManagerState*, const QueryKey<>&) noexcept;
template Candidate PrepareNode(ManagerState*, const QueryKey<>&, SortId,
                               std::uint64_t);
template Node InternNode(ManagerState*, const QueryKey<>&, SortId);
template Node InternNode(ManagerState*, const QueryKey<Node>&, SortId);
template Node InternPlainBuiltin(ManagerState*, Kind,
                                 std::span<const NodeView>);
template Node InternPlainBuiltin(ManagerState*, Kind, std::span<Node>);

void RetainNodeData(NodeData* node) {
  RetainCheckpoint(node->state);
  if (node->ref_count >= ReferenceCapacity(node->state)) throw ReferenceLimit{};
  ++node->ref_count;
}
void ReleaseNodeData(NodeData* node) noexcept {
  if (node->ref_count == 0) std::terminate();
  if (--node->ref_count) return;
  auto* state = node->state;
  state->table.Erase(node);
  node->reclaim_next = nullptr;
  NodeData* work = node;
  std::uint64_t retired = 0;
  while (work) {
    auto* current = work;
    work = work->reclaim_next;
    for (auto* child : Children(current)) {
      if (child->ref_count == 0) std::terminate();
      if (--child->ref_count == 0) {
        state->table.Erase(child);
        child->reclaim_next = work;
        work = child;
      }
    }
    DestroyPayload(current);
    FreeStorage(current);
    ++retired;
#ifdef DANDELION_NODE_TESTING
    ++state->reclamations;
#endif
  }
  if (retired > state->references) std::terminate();
  state->table.ReleaseEmpty();
  state->references -= retired;
  if (state->references == 0) delete state;
}

namespace {
template <class ForEachRoot>
void ReleaseRootBatchImpl(ManagerState* state,
                          ForEachRoot for_each_root) noexcept {
  NodeData* pending = nullptr;
  const auto drop = [&](NodeData* node) noexcept {
    if (node->ref_count == 0) std::terminate();
    if (--node->ref_count != 0) return;
    node->kind_code |= NodeData::kRetiring;
    node->reclaim_next = pending;
    pending = node;
  };
  for_each_root([&](Node& root) noexcept {
    if (auto* node = Access::TakeNode(root)) drop(node);
  });
  if (!pending) return;

  NodeData* retired = nullptr;
  std::size_t count = 0;
  while (pending) {
    auto* node = pending;
    pending = node->reclaim_next;
    for (auto* child : Children(node)) drop(child);
    node->reclaim_next = retired;
    retired = node;
    ++count;
  }
  // The old index is detached. Release its buffer after freeing the nodes so
  // the allocator can consolidate their adjacent free blocks first.
  const auto old_slots = state->table.EraseRetired(state, retired, count);
  while (retired) {
    auto* node = retired;
    retired = node->reclaim_next;
    DestroyPayload(node);
    FreeStorage(node);
  }
  state->references -= count;
#ifdef DANDELION_NODE_TESTING
  state->reclamations += count;
#endif
}
}  // namespace

void ReleaseRootBatch(ManagerState* state, std::span<Node> roots) noexcept {
  ReleaseRootBatchImpl(state, [&](auto consume) noexcept {
    for (auto& root : roots) consume(root);
  });
}

void ReleaseRootBatch(ManagerState* state,
                      std::span<const RootRange> ranges) noexcept {
  ReleaseRootBatchImpl(state, [&](auto consume) noexcept {
    for (const auto& range : ranges) {
      for (std::size_t i = 0; i < Access::RootCount(range); ++i)
        consume(Access::RootAt(range, i));
    }
  });
}
}  // namespace dandelion::detail
