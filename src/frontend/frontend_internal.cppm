module dandelion.frontend:internal;
import dandelion.frontend;
import dandelion.util.numeric;
import std;

namespace dandelion::frontend::detail {

// NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
struct Failure {
  Diagnostic error;
};

[[noreturn, gnu::cold]] void Fail(DiagnosticCode code, SourceSpan where,
                                  std::string_view message);
[[noreturn]] void OutputError(SourceSpan where, std::string_view message,
                              std::error_code error = {});
[[noreturn, gnu::cold]] void NodeFailure(NodeError error, SourceSpan where);
[[noreturn, gnu::cold]] void NumericFailure(numeric::NumericError error,
                                            SourceSpan where);

class ReadScope {
 public:
  explicit ReadScope(bool& in_read) noexcept : in_read_(in_read) {
    in_read_ = true;
  }
  ReadScope(const ReadScope&) = delete;
  ReadScope& operator=(const ReadScope&) = delete;
  ~ReadScope() { in_read_ = false; }

 private:
  bool& in_read_;
};

inline Diagnostic ReentrantRead(SourceSpan where) noexcept {
  return {DiagnosticCode::kInvalidState, where, "Input read is already active"};
}

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

enum class SpellingId : std::uint16_t {
  kUnknown,
  kAnnotation,
  kUnderscore,
  kAs,
  kExists,
  kForall,
  kLambda,
  kLet,
  kMatch,
  kPar,
  kOtherReserved,
  kIs,
  kTrue,
  kFalse,
  kBool,
  kInt,
  kArray,
  kBitVec,
  kFloatingPoint,
  kRoundingMode,
  kFloat16,
  kFloat32,
  kFloat64,
  kFloat128,
  kReal,
  kString,
  kRegLan,
  kFunctionSort,
  kRne,
  kRna,
  kRtp,
  kRtn,
  kRtz,
  kOperatorBase = 128,
  kCommandBase = 512,
};
constexpr auto kUnknownSpelling = SpellingId::kUnknown;
using SyntaxWord = std::uint64_t;

using ChildRange = std::span<const ExprId>;

struct Token {
  // NOLINTBEGIN(readability-identifier-naming): Lexical record fields.
  TokenKind kind;
  SpellingId spelling_id;
  std::string_view raw;
  SourceSpan where;
  // NOLINTEND(readability-identifier-naming)
};

struct Replacement {
  // NOLINTBEGIN(readability-identifier-naming): Lexical record fields.
  std::size_t last_token;
  std::string spelling;
  // NOLINTEND(readability-identifier-naming)
};

struct SortRecipe;

// Most structural records hold a command rank or a fixed special-form
// prefix. Only variable-length structural lists need overflow storage.
class SyntaxChildren {
 public:
  ChildRange Range() const noexcept {
    return overflow_.empty() ? ChildRange(inline_).first(size_)
                             : ChildRange(overflow_);
  }
  void Push(ExprId id) {
    if (size_ < inline_.size()) {
      inline_[size_] = id;
    } else {
      if (size_ == inline_.size())
        overflow_.assign(inline_.begin(), inline_.end());
      overflow_.push_back(id);
    }
    ++size_;
  }
  void Pop() noexcept {
    if (size_-- > inline_.size()) {
      overflow_.pop_back();
      if (size_ == inline_.size()) overflow_.clear();
    }
  }
  void Clear() noexcept {
    size_ = 0;
    overflow_.clear();
    if (overflow_.capacity() > 256) std::vector<ExprId>{}.swap(overflow_);
  }

 private:
  std::array<ExprId, 6> inline_;
  std::vector<ExprId> overflow_;
  std::size_t size_ = 0;
};

struct Expression {
  // NOLINTBEGIN(readability-identifier-naming): Lexical record fields.
  TokenKind kind;
  SpellingId spelling;
  SourceSpan where;
  std::string owned;
  std::string_view raw;
  SyntaxChildren children;
  std::variant<std::monostate, SortView, const SortRecipe*, SortExpr> sort;
  std::size_t token = 0;
  std::size_t last_token = 0;
  // NOLINTEND(readability-identifier-naming)
};

class Syntax {
 public:
  // NOLINTNEXTLINE(readability-identifier-naming): Current command record.
  CommandHeader header{};
  ExprId Add(Token token, bool borrow);
  void Close(ExprId id, SourcePosition end) noexcept;
  void DiscardChildren(ExprId id) noexcept;
  void Discard(ExprId id) noexcept;
  void Append(ExprId parent, ExprId child) {
    Entry(parent).children.Push(child);
  }
  void Capture(Token token);
  void Mark(ExprId id) noexcept;
  void ForgetSpelling(ExprId id) noexcept {
    auto& entry = Entry(id);
    entry.raw = entry.raw == "0" ? "0" : "";
  }
  void RemoveLast(ExprId parent) noexcept;
  void Reset() noexcept;
  template <class SortValue>
  const SortValue* ReducedSort(ExprId id) const noexcept {
    return std::get_if<SortValue>(&GetExpression(id).sort);
  }
  template <class SortValue>
  void SetReducedSort(ExprId id, SortValue sort) noexcept {
    Entry(id).sort = sort;
    DiscardChildren(id);
  }
  std::size_t LiveCount() const noexcept { return live_count_; }
  void ClearCapture() noexcept {
    captured_.clear();
    mark_ = 0;
    previous_ = TokenKind::kLeft;
  }

  const Expression& GetExpression(ExprId id) const noexcept {
    return (*pages_[id / kPageSize])[id % kPageSize];
  }
  Token GetTokenInfo(ExprId id) const noexcept {
    const auto& entry = GetExpression(id);
    return {entry.kind, entry.spelling, entry.raw, entry.where};
  }
  std::string_view Raw(ExprId id) const noexcept {
    return GetExpression(id).raw;
  }
  static bool Symbol(TokenKind kind) noexcept {
    return kind == TokenKind::kSymbol || kind == TokenKind::kQuoted;
  }
  TokenKind Type(ExprId id) const noexcept { return GetExpression(id).kind; }
  SpellingId Spelling(ExprId id) const noexcept {
    return GetExpression(id).spelling;
  }
  static std::string_view Name(const Token& token) noexcept {
    return token.kind == TokenKind::kQuoted
               ? token.raw.substr(1, token.raw.size() - 2)
               : token.raw;
  }
  std::string_view Name(ExprId id) const noexcept {
    return Name(GetTokenInfo(id));
  }
  const SourceSpan& Span(ExprId id) const noexcept {
    return GetExpression(id).where;
  }
  ChildRange Children(ExprId id) const noexcept {
    return GetExpression(id).children.Range();
  }
  bool Is(ExprId id, std::string_view unquoted) const noexcept {
    return Type(id) != TokenKind::kQuoted && Raw(id) == unquoted;
  }
  bool IsSymbol(ExprId id) const noexcept { return Symbol(Type(id)); }
  bool List(ExprId id) const noexcept { return Type(id) == TokenKind::kLeft; }
  std::string Render(
      ExprId id,
      const std::map<std::size_t, Replacement>* replacements = nullptr) const;

 private:
  static constexpr std::size_t kPageSize = 32;
  using Page = std::array<Expression, kPageSize>;
  Expression& Entry(ExprId id) noexcept {
    return (*pages_[id / kPageSize])[id % kPageSize];
  }
  std::vector<std::unique_ptr<Page>> pages_;
  std::size_t slots_ = 0;
  ExprId free_ = kNoExpr;
  std::size_t live_count_ = 0;
  std::string captured_;
  std::size_t mark_ = 0;
  TokenKind previous_ = TokenKind::kLeft;
};

// A source occurrence whose byte coordinates are needed only on failure.
struct Location {
  Location(const Syntax& syntax, ExprId expr) noexcept
      : syntax_(&syntax), expr_(expr) {}
  Location(const SourceSpan& where) noexcept : span_(&where), expr_(kNoExpr) {}
  const SourceSpan& Span() const noexcept {
    return expr_ == kNoExpr ? *span_ : syntax_->Span(expr_);
  }

 private:
  union {
    const Syntax* syntax_;
    const SourceSpan* span_;
  };
  ExprId expr_;
};

[[noreturn]] inline void Fail(DiagnosticCode code, Location where,
                              std::string_view message) {
  Fail(code, where.Span(), message);
}
[[noreturn]] inline void NodeFailure(NodeError error, Location where) {
  NodeFailure(error, where.Span());
}
template <class T>
T TakeAt(Result<T> result, Location where) {
  if (!result) [[unlikely]] {
    NodeFailure(result.error(), where);
  }
  if constexpr (!std::is_void_v<T>) return std::move(*result);
}

template <class T>
T TakeNumericAt(numeric::NumericResult<T> result, Location where) {
  if (!result) [[unlikely]] {
    NumericFailure(result.error(), where.Span());
  }
  return std::move(*result);
}

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
    std::array<std::uint16_t, 32 * 128> heads{};
    std::array<std::uint16_t, std::tuple_size_v<Catalog>> next{};
  };
  Table table;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto word = spelling(entries[i]);
    auto& head = table.heads[word.size() * 128 +
                             static_cast<unsigned char>(word.front())];
    table.next[i] = std::exchange(head, static_cast<std::uint16_t>(i + 1));
  }
  return table;
}
template <class Catalog, class Spelling, class Table>
std::uint16_t LookupSpelling(const Catalog& entries, Spelling spelling,
                             const Table& table,
                             std::string_view word) noexcept {
  if (word.empty() || word.size() >= 32 ||
      static_cast<unsigned char>(word.front()) >= 128)
    return 0;
  auto id =
      table.heads[word.size() * 128 + static_cast<unsigned char>(word.front())];
  while (id && spelling(entries[id - 1]) != word) id = table.next[id - 1];
  return id;
}
SpellingId ClassifySymbol(std::string_view word) noexcept;
const Operator* GetOperator(SpellingId id) noexcept;
std::optional<numeric::RoundingMode> GetRoundingMode(SpellingId id) noexcept;
bool IsSimpleSymbol(std::string_view word) noexcept;
std::size_t Utf8Size(int first) noexcept;
bool IsUtf8Continuation(int byte) noexcept;
bool IsUtf8Scalar(unsigned scalar, std::size_t size) noexcept;
bool IsQuotedByte(int byte) noexcept;

enum class GrammarRole : std::uint8_t {
  kCommand,
  kTerm,
  kSort,
  kIdentifier,
  kHead,
  kPlainIdentifier,
  kOpaque,
  kAttributeValue,
  kSymbol,
  kNumeral,
  kIndex,
  kKeyword,
  kString,
  kBoolean,
  kPattern,
  kPatternSymbol,
  kLetBindings,
  kLetBinding,
  kFormals,
  kNonemptyFormals,
  kFormal,
  kCases,
  kBranch,
  kSorts,
  kSymbols,
  kParameters,
  kTerms,
  kValues,
  kDatatype,
  kConstructors,
  kConstructor,
  kDatatypeHeaders,
  kDatatypeHeader,
  kDatatypes,
  kRecDeclarations,
  kRecDeclaration,
  kRecBodies,
  kCommandName,
  kInvalid
};

struct GrammarFrame {
  // NOLINTBEGIN(readability-identifier-naming): Grammar frame fields.
  GrammarRole role;
  SourceSpan where;
  std::size_t count = 0;
  SpellingId head = kUnknownSpelling;
  bool expect_attribute = true;
  // NOLINTEND(readability-identifier-naming)
};

class Scanner {
 public:
  Scanner(std::string_view input, std::uint32_t source);
  Scanner(Reader reader, std::uint32_t source);
  SourceSpan Here() const noexcept { return {source_, position_, position_}; }
  std::optional<Token> Next();
  // One indexed BV literal may borrow its digits until its width and close.
  void HoldToken(std::string_view bytes);
  std::string_view HeldToken(SourceSpan where) const;
  void ReleaseToken() noexcept;
  bool IsText() const noexcept { return !reader_; }
  std::vector<GrammarFrame>& Frames() noexcept { return frames_; }
#ifdef DANDELION_FRONTEND_TESTING
  void Checkpoint();
  void FailAllocationAfter(std::size_t checkpoints) noexcept {
    fail_after_ = checkpoints;
  }
#endif

 private:
  static constexpr std::size_t kReaderCapacity = 64 * 1024;
  int Peek();
  void TakeByte(int byte);
  void Advance(std::size_t count);
  void Trivia();
  std::string_view TokenBytes();
  void SaveTokenPart();
  void SaveHeldToken() noexcept;
  void Scalar(int first);
  Token ScanToken(int first);

  Reader reader_;
  std::unique_ptr<std::array<char, kReaderCapacity>> buffer_;
  std::string_view window_;
  std::size_t cursor_ = 0;
  SourcePosition position_;
  std::uint32_t source_;
  bool after_cr_ = false;
  bool eof_ = false;
  std::string scratch_;
  std::string held_text_;
  std::string_view held_;
  ResourceKind held_error_ = ResourceKind::kNone;
  bool held_borrowed_ = false;
  std::vector<GrammarFrame> frames_;
  std::optional<std::size_t> token_start_;
#ifdef DANDELION_FRONTEND_TESTING
  std::optional<std::size_t> fail_after_;
#endif
};

struct Option;

struct GrammarCommand {
  // NOLINTBEGIN(readability-identifier-naming): Grammar command fields.
  CommandKind kind = CommandKind::kUnsupported;
  CommandKind shape = CommandKind::kUnsupported;
  std::string_view excluded;
  const Option* option = nullptr;
  std::size_t recursive_count = 0;
  std::size_t datatype_headers = 0;
  std::size_t datatype_bodies = 0;
  // NOLINTEND(readability-identifier-naming)
};

GrammarRole SpecialChildRole(GrammarFrame& parent, const Token& token,
                             const GrammarCommand& command);
bool AcceptAtom(GrammarRole role, const Token& token);
bool AcceptList(GrammarRole role);
std::size_t Minimum(const GrammarFrame& frame, const GrammarCommand& command);
void ReadCommandName(GrammarCommand& command, SpellingId spelling);

std::optional<CommandHeader> ReadGrammar(Scanner& scanner);

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
  CommandResponse default_value;
};
const Option* FindOption(std::string_view name) noexcept;

inline GrammarRole NextRole(GrammarFrame& parent, const Token& token,
                            const GrammarCommand& command) {
  using enum GrammarRole;
  if (parent.role == kTerm && parent.head == kUnknownSpelling &&
      parent.count != 0) {
    ++parent.count;
    return kTerm;
  }
  if (parent.role == kLetBindings) {
    ++parent.count;
    return kLetBinding;
  }
  if (parent.role == kLetBinding && parent.count < 2)
    return parent.count++ == 0 ? kSymbol : kTerm;
  return SpecialChildRole(parent, token, command);
}

inline bool AtomAllowed(GrammarRole role, const Token& token) {
  using enum GrammarRole;
  if (role == kTerm)
    return Syntax::Symbol(token.kind) || token.kind >= TokenKind::kNumeral;
  if (role == kHead) return Syntax::Symbol(token.kind);
  return AcceptAtom(role, token);
}

inline bool ListAllowed(GrammarRole role) {
  using enum GrammarRole;
  return role == kTerm || role == kHead || role == kLetBinding ||
         role == kLetBindings || AcceptList(role);
}

inline bool Complete(const GrammarFrame& frame, const GrammarCommand& command) {
  using enum GrammarRole;
  if (frame.role == kTerm &&
      (frame.count >= 3 || frame.head == kUnknownSpelling))
    return frame.count != 0;
  if (frame.role == kLetBindings) return frame.count != 0;
  if (frame.role == kLetBinding) return frame.count == 2;
  return frame.count >= Minimum(frame, command);
}

inline void ClosedGrammarFrame(const GrammarFrame& frame,
                               GrammarCommand& command) {
  using enum GrammarRole;
  if (frame.role == kRecDeclarations) command.recursive_count = frame.count;
  if (frame.role == kDatatypeHeaders) command.datatype_headers = frame.count;
  if (frame.role == kDatatypes) command.datatype_bodies = frame.count;
}

inline void ReadGrammarAtom(const GrammarFrame& frame, GrammarRole role,
                            const Token& token, GrammarCommand& command) {
  using enum GrammarRole;
  if (role == kCommandName) {
    ReadCommandName(command, token.spelling_id);
  } else if (frame.role == kCommand && frame.count == 2 &&
             command.kind == CommandKind::kSetOption) {
    command.option = FindOption(token.raw);
  }
}

inline bool BeginCommand(Scanner& scanner, Token& token) {
  const auto first = scanner.Next();
  if (!first) return false;
  if (first->kind != TokenKind::kLeft)
    Fail(DiagnosticCode::kSyntax,
         {first->where.source, first->where.begin, first->where.begin},
         "Expected a command list");
  token = *first;
  return true;
}

inline Token ScanCommandToken(Scanner& scanner) {
  const auto next = scanner.Next();
  if (!next) [[unlikely]]
    Fail(DiagnosticCode::kSyntax, scanner.Here(),
         "Incomplete command at end of input");
  return *next;
}

template <class Frame>
Frame& PushFrame(Scanner& scanner, std::vector<Frame>& frames, GrammarRole role,
                 const Token& token) {
#ifdef DANDELION_FRONTEND_TESTING
  if (frames.size() == frames.capacity()) scanner.Checkpoint();
#else
  static_cast<void>(scanner);
#endif
  auto& frame = frames.emplace_back();
  frame.role = role;
  frame.where = token.where;
  return frame;
}

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
using SortBinding =
    std::variant<SortConstructorView, Sort, Alias, DatatypeSort>;
enum class AliasOrdinal : std::size_t {};
enum class MemberOrdinal : std::size_t {};
using TermBinding =
    std::variant<DeclInstanceView, DatatypeMember, AliasOrdinal>;
using TermEntry = std::variant<DeclInstanceView, MemberOrdinal, AliasOrdinal>;
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
  using Entry = std::pair<std::string_view, T>;
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
    const bool grow = size >= capacity - capacity / 4;
    const bool widen =
        wide_.empty() && size == std::numeric_limits<std::uint32_t>::max();
    if (grow || widen) {
      if (grow && capacity > std::numeric_limits<std::size_t>::max() / 2)
        throw std::length_error("Name table capacity");
      const auto next =
          grow ? std::max(std::size_t{8}, capacity * 2) : capacity;
      if (widen || !wide_.empty()) {
        Rehash(wide_, next);
        std::vector<Compact>{}.swap(compact_);
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
  std::size_t Size() const noexcept { return entries_.size(); }
  void EraseLast() noexcept {
    const auto hash = Hash(entries_.back().first);
    if (wide_.empty())
      Erase(compact_, hash);
    else
      Erase(wide_, hash);
    entries_.pop_back();
  }
  const auto& Entries() const noexcept { return entries_; }
#ifdef DANDELION_FRONTEND_TESTING
  void ForceCollisions() noexcept { collide_ = true; }
#endif

 private:
  template <class Ordinal>
  struct Slot {
    std::uint32_t hash;
    Ordinal ordinal;
  };
  using Compact = Slot<std::uint32_t>;
  using Wide = Slot<std::size_t>;
  static_assert(sizeof(Compact) == 8);

  std::uint32_t Hash(std::string_view name) const noexcept {
#ifdef DANDELION_FRONTEND_TESTING
    if (collide_) return 0;
#endif
    const auto hash = static_cast<std::uint64_t>(TextHash{}(name));
    return static_cast<std::uint32_t>(hash ^ (hash >> 32));
  }
  template <class Ordinal>
  const Entry* Find(const std::vector<Slot<Ordinal>>& slots,
                    std::string_view name, std::uint32_t hash) const noexcept {
    const auto mask = slots.size() - 1;
    auto index = hash & mask;
    while (const auto ordinal = slots[index].ordinal) {
      if (slots[index].hash == hash) {
        const auto& entry = entries_[ordinal - 1];
        if (entry.first == name) return &entry;
      }
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
    if (wide_.empty()) {
      for (auto [hash, ordinal] : compact_)
        if (ordinal) Place(next, hash, ordinal);
    } else {
      for (auto [hash, ordinal] : wide_)
        if (ordinal) Place(next, hash, ordinal);
    }
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
  std::vector<Entry> entries_;
#ifdef DANDELION_FRONTEND_TESTING
  bool collide_ = false;
#endif
  std::vector<Compact> compact_;
  std::vector<Wide> wide_;
};

// Owns canonical keys that do not already have manager-backed spelling.
class NamePool {
 public:
  struct Mark {
    // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
    std::size_t blocks;
    std::size_t used;
    // NOLINTEND(readability-identifier-naming)
  };

  Mark GetMark() const noexcept {
    return {blocks_.size(), blocks_.empty() ? 0 : blocks_.back().used};
  }
  std::string_view Append(std::string_view name) {
    if (name.empty()) return {};
    if (blocks_.empty() ||
        name.size() > blocks_.back().capacity - blocks_.back().used) {
      constexpr std::size_t kBlockSize = 4096;
      const auto capacity = std::max(kBlockSize, name.size());
      blocks_.push_back(
          {std::make_unique_for_overwrite<char[]>(capacity), capacity, 0});
    }
    auto& block = blocks_.back();
    auto* const written = block.text.get() + block.used;
    std::ranges::copy(name, written);
    block.used += name.size();
    return {written, name.size()};
  }
  void Restore(Mark mark) noexcept {
    blocks_.resize(mark.blocks);
    if (!blocks_.empty()) blocks_.back().used = mark.used;
  }

 private:
  struct Block {
    // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
    std::unique_ptr<char[]> text;
    std::size_t capacity;
    std::size_t used;
    // NOLINTEND(readability-identifier-naming)
  };
  std::vector<Block> blocks_;
};

struct Signature {
  static const Signature& Of(const QueryContext& query) noexcept {
    return *query.signature_;
  }

  TermBinding ResolveTerm(const TermEntry& entry) const noexcept {
    if (const auto* member = std::get_if<MemberOrdinal>(&entry))
      return members[std::to_underlying(*member)];
    if (const auto* instance = std::get_if<DeclInstanceView>(&entry))
      return *instance;
    return std::get<AliasOrdinal>(entry);
  }

  const Profile* profile = nullptr;
  NamePool name_pool;
  NameTable<SortBinding> sorts;
  NameTable<TermEntry> terms;
  std::vector<DatatypeMember> members;
  std::vector<Node> aliases;
  std::unordered_set<SortRecipe, RecipeHash> recipes;
  static std::optional<std::size_t> FunctionOrdinal(const QueryContext& query,
                                                    DeclView decl) noexcept;
};

constexpr std::size_t kCacheSlots = 256;
struct LiteralCache {
  using Value =
      std::variant<std::uint64_t, numeric::Integer, numeric::BitVector>;
  struct Entry {
    std::string digits;
    Value value;
    std::uint32_t width = 0;
    int radix = 0;
  };
  std::array<Entry, kCacheSlots> entries;
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

class ElaborationScratch;
struct DeleteElaborationScratch {
  void operator()(ElaborationScratch* scratch) const noexcept;
};

template <class T>
void ClearScratch(std::vector<T>& buffer) noexcept {
  constexpr std::size_t kMaxRetainedBytes = 256 * 1024;
  buffer.clear();
  if (buffer.capacity() > kMaxRetainedBytes / sizeof(T))
    std::vector<T>{}.swap(buffer);
}

template <class Map>
void ClearScratch(Map& buffer) noexcept {
  constexpr std::size_t kMaxRetainedBuckets = 4096;
  buffer.clear();
  if (buffer.bucket_count() > kMaxRetainedBuckets) Map{}.swap(buffer);
}

struct PendingTerms;
struct DefinitionInputs;
class PendingDatatypeGroup;
class PendingSortAlias;

struct SessionState {
  explicit SessionState(Scanner scanner, ParseOptions parse = {});
  SessionState(Scanner scanner, std::shared_ptr<ModelOwner> model,
               ParseOptions parse);
  ~SessionState();
  const QueryContext& Prefix() const noexcept;
  const Signature& GetSignature() const noexcept {
    return *Prefix().signature_;
  }
  void Count([[maybe_unused]] std::size_t FrontendStats::* member) noexcept {
#ifdef DANDELION_FRONTEND_TESTING
    ++(stats.*member);
#endif
  }
  void AddAnnotations(std::vector<Annotation> annotations);
#ifdef DANDELION_FRONTEND_TESTING
  void Checkpoint();
#endif
  Node NumericLiteral(std::string_view digits, int radix, SortView sort,
                      Location where);
  Outcome<std::optional<TypedCommand>> ReadCommand(
      const ModelOwner* model,
      const std::optional<CheckAnswer>* answer = nullptr);
  void CheckCommand(CommandKind kind, SourceSpan where, const ModelOwner* model,
                    const std::optional<CheckAnswer>* answer) const;
  void Publish(const Syntax& syntax, TypedCommand& command, Node term,
               PendingTerms& pending, DefinitionInputs* definition,
               PendingDatatypeGroup* datatypes, PendingSortAlias* alias);
  void ApplySemantic(const Syntax& syntax, TypedCommand& command, Node term,
                     PendingTerms& pending, DefinitionInputs* definition,
                     PendingDatatypeGroup* datatypes, PendingSortAlias* alias);

  Scanner scanner;
  std::unique_ptr<Syntax> fragments;
  NodeManager manager;
  std::shared_ptr<QueryContext> building;
  std::shared_ptr<const QueryContext> sealed;
  std::shared_ptr<ModelOwner> inspection_model;
  std::unique_ptr<SourceRecords> source_records;
  SessionOptions options;
  ParseOptions parse_options;
  std::unique_ptr<LiteralCache> literal_cache;
  std::unique_ptr<SortCache> sort_cache;
  std::unique_ptr<ElaborationScratch, DeleteElaborationScratch> elaboration;
  std::vector<SortView> domain_sorts;
  SessionPhase phase = SessionPhase::kBeforeLogic;
  bool in_read = false;
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
  void AddTerm(std::string_view name, DeclInstanceView instance);
  void AddTerm(std::string_view name, DatatypeMember member);
  void AddSort(std::string_view name, SortBinding binding);
  const SortRecipe* InternRecipe(SortRecipe recipe);
  void AddAlias(std::string_view name, Node term);
  void AddNamed(std::string_view name, const Node& term, SourceSpan where);
  void AddAnnotation(Annotation annotation);
  void AddAssertion(Node term, SourceSpan where);
  void AddUserSort(SortConstructor sort, std::string_view spelling,
                   SourceSpan where);
  void AddFunction(Decl decl, std::string_view spelling,
                   std::vector<std::string> rank, SourceSpan where);
  std::size_t AddDefinition(DefinedFunction definition);
  void AddDefinitionSource(DefinitionSource source);
  std::size_t StageGroup();
  void FinishGroup(std::size_t index, DatatypeGroup group) noexcept;

 private:
  SessionState& state_;
  QueryContext& query_;
  Signature& signature_;
  std::string_view StoreName(std::string_view name);
  SourceRecords* records_;
  std::vector<const SortRecipe*> added_recipes_;
  const NamePool::Mark pooled_names_;
  const std::size_t term_names_;
  const std::size_t sort_names_;
  const std::size_t alias_roots_;
  const std::size_t members_;
  const std::size_t assertions_;
  const std::size_t sorts_;
  const std::size_t functions_;
  const std::size_t function_runs_;
  const std::size_t definitions_;
  const std::size_t groups_;
  const std::size_t source_definitions_;
  const std::size_t source_named_;
  const std::size_t source_annotations_;
  bool committed_ = false;
};

bool IsReservedName(std::string_view name) noexcept;
bool IsActiveSort(const Profile& profile, SpellingId spelling) noexcept;
bool IsActiveTerm(const Profile& profile, std::string_view name,
                  SpellingId spelling) noexcept;
inline bool IsActiveTerm(const Profile& profile,
                         std::string_view name) noexcept {
  return IsActiveTerm(profile, name, ClassifySymbol(name));
}
void CheckSortName(const SessionState& state, const Syntax& syntax, ExprId name,
                   bool global);
void CheckTermName(const SessionState& state, std::string_view name,
                   SpellingId spelling, SourceSpan where, bool global);
void CheckTermName(const SessionState& state, const Syntax& syntax, ExprId name,
                   bool global);
std::vector<numeric::Integer> NumericIndices(const Syntax& syntax,
                                             Identifier id,
                                             std::size_t expected,
                                             Location where);
std::uint64_t ParseArity(const Syntax& syntax, ExprId numeral);
void CheckMinimumIndex(const Syntax& syntax, Identifier id,
                       std::span<const numeric::Integer> indices,
                       std::size_t index, std::int64_t minimum);

inline std::optional<std::uint64_t> ParseWord(std::string_view digits,
                                              int radix = 10) noexcept {
  std::uint64_t value;
  const auto* end = digits.data() + digits.size();
  const auto parsed = std::from_chars(digits.data(), end, value, radix);
  if (parsed.ec == std::errc{} && parsed.ptr == end) return value;
  return std::nullopt;
}
SortView CachedBitVectorSort(SessionState& session, std::uint64_t width,
                             Location where);
SortView CachedBitVectorSort(SessionState& session, numeric::IntegerView width,
                             Location where);
SortView CachedFloatingPointSort(SessionState& session,
                                 numeric::IntegerView exponent,
                                 numeric::IntegerView significand,
                                 Location where);

using Parameters = TextMap<std::uint64_t>;
SortView ResolveSort(SessionState& session, const Syntax& syntax, ExprId root);
class PendingSortAlias {
 public:
  PendingSortAlias(SessionState& state, const Syntax& syntax, ExprId name,
                   ChildRange parameters);
  bool HasParameter(std::string_view name) const noexcept {
    return parameters_.contains(name);
  }
  const SortRecipe* Reduce(ExprId expr);
  const SortRecipe* InternRecipe(SortRecipe recipe);
  void Publish(Transaction& txn, ExprId name, ExprId body);

 private:
  SessionState& state_;
  const Syntax& syntax_;
  Parameters parameters_;
  std::unordered_set<SortRecipe, RecipeHash> recipes_;
  std::vector<const SortRecipe*> order_;
};

SortExpr ResolveField(SessionState& session, const Syntax& syntax, ExprId root,
                      const Parameters& parameters, DatatypeBuilder& builder,
                      const TextMap<DatatypeSort>& forward, std::uint64_t slot);

class PendingDatatypeGroup {
 public:
  explicit PendingDatatypeGroup(SessionState& session);
  void ReadHeaders(const Syntax& syntax, ExprId list, SourceSpan where);
  void StartBody(const Syntax& syntax, ExprId body, ExprId name,
                 ChildRange parameters);
  void StartConstructor(const Syntax& syntax, ExprId name);
  bool HasSortName(std::string_view name) const noexcept {
    return parameters_.contains(name) || sorts_.contains(name);
  }
  SortExpr ReduceSort(const Syntax& syntax, ExprId expr);
  void AddField(const Syntax& syntax, ExprId field);
  void EndConstructor(SourceSpan where);
  void EndBody() noexcept;
  void Publish(Transaction& txn, SourceSpan where);

 private:
  void AddHeader(const Syntax& syntax, ExprId name, std::uint64_t arity);
  void Start(SourceSpan where);
  std::string_view AddMember(const Syntax& syntax, ExprId name,
                             std::optional<std::uint64_t> field);

  SessionState& session_;
  const std::size_t group_;
  TextMap<DatatypeSort> sorts_;
  std::vector<DatatypeHeader> headers_;
  std::unordered_set<std::string, TextHash, std::equal_to<>> term_index_;
  std::vector<std::pair<std::string_view, DatatypeMember>> members_;
  std::optional<DatatypeBuilder> builder_;
  Parameters parameters_;
  std::vector<DatatypeFieldSpec> fields_;
  std::string_view constructor_name_;
  std::uint64_t slot_ = 0;
  std::uint64_t ctor_ = 0;
  bool in_body_ = false;
};
struct Operator {
  std::string_view name;
  Kind kind;
  unsigned features;
  std::size_t indices;
  std::uint64_t min_arity;
  std::uint64_t max_arity = min_arity;
};
std::span<Node> CommutativeOperands(Kind kind,
                                    std::span<Node> operands) noexcept;
bool OrderCommutative(std::span<Node> operands) noexcept;
const Operator& OperatorFor(Kind kind) noexcept;
bool IsRestrictedInt(Kind kind) noexcept;
bool IsKnownExcludedTerm(std::string_view name) noexcept;
std::optional<numeric::FpSpecial> FindFpSpecial(std::string_view name) noexcept;
std::string_view RoundingSpelling(numeric::RoundingMode mode) noexcept;
std::string_view SpecialSpelling(numeric::FpSpecial special) noexcept;
struct NamedAliases {
  // NOLINTBEGIN(readability-identifier-naming): Pending publication fields.
  std::deque<NamedTerm> records;
  std::unordered_map<std::string_view, std::size_t> index;
  // NOLINTEND(readability-identifier-naming)
};

struct PendingTerms {
  // NOLINTBEGIN(readability-identifier-naming): Pending publication fields.
  std::unique_ptr<NamedAliases> named;
  std::vector<Annotation> annotations;
  const Node* Find(std::string_view name) const noexcept;
  void AddNamed(SessionState& state, std::string_view name, const Node& term,
                SourceSpan where);
  void Publish(Transaction& txn);
  // NOLINTEND(readability-identifier-naming)
};

struct DefinitionInputs {
  // NOLINTBEGIN(readability-identifier-naming): Pending publication fields.
  std::vector<Decl> formals;
  std::vector<SortView> domain;
  std::vector<std::string> source_rank;
  SortView range;
  // NOLINTEND(readability-identifier-naming)
};

void ApplyTerms(SessionState& state, const Syntax& syntax,
                TypedCommand& command, Transaction& txn, Node term,
                DefinitionInputs* definition);
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
