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

struct Token {
  TokenKind kind;
  SourceSpan where;
  std::size_t start;
  std::size_t length;
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

struct Syntax {
  std::string_view bytes;
  std::string arena;
  std::vector<Token> tokens;
  std::vector<Expression> expressions;
  std::vector<ExprId> child_edges;
  CommandHeader header = {};
  std::string_view RawToken(std::size_t i) const noexcept;
  std::string_view Raw(ExprId id) const noexcept {
    return RawToken(expressions[id].token);
  }
  TokenKind Type(ExprId id) const noexcept {
    return tokens[expressions[id].token].kind;
  }
  std::string_view Name(ExprId id) const noexcept;
  SourceSpan Span(ExprId id) const noexcept;
  std::span<const ExprId> Children(ExprId id) const noexcept;
  bool Is(ExprId id, std::string_view unquoted) const noexcept;
  bool IsSymbol(ExprId id) const noexcept;
  bool List(ExprId id) const noexcept { return Type(id) == TokenKind::kLeft; }
  std::string Render(ExprId id,
                     const std::unordered_map<std::size_t, Replacement>*
                         replacements = nullptr) const;
};

struct Identifier {
  ExprId name;
  std::span<const ExprId> indices;
  ExprId qualifier = kNoExpr;
};
Identifier GetIdentifier(const Syntax& syntax, ExprId id);
bool IsReservedWord(std::string_view word) noexcept;
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
  std::optional<Syntax> Next();

 private:
  static constexpr std::size_t kReaderCapacity = 64 * 1024;
  int Peek();
  void TakeByte(int byte, Syntax* syntax);
  void Trivia();
  void Scalar(int first, Syntax* syntax);
  Token ScanToken(Syntax& syntax, int first);
  std::optional<Syntax> ReadCommand();

  std::string_view text_;
  Reader reader_;
  std::unique_ptr<std::array<char, kReaderCapacity>> buffer_;
  std::string_view window_;
  std::size_t cursor_ = 0;
  SourcePosition position_;
  std::uint32_t source_;
  bool after_cr_ = false;
  bool eof_ = false;
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

struct Signature {
  static const Signature& Of(const QueryContext& query) noexcept {
    return *query.signature_;
  }
  const Profile* profile = nullptr;
  TextMap<SortBinding> sorts;
  TextMap<TermBinding> terms;
  std::unordered_set<SortRecipe, RecipeHash> recipes;
  std::unordered_map<SortId, std::string_view> sort_spellings;
  std::unordered_map<DeclId, std::size_t> definition_index;
  std::unordered_map<DeclId, std::size_t> function_index;
};

struct SessionState {
  explicit SessionState(Scanner scanner);
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
  void AddFunction(UserFunction function);
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
