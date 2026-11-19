module dandelion.frontend:internal;
import dandelion.frontend;
import std;

namespace dandelion::frontend::detail {

// NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
struct Failure {
  Diagnostic error;
};

[[noreturn]] inline void Fail(DiagnosticCode code, SourceSpan where,
                              std::string_view message) {
  throw Failure{{code, where, message}};
}
[[noreturn]] void NodeFailure(NodeError error, SourceSpan where);
template <class T>
T Take(Result<T> result, SourceSpan where) {
  if (!result) NodeFailure(result.error(), where);
  if constexpr (!std::is_void_v<T>) return std::move(*result);
}

template <class Function>
auto Guard(SourceSpan where, Function&& function)
    -> Outcome<std::invoke_result_t<Function>> {
  try {
    if constexpr (std::is_void_v<std::invoke_result_t<Function>>) {
      function();
      return {};
    } else {
      return function();
    }
  } catch (const Failure& failure) {
    return std::unexpected(failure.error);
  } catch (const ReferenceLimit&) {
    return std::unexpected(Diagnostic{
        DiagnosticCode::kResource, where, "Reference count exhausted",
        NodeError{ErrorCode::kResourceLimit, ResourceKind::kReferenceCount}});
  } catch (const std::bad_alloc&) {
    return std::unexpected(Diagnostic{
        DiagnosticCode::kResource, where, "Memory allocation failed",
        NodeError{ErrorCode::kResourceLimit, ResourceKind::kCppAllocation}});
  } catch (const std::length_error&) {
    return std::unexpected(Diagnostic{
        DiagnosticCode::kResource, where, "Representation limit exceeded",
        NodeError{ErrorCode::kResourceLimit, ResourceKind::kSize}});
  }
}

struct TextHash {
  using is_transparent = void;  // NOLINT(readability-identifier-naming)
  std::size_t operator()(std::string_view text) const noexcept {
    return std::hash<std::string_view>{}(text);
  }
};
template <class T>
using TextMap = std::unordered_map<std::string, T, TextHash, std::equal_to<>>;
using TextSet = std::unordered_set<std::string, TextHash, std::equal_to<>>;

enum class TokenKind : std::uint8_t {
  kLeft,
  kRight,
  kSymbol,
  kQuoted,
  kReserved,
  kKeyword,
  kNumeral,
  kDecimal,
  kBinary,
  kHexadecimal,
  kString,
};
using ExprId = std::size_t;
constexpr auto kNoExpr = std::numeric_limits<ExprId>::max();

using SpellingId = std::uint16_t;
constexpr SpellingId kUnknownSpelling = 0;
using SyntaxWord = std::uint64_t;

// The outer directory indexes blocks of pages. Growing it only moves pointers.
// Compact rows are promoted independently and keep their logical index.
template <std::size_t Fields, std::size_t Metadata = 0, bool Line = false>
class SyntaxPages {
 public:
  using Record = std::array<SyntaxWord, Fields>;
  using Compact = std::array<std::uint32_t, Line ? 1 : Fields>;
  static constexpr std::size_t kPrefixSize = 32;
  static constexpr std::size_t kPageSize = 512;
  template <class Row>
  using Rows = std::array<Row, kPageSize>;
  struct Pool {
    std::array<std::unique_ptr<Rows<Compact>>, 2> compact;
    std::unique_ptr<Rows<Record>> wide;
  };

  SyntaxPages(Pool& pool, SyntaxWord threshold, FrontendStats* stats) noexcept
      : pool_(pool), threshold_(threshold), stats_(stats) {}
  ~SyntaxPages() {
    Return(prefix_page_);
    for (auto& directory : directory_)
      for (auto& page : *directory) Return(page);
  }
  SyntaxPages(const SyntaxPages&) = delete;
  SyntaxPages& operator=(const SyntaxPages&) = delete;

  std::size_t Size() const noexcept { return size_; }
  Record Get(std::size_t index) const noexcept {
    const auto [page, slot] = Locate(index);
    if (index < kPrefixSize)
      return prefix_page_.wide ? (*prefix_page_.wide)[slot]
                               : Decode(prefix_[slot]);
    const auto& rows = (*directory_[page / kFanout])[page % kFanout];
    return rows.wide ? (*rows.wide)[slot] : Decode((*rows.compact)[slot]);
  }
  void Append(Record value) {
    if (size_ == std::numeric_limits<std::size_t>::max())
      throw std::length_error("Syntax record count");
    Set(size_, value);
    ++size_;
  }
  void Set(std::size_t index, Record value) {
    const auto [page_index, slot] = Locate(index);
    const bool inline_page = index < kPrefixSize;
    auto& page = inline_page ? prefix_page_ : EnsurePage(page_index);
    const bool fits = Fits(value);
    if (!fits && !page.wide) {
      auto wide = AcquireWide();
      const auto* compact = inline_page    ? prefix_.data()
                            : page.compact ? page.compact->data()
                                           : nullptr;
      const auto page_start = index - slot;
      const auto used =
          std::min(inline_page ? kPrefixSize : kPageSize, size_ - page_start);
      if (used != 0) {
        for (std::size_t i = 0; i < used; ++i) (*wide)[i] = Decode(compact[i]);
        Count(&FrontendStats::syntax_page_copies);
      }
      page.wide = std::move(wide);
      ReturnCompact(page);
      Count(&FrontendStats::syntax_promoted_pages);
    }
    if (page.wide) {
      (*page.wide)[slot] = value;
    } else if (inline_page) {
      prefix_[slot] = Encode(value);
    } else {
      if (!page.compact) page.compact = AcquireCompact();
      (*page.compact)[slot] = Encode(value);
    }
  }

 private:
  static constexpr std::size_t kFanout = 64;
  struct Page {
    std::unique_ptr<Rows<Compact>> compact;
    std::unique_ptr<Rows<Record>> wide;
  };
  static std::pair<std::size_t, std::size_t> Locate(
      std::size_t index) noexcept {
    if (index < kPrefixSize) return {0, index};
    index -= kPrefixSize;
    return {index / kPageSize, index % kPageSize};
  }
  Page& EnsurePage(std::size_t index) {
    const auto block = index / kFanout;
    if (block == directory_.size())
      directory_.push_back(std::make_unique<std::array<Page, kFanout>>());
    return (*directory_[block])[index % kFanout];
  }
  bool Fits(const Record& row) const noexcept {
    constexpr auto limit = std::numeric_limits<std::uint32_t>::max();
    if constexpr (Line)
      return row[0] <= std::min<SyntaxWord>(limit >> 1, threshold_);
    for (std::size_t i = 0; i < Fields - Metadata; ++i)
      if (row[i] > limit || row[i] > threshold_) return false;
    return true;
  }
  static Compact Encode(const Record& row) noexcept {
    if constexpr (Line) {
      return {static_cast<std::uint32_t>((row[0] << 1) | row[1])};
    } else {
      Compact compact;
      for (std::size_t i = 0; i < Fields; ++i)
        compact[i] = static_cast<std::uint32_t>(row[i]);
      return compact;
    }
  }
  static Record Decode(const Compact& row) noexcept {
    if constexpr (Line) {
      return {row[0] >> 1, row[0] & 1};
    } else {
      Record wide;
      for (std::size_t i = 0; i < Fields; ++i) wide[i] = row[i];
      return wide;
    }
  }
  void Count(std::size_t FrontendStats::* member) noexcept {
    if (stats_) ++(stats_->*member);
  }
  std::unique_ptr<Rows<Compact>> AcquireCompact() {
    for (auto& page : pool_.compact)
      if (page) return std::move(page);
    Count(&FrontendStats::syntax_page_allocations);
    return std::make_unique_for_overwrite<Rows<Compact>>();
  }
  std::unique_ptr<Rows<Record>> AcquireWide() {
    if (pool_.wide) return std::move(pool_.wide);
    Count(&FrontendStats::syntax_page_allocations);
    return std::make_unique_for_overwrite<Rows<Record>>();
  }
  void ReturnCompact(Page& page) noexcept {
    if (!page.compact) return;
    for (auto& spare : pool_.compact) {
      if (spare) continue;
      spare = std::move(page.compact);
      return;
    }
    page.compact.reset();
  }
  void Return(Page& page) noexcept {
    ReturnCompact(page);
    if (page.wide && !pool_.wide) pool_.wide = std::move(page.wide);
  }

  Pool& pool_;
  SyntaxWord threshold_;
  FrontendStats* stats_;
  std::size_t size_ = 0;
  std::array<Compact, kPrefixSize> prefix_;
  Page prefix_page_;
  std::vector<std::unique_ptr<std::array<Page, kFanout>>> directory_;
};

using TokenPages = SyntaxPages<4, 1>;
using ExpressionPages = SyntaxPages<4>;
using EdgePages = SyntaxPages<1>;
using LinePages = SyntaxPages<2, 1, true>;
static_assert(sizeof(TokenPages::Compact) == 16);
static_assert(sizeof(ExpressionPages::Compact) == 16);
static_assert(sizeof(EdgePages::Compact) == 4);
static_assert(sizeof(LinePages::Compact) == 4);

class ChildRange {
 public:
  class Iterator {
   public:
    // NOLINTBEGIN(readability-identifier-naming): Standard iterator interface.
    using value_type = ExprId;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::bidirectional_iterator_tag;
    // NOLINTEND(readability-identifier-naming)
    Iterator() noexcept = default;
    ExprId operator*() const noexcept {
      return static_cast<ExprId>(edges_->Get(index_)[0]);
    }
    Iterator& operator++() noexcept {
      ++index_;
      return *this;
    }
    Iterator operator++(int) noexcept {
      auto previous = *this;
      ++*this;
      return previous;
    }
    Iterator& operator--() noexcept {
      --index_;
      return *this;
    }
    Iterator operator--(int) noexcept {
      auto previous = *this;
      --*this;
      return previous;
    }
    friend bool operator==(Iterator, Iterator) = default;

   private:
    friend class ChildRange;
    Iterator(const EdgePages* edges, std::size_t index) noexcept
        : edges_(edges), index_(index) {}
    const EdgePages* edges_ = nullptr;
    std::size_t index_ = 0;
  };

  ChildRange() noexcept = default;
  ChildRange(const EdgePages& edges, std::size_t first,
             std::size_t count) noexcept
      : edges_(&edges), first_(first), count_(count) {}
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  std::size_t size() const noexcept { return count_; }
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  bool empty() const noexcept { return count_ == 0; }
  ExprId operator[](std::size_t index) const noexcept {
    return *Iterator(edges_, first_ + index);
  }
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  ExprId front() const noexcept { return (*this)[0]; }
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  ExprId back() const noexcept { return (*this)[count_ - 1]; }
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  Iterator begin() const noexcept { return Iterator(edges_, first_); }
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  Iterator end() const noexcept { return Iterator(edges_, first_ + count_); }
  // NOLINTNEXTLINE(readability-identifier-naming): Span-style accessor.
  ChildRange subspan(std::size_t start) const noexcept {
    return ChildRange(edges_, first_ + start, count_ - start);
  }

 private:
  ChildRange(const EdgePages* edges, std::size_t first,
             std::size_t count) noexcept
      : edges_(edges), first_(first), count_(count) {}
  const EdgePages* edges_ = nullptr;
  std::size_t first_ = 0;
  std::size_t count_ = 0;
};

struct SyntaxPool {
  TokenPages::Pool tokens;
  ExpressionPages::Pool expressions;
  EdgePages::Pool edges;
  LinePages::Pool lines;
  std::array<std::unique_ptr<char[]>, 2> slabs;
  FrontendStats* stats = nullptr;
  SyntaxWord threshold = std::numeric_limits<std::uint32_t>::max();
};

struct Token {
  SyntaxWord offset;
  SyntaxWord storage;
  SyntaxWord length;
  TokenKind kind;
  SpellingId spelling_id = kUnknownSpelling;
};
struct Expression {
  std::size_t token;
  std::size_t last_token;
  std::size_t first_child = 0;
  std::size_t child_count = 0;
};
struct Replacement {
  std::size_t last_token;
  std::string spelling;
};

class Syntax {
 public:
  Syntax(SyntaxPool& pool, std::string_view bytes, SourceSpan start) noexcept;
  ~Syntax();
  Syntax(const Syntax&) = delete;
  Syntax& operator=(const Syntax&) = delete;

  // NOLINTNEXTLINE(readability-identifier-naming): Struct-style command record.
  CommandHeader header = {};
  std::string_view RawToken(std::size_t i) const noexcept;
  std::string_view Raw(ExprId id) const noexcept {
    return RawToken(GetExpression(id).token);
  }
  TokenKind Type(ExprId id) const noexcept {
    return GetToken(GetExpression(id).token).kind;
  }
  SpellingId Spelling(ExprId id) const noexcept {
    return GetToken(GetExpression(id).token).spelling_id;
  }
  std::string_view Name(ExprId id) const noexcept;
  SourceSpan Span(ExprId id) const noexcept;
  ChildRange Children(ExprId id) const noexcept;
  bool Is(ExprId id, std::string_view unquoted) const noexcept;
  bool IsSymbol(ExprId id) const noexcept;
  bool List(ExprId id) const noexcept { return Type(id) == TokenKind::kLeft; }
  std::string Render(ExprId id,
                     const std::unordered_map<std::size_t, Replacement>*
                         replacements = nullptr) const;
  Expression GetExpression(ExprId id) const noexcept;

 private:
  friend class Scanner;
  static constexpr std::size_t kSlabSize = 16 * 1024;
  struct Slab {
    std::unique_ptr<char[]> small;
    std::string large;
  };
  Token GetToken(std::size_t index) const noexcept;
  void AddToken(Token token);
  SyntaxWord Store(std::string_view bytes, std::string& scratch);
  std::string_view Stored(SyntaxWord start, SyntaxWord length) const noexcept;
  SourcePosition Position(SyntaxWord offset) const noexcept;
  void LineBreak(SyntaxWord offset, bool continuation);

  SyntaxPool& pool_;
  std::string_view bytes_;
  SourceSpan start_;
  TokenPages tokens_;
  ExpressionPages expressions_;
  EdgePages edges_;
  LinePages lines_;
  std::array<char, 256> inline_bytes_;
  std::size_t inline_used_ = 0;
  std::vector<Slab> slabs_;
  std::size_t slab_used_ = kSlabSize;
};

struct Operator;
struct Identifier {
  ExprId name;
  ChildRange indices;
  ExprId qualifier = kNoExpr;
};
Identifier GetIdentifier(const Syntax& syntax, ExprId id);
// Descriptors in a reserved token are in the grammar's namespace.
SpellingId ClassifyReserved(std::string_view word) noexcept;
bool IsReservedWord(std::string_view word) noexcept;

template <class Catalog, class Spelling>
constexpr auto SpellingBuckets(const Catalog& entries, Spelling spelling) {
  // Bucket by length and first ASCII byte, keeping compact linked ordinals.
  struct Table {
    std::array<SpellingId, 32 * 128> heads{};
    std::array<SpellingId, std::tuple_size_v<Catalog>> next{};
  };
  Table table;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto word = spelling(entries[i]);
    auto& head = table.heads[word.size() * 128 +
                             static_cast<unsigned char>(word.front())];
    table.next[i] = std::exchange(head, static_cast<SpellingId>(i + 1));
  }
  return table;
}
template <class Catalog, class Spelling, class Table>
SpellingId LookupSpelling(const Catalog& entries, Spelling spelling,
                          const Table& table, std::string_view word) noexcept {
  if (word.empty() || word.size() >= 32 ||
      static_cast<unsigned char>(word.front()) >= 128)
    return kUnknownSpelling;
  auto id =
      table.heads[word.size() * 128 + static_cast<unsigned char>(word.front())];
  while (id && spelling(entries[id - 1]) != word) id = table.next[id - 1];
  return id;
}
SpellingId ClassifyOperator(std::string_view word) noexcept;
const Operator* GetOperator(SpellingId id) noexcept;
bool IsSimpleSymbol(std::string_view word) noexcept;
std::size_t Utf8Size(int first) noexcept;
bool IsUtf8Continuation(int byte) noexcept;
bool IsUtf8Scalar(unsigned scalar, std::size_t size) noexcept;
bool IsQuotedByte(int byte) noexcept;

class Scanner {
 public:
  Scanner(std::string_view input, std::uint32_t source);
  Scanner(Reader reader, std::uint32_t source);
  SourceSpan Here() const noexcept { return {source_, position_, position_}; }
  void Next(std::optional<Syntax>& pending);
  SyntaxPool& Pool() noexcept { return pool_; }

 private:
  static constexpr std::size_t kReaderCapacity = 64 * 1024;
  int Peek();
  void TakeByte(int byte, Syntax* syntax);
  void Advance(std::size_t count);
  void Trivia(Syntax* syntax = nullptr);
  std::string_view TokenBytes();
  void SaveTokenPart();
  void Scalar(int first, Syntax* syntax);
  Token ScanToken(Syntax& syntax, int first);
  void ReadCommand(std::optional<Syntax>& pending);

  std::string_view text_;
  Reader reader_;
  std::unique_ptr<std::array<char, kReaderCapacity>> buffer_;
  std::string_view window_;
  std::size_t cursor_ = 0;
  SourcePosition position_;
  std::uint32_t source_;
  bool after_cr_ = false;
  bool eof_ = false;
  SyntaxPool pool_;
  std::string scratch_;
  std::optional<std::size_t> token_start_;
};
void ValidateSyntax(Syntax& syntax);

enum class OptionKind { kBoolean, kNumeral, kString };
enum class OptionId {
  kPrintSuccess,
  kProduceModels,
  kGlobalDeclarations,
  kFixedFalse,
  kUnsupported,
};
struct Option {
  std::string_view name;
  OptionId id;
  OptionKind kind;
  std::string_view default_value;
};
const Option* FindOption(std::string_view name) noexcept;

enum Feature : unsigned {
  kArrays = 1 << 0,
  kBitVectors = 1 << 1,
  kDatatypes = 1 << 2,
  kFloats = 1 << 3,
  kIntegers = 1 << 4,
  kRestrictedIntegers = 1 << 5,
  kReals = 1 << 6,
  kRealInt = 1 << 7,
};
enum class DeclarationPermission { kConstants, kSorts, kFunctions };
struct Profile {
  std::string_view name;
  unsigned features;
  DeclarationPermission declarations;
  bool Has(unsigned mask) const noexcept { return (features & mask) == mask; }
};
const Profile* FindProfile(std::string_view name) noexcept;

struct SortRecipe;
struct Alias {
  std::uint64_t arity;
  const SortRecipe* root;
};
struct DatatypeSort {
  std::size_t group;
  std::uint64_t slot;
  std::uint64_t arity;
};
struct DatatypeMember {
  std::size_t group;
  std::uint64_t datatype;
  std::uint64_t ctor;
  std::optional<std::uint64_t> field;
};
using SortBinding = std::variant<SortConstructorView, Alias, DatatypeSort>;
using TermBinding = std::variant<DeclInstanceView, DatatypeMember, std::size_t>;
enum class RecipeKind { kConcrete, kParameter, kArray, kApply };
struct SortRecipe {
  RecipeKind kind;
  SortView concrete;
  SortConstructorView constructor;
  std::uint64_t param;
  std::vector<const SortRecipe*> children;
  // Constructor nesting with parameters and concrete builtin sorts as leaves.
  std::size_t height = 0;
  friend bool operator==(const SortRecipe&, const SortRecipe&) = default;
};
struct RecipeHash {
  std::size_t operator()(const SortRecipe& recipe) const noexcept;
};

// The probe table stores one-based ordinals. Records are append-only until
// rollback, which removes insertions in reverse order.
template <class T>
class NameTable {
 public:
  using Entry = std::pair<std::string, T>;
  const Entry* find(std::string_view name) const
      noexcept {  // NOLINT(readability-identifier-naming): Container interface.
    if (compact_.empty() && wide_.empty()) return nullptr;
    const auto hash = Hash(name);
    return wide_.empty() ? Find(compact_, name, hash) : Find(wide_, name, hash);
  }
  bool contains(std::string_view name) const noexcept {
    return find(name) != nullptr;
  }  // NOLINT(readability-identifier-naming): Container interface.
  void Insert(std::string_view name, T value) {
    const auto size = entries_.size();
    const auto capacity = wide_.empty() ? compact_.size() : wide_.size();
    if (size >= capacity - capacity / 4) {
      if (capacity > std::numeric_limits<std::size_t>::max() / 2)
        throw std::length_error("Name table capacity");
      const auto next = std::max(std::size_t{8}, capacity * 2);
      if (size >= std::numeric_limits<std::uint32_t>::max() || !wide_.empty()) {
        Rehash(wide_, next);
        compact_.clear();
      } else {
        Rehash(compact_, next);
      }
    }
    entries_.emplace_back(name, std::move(value));
    const auto hash = Hash(name);
    if (wide_.empty())
      Place(compact_, hash, size + 1);
    else
      Place(wide_, hash, size + 1);
  }
  void EraseLast(std::string_view name) noexcept {
    const auto hash = Hash(name);
    if (wide_.empty())
      Erase(compact_, hash);
    else
      Erase(wide_, hash);
    entries_.pop_back();
  }
  const auto& Entries() const noexcept { return entries_; }

 private:
  template <class Ordinal>
  struct Slot {
    std::uint32_t hash;
    Ordinal ordinal;
  };
  using Compact = Slot<std::uint32_t>;
  using Wide = Slot<std::size_t>;
  static_assert(sizeof(Compact) == 8);

  static std::uint32_t Hash(std::string_view name) noexcept {
    const auto hash = static_cast<std::uint64_t>(TextHash{}(name));
    return static_cast<std::uint32_t>(hash ^ (hash >> 32));
  }
  template <class Ordinal>
  const Entry* Find(const std::vector<Slot<Ordinal>>& slots,
                    std::string_view name, std::uint32_t hash) const noexcept {
    const auto mask = slots.size() - 1;
    auto index = hash & mask;
    while (const auto ordinal = slots[index].ordinal) {
      const auto& entry = entries_[ordinal - 1];
      if (slots[index].hash == hash && entry.first == name) return &entry;
      index = (index + 1) & mask;
    }
    return nullptr;
  }
  template <class Ordinal>
  static void Place(std::vector<Slot<Ordinal>>& slots, std::uint32_t hash,
                    std::size_t ordinal) noexcept {
    const auto mask = slots.size() - 1;
    auto index = hash & mask;
    while (slots[index].ordinal) index = (index + 1) & mask;
    slots[index] = {hash, static_cast<Ordinal>(ordinal)};
  }
  template <class Ordinal>
  void Rehash(std::vector<Slot<Ordinal>>& slots, std::size_t capacity) {
    std::vector<Slot<Ordinal>> next(capacity);
    for (std::size_t i = 0; i < entries_.size(); ++i)
      Place(next, Hash(entries_[i].first), i + 1);
    slots.swap(next);
  }
  template <class Ordinal>
  void Erase(std::vector<Slot<Ordinal>>& slots, std::uint32_t hash) noexcept {
    const auto mask = slots.size() - 1;
    auto hole = hash & mask;
    while (slots[hole].ordinal != entries_.size()) hole = (hole + 1) & mask;
    for (auto next = (hole + 1) & mask; slots[next].ordinal;
         next = (next + 1) & mask) {
      const auto home = slots[next].hash & mask;
      if (((hole - home) & mask) < ((next - home) & mask)) {
        slots[hole] = slots[next];
        hole = next;
      }
    }
    slots[hole] = {};
  }
  std::deque<Entry> entries_;
  std::vector<Compact> compact_;
  std::vector<Wide> wide_;
};

using Rank = std::span<const std::string_view>;
struct RankHash {
  std::size_t operator()(Rank rank) const noexcept;
};
struct RankEqual {
  bool operator()(Rank left, Rank right) const noexcept {
    return std::ranges::equal(left, right);
  }
};
struct RankPool {
  static constexpr std::size_t kBlockSize = 1024;
  TextSet spellings;
  std::unordered_set<Rank, RankHash, RankEqual> ranks;
  std::vector<std::unique_ptr<std::string_view[]>> blocks;
  std::size_t remaining = 0;
};

struct Signature {
  static const Signature& Of(const QueryContext& query) noexcept {
    return *query.signature_;
  }
  const Profile* profile = nullptr;
  NameTable<SortBinding> sorts;
  NameTable<TermBinding> terms;
  std::unordered_set<SortRecipe, RecipeHash> recipes;
  std::unordered_map<SortId, std::string_view> sort_spellings;
  std::unordered_map<DeclId, std::size_t> definition_index;
  std::vector<std::pair<DeclId, std::size_t>> function_runs;
  RankPool rank_pool;
  static std::optional<std::size_t> FunctionOrdinal(const QueryContext& query,
                                                    DeclView decl) noexcept;
};

constexpr std::size_t kCacheSlots = 256;
struct LiteralCache {
  struct Entry {
    std::string digits;
    IntegerView value = IntegerView(0);
    std::uint64_t width = 0;
    int radix = 0;
  };
  std::array<Entry, kCacheSlots> entries;
  // Wide views borrow these exact integers. Word-sized values have no backing.
  std::array<Integer, kCacheSlots> backing;
  std::size_t key_bytes = 0;
  std::size_t eviction = 0;
};

struct SortKey {
  std::string_view name;
  std::array<std::uint64_t, 8> arguments{};
  std::size_t count = 0;
  bool indexed = false;
  friend bool operator==(const SortKey&, const SortKey&) = default;
  std::size_t Slot() const noexcept;
};
struct CachedSort {
  SortKey key;
  SortView sort;
};
using SortCache = std::array<CachedSort, kCacheSlots>;

struct SessionState {
  explicit SessionState(Scanner scanner, ParseOptions parse = {});
  SessionState(Scanner scanner, std::shared_ptr<ModelOwner> model);
  const QueryContext& Prefix() const noexcept;
  const Signature& GetSignature() const noexcept {
    return *Prefix().signature_;
  }
  void Count([[maybe_unused]] std::size_t FrontendStats::* member) noexcept {
#ifdef DANDELION_FRONTEND_TESTING
    ++(stats.*member);
#endif
  }
  void Checkpoint();
  Node NumericLiteral(std::string_view digits, int radix, SortView sort,
                      SourceSpan where);
  TypedCommand Apply(Syntax& syntax, const ModelOwner* model);
  void ApplySemantic(const Syntax& syntax, TypedCommand& command);

  Scanner scanner;
  std::optional<Syntax> pending;
  NodeManager manager;
  std::shared_ptr<QueryContext> building;
  std::shared_ptr<const QueryContext> sealed;
  std::shared_ptr<ModelOwner> inspection_model;
  std::vector<Attribute> metadata;
  SessionOptions options;
  ParseOptions parse_options;
  std::unique_ptr<LiteralCache> literal_cache;
  std::unique_ptr<SortCache> sort_cache;
  SessionPhase phase = SessionPhase::kBeforeLogic;
#ifdef DANDELION_FRONTEND_TESTING
  FrontendStats stats;
  std::optional<std::size_t> fail_after;
#endif
};
class Transaction {
 public:
  explicit Transaction(SessionState& state) noexcept;
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
  ~Transaction() noexcept;
  void Commit() noexcept { committed_ = true; }
  void AddTerm(std::string_view name, TermBinding binding);
  void AddSort(std::string_view name, SortBinding binding);
  void RememberAlias(SortView sort, std::string_view name);
  const SortRecipe* InternRecipe(SortRecipe recipe);
  void AddNamed(std::string_view name, Node term, SourceSpan where);
  void AddAnnotation(Annotation annotation);
  void AddAssertion(Assertion assertion);
  void AddUserSort(UserSort sort);
  void AddFunction(Decl decl, bool quoted, std::span<std::string> spelling,
                   SourceSpan where);
  void AddDefinition(DefinedFunction definition);
  std::size_t StageGroup();
  void FinishGroup(std::size_t index, DatatypeGroup group) noexcept;

 private:
  enum class NameSpace { kTerm, kSort };
  struct NameEntry {
    NameSpace space;
    std::string_view name;
  };
  SessionState& state_;
  QueryContext& query_;
  Signature& signature_;
  std::vector<NameEntry> names_;
  std::vector<const SortRecipe*> added_recipes_;
  std::vector<SortId> added_spellings_;
  const std::size_t named_;
  const std::size_t annotations_;
  const std::size_t assertions_;
  const std::size_t sorts_;
  const std::size_t functions_;
  const std::size_t function_runs_;
  const std::size_t rank_blocks_;
  const std::size_t rank_remaining_;
  std::vector<std::string_view> added_rank_strings_;
  Rank added_rank_;
  const std::size_t definitions_;
  const std::size_t groups_;
  bool committed_ = false;
};

bool IsReservedName(std::string_view name) noexcept;
bool IsActiveSort(const Profile& profile, std::string_view name) noexcept;
bool IsActiveTerm(const Profile& profile, std::string_view name) noexcept;
void CheckSortName(const SessionState& state, const Syntax& syntax, ExprId name,
                   bool global);
void CheckTermName(const SessionState& state, const Syntax& syntax, ExprId name,
                   bool global);
std::vector<Integer> NumericIndices(const Syntax& syntax, Identifier id,
                                    std::size_t expected, SourceSpan where);
std::uint64_t ParseArity(const Syntax& syntax, ExprId numeral);
void CheckMinimumIndex(const Syntax& syntax, Identifier id,
                       std::span<const Integer> indices, std::size_t index,
                       std::int64_t minimum);

using Parameters = TextMap<std::uint64_t>;
Sort ResolveSort(SessionState& session, const Syntax& syntax, ExprId root,
                 Transaction* txn);
Alias ResolveAlias(SessionState& session, Transaction& txn,
                   const Syntax& syntax, ExprId root,
                   const Parameters& parameters);
SortExpr ResolveField(SessionState& session, const Syntax& syntax, ExprId root,
                      const Parameters& parameters, DatatypeBuilder& builder,
                      std::size_t group, std::uint64_t slot);
void DeclareDatatypes(SessionState& session, Transaction& txn,
                      const Syntax& syntax);
struct Operator {
  std::string_view name;
  Kind kind;
  unsigned features;
  std::size_t indices;
  std::uint64_t min_arity;
  std::uint64_t max_arity = min_arity;
};
const Operator* FindOperator(std::string_view name, bool indexed) noexcept;
std::span<NodeView> CommutativeOperands(Kind kind,
                                        std::span<NodeView> operands) noexcept;
bool OrderCommutative(std::span<NodeView> operands) noexcept;
const Operator& OperatorFor(Kind kind) noexcept;
bool IsRestrictedInt(Kind kind) noexcept;
bool IsKnownExcludedTerm(std::string_view name) noexcept;
std::optional<RoundingMode> FindRoundingMode(std::string_view name) noexcept;
std::optional<FpSpecial> FindFpSpecial(std::string_view name) noexcept;
std::string_view RoundingSpelling(RoundingMode mode) noexcept;
std::string_view SpecialSpelling(FpSpecial special) noexcept;
void ApplyTerms(SessionState& state, const Syntax& syntax,
                TypedCommand& command, Transaction& txn);
void ApplyValues(SessionState& state, const Syntax& syntax,
                 TypedCommand& command, const ModelOwner* model);
std::pair<Node, std::string> ResolveModelReference(
    const ModelOwner& model, const QueryContext& query, std::string_view name,
    std::optional<SortView> qualifier, SourceSpan where);
std::optional<std::size_t> AbstractOrdinal(std::string_view name) noexcept;

struct ModelIdentity {};
struct ModelValue {
  std::shared_ptr<const ModelIdentity> owner;
  Sort sort;
  std::string key;
  std::shared_ptr<const void> payload;
};
struct AbstractKey {
  SortId sort;
  std::string_view key;
};
bool operator==(AbstractKey left, AbstractKey right) noexcept;
struct AbstractHash {
  bool constant = false;
  std::size_t operator()(AbstractKey key) const noexcept;
};
struct ExposedValue {
  Node proxy;
  ValueRef value;
  std::string spelling;
};
using AbstractNames =
    std::unordered_map<AbstractKey, std::size_t, AbstractHash>;
using ProxyBindings = std::map<DeclId, ValueRef>;
struct ModelState {
  ModelState(const std::shared_ptr<const QueryContext>& query,
             CheckAnswer answer, UnknownReason reason);
  Node RegisterProxy(const ValueRef& value, ProxyBindings& bindings,
                     SourceSpan where);
  // Keep the query alive through provider destruction.
  std::shared_ptr<const QueryContext> query;
  NodeManager manager;
  std::shared_ptr<const ModelIdentity> identity;
  CheckAnswer answer;
  UnknownReason reason;
  std::vector<ExposedValue> exposed;
  AbstractNames keys;
  ProxyBindings proxies;
  bool writing_response = false;
  std::unique_ptr<ModelProvider> provider;
};
bool IsAbstractSort(SortView sort) noexcept;
class ModelScope {
 public:
  explicit ModelScope(const QueryContext& query) noexcept;
  void CheckSort(SortView sort, SourceSpan where);
  void CheckNode(NodeView node, SourceSpan where);

 private:
  const QueryContext& query_;
  const Signature& signature_;
  std::unordered_set<SortId> seen_sorts_;
  std::vector<SortView> work_;
};
template <class Function>
auto InvokeProvider(Function&& function) -> std::invoke_result_t<Function> {
  try {
    return function();
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::length_error&) {
    throw;
  } catch (const ReferenceLimit&) {
    throw;
  } catch (...) {
    Fail(DiagnosticCode::kModelFailure, {},
         "Model provider raised an exception");
  }
}

template <class T>
T Use(Outcome<T> value, SourceSpan where) {
  if (!value) {
    auto error = value.error();
    if (error.where == SourceSpan{}) error.where = where;
    throw Failure{error};
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}

class Response {
 public:
  explicit Response(const ResponseSink& sink) noexcept : sink_(sink) {}
  void Write(std::string_view bytes, SourceSpan where);
  void Finish(SourceSpan where);
  bool Started() const noexcept { return started_; }

 private:
  const ResponseSink& sink_;
  bool started_ = false;
};
void WriteValues(ModelOwner& model, SessionState& session,
                 std::span<const ValueRequest> values, Response& response,
                 SourceSpan where);
void WriteModel(ModelOwner& model, SessionState& session, Response& response,
                SourceSpan where);
// NOLINTEND(readability-identifier-naming)

}  // namespace dandelion::frontend::detail
