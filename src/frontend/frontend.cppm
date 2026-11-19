export module dandelion.frontend;
export import dandelion.node;
import std;

namespace dandelion::frontend::detail {
struct Signature;
struct SessionState;
class Transaction;
struct ModelValue;
struct ModelState;
class ModelPrinter;
}  // namespace dandelion::frontend::detail

export namespace dandelion::frontend {

// NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
struct SourcePosition {
  std::uint64_t offset = 0;
  std::uint64_t line = 1;
  std::uint64_t column = 1;
};
bool operator==(const SourcePosition& left,
                const SourcePosition& right) noexcept;
struct SourceSpan {
  std::uint32_t source = 0;
  SourcePosition begin;
  SourcePosition end;
};
bool operator==(const SourceSpan& left, const SourceSpan& right) noexcept;

enum class DiagnosticCode {
  kSyntax,
  kInvalidState,
  kInvalidInput,
  kUnsupported,
  kInputIo,
  kOutputIo,
  kResource,
  kEngineFailure,
  kModelUnavailable,
  kModelDependent,
  kModelFailure,
};
struct Diagnostic {
  DiagnosticCode code;
  SourceSpan where = {};
  std::string_view message = {};
  std::optional<NodeError> node_error = std::nullopt;
  std::error_code io_error = {};
};
template <class T>
using Outcome = std::expected<T, Diagnostic>;

using ReadResult = std::expected<std::size_t, std::error_code>;
// A positive size means bytes available now. Zero is EOF. A callback may
// block until input is available, then must return without waiting to fill.
using Reader = std::function<ReadResult(std::span<char>)>;

enum class SessionPhase { kBeforeLogic, kBuilding, kAfterCheck, kTerminated };
enum class CommandKind {
  kSetLogic,
  kSetInfo,
  kSetOption,
  kGetInfo,
  kGetOption,
  kEcho,
  kDeclareSort,
  kDeclareFun,
  kDeclareConst,
  kDefineSort,
  kDefineFun,
  kDefineConst,
  kDeclareDatatype,
  kDeclareDatatypes,
  kAssert,
  kCheckSat,
  kGetModel,
  kGetValue,
  kExit,
  kUnsupported,
};
struct CommandHeader {
  CommandKind kind;
  SourceSpan where;
};
struct SessionOptions {
  bool print_success = false;
  bool produce_models = false;
  bool global_declarations = false;
};
struct Attribute {
  std::string key;
  std::optional<std::string> value;
  SourceSpan where;
};
struct Assertion {
  Node term;
  SourceSpan where;
};
struct Annotation {
  Node term;
  std::vector<Attribute> attributes;
  SourceSpan where;
};
struct NamedTerm {
  std::string name;
  Node term;
  SourceSpan where;
};
struct UserSort {
  SortConstructor constructor;
  std::string source_name;
  SourceSpan where;
};
struct UserFunction {
  Decl decl;
  std::string source_name;
  // Domain sorts in declaration order followed by the result sort.
  std::vector<std::string> source_rank;
  SourceSpan where;
};
struct DefinedFunction {
  Decl decl;
  DeclInstance instance;
  std::vector<Decl> formals;
  Node body;
  std::string source_name;
  SourceSpan where;
};
struct FrontendStats {
  std::size_t definition_bodies = 0;
  std::size_t defined_calls = 0;
  std::size_t recipe_pairs = 0;
  std::size_t sort_recipe_pairs = 0;
  std::size_t closure_visits = 0;
};

class QueryContext {
 public:
  ~QueryContext();
  QueryContext(const QueryContext&) = delete;
  QueryContext& operator=(const QueryContext&) = delete;
  const ManagerRef& GetManager() const noexcept { return manager_; }
  std::string_view GetLogic() const noexcept { return logic_; }
  const SessionOptions& GetOptions() const noexcept { return options_; }
  std::span<const Assertion> GetAssertions() const noexcept {
    return assertions_;
  }
  std::span<const UserSort> GetUserSorts() const noexcept {
    return user_sorts_;
  }
  std::span<const UserFunction> GetFunctions() const noexcept {
    return functions_;
  }
  std::span<const DatatypeGroup> GetDatatypeGroups() const noexcept {
    return datatype_groups_;
  }
  std::span<const DefinedFunction> GetDefinitions() const noexcept {
    return definitions_;
  }
  const DefinedFunction* FindDefinition(DeclView decl) const noexcept;
  const DefinedFunction* FindDefinition(DeclId id) const noexcept;
  std::span<const NamedTerm> GetNamedTerms() const noexcept {
    return named_terms_;
  }
  std::span<const Annotation> GetAnnotations() const noexcept {
    return annotations_;
  }
  std::span<const Attribute> GetMetadata() const noexcept { return metadata_; }
  // Resolve a declaration by identity in this sealed signature, for example
  // after attaching to the manager for model evaluation.
  bool HasFunction(DeclView decl) const noexcept;

 private:
  friend struct detail::SessionState;
  friend struct detail::Signature;
  friend class detail::Transaction;
  explicit QueryContext(ManagerRef manager);
  ManagerRef manager_;
  std::string_view logic_;
  SessionOptions options_;
  std::vector<Assertion> assertions_;
  std::vector<UserSort> user_sorts_;
  std::vector<UserFunction> functions_;
  std::vector<DatatypeGroup> datatype_groups_;
  std::vector<DefinedFunction> definitions_;
  std::vector<NamedTerm> named_terms_;
  std::vector<Annotation> annotations_;
  std::vector<Attribute> metadata_;
  std::unique_ptr<detail::Signature> signature_;
};

struct ValueRequest {
  Node term;
  std::string input;
  SourceSpan where;
};
struct TypedCommand {
  CommandHeader header;
  std::string text;
  std::shared_ptr<const QueryContext> query;
  std::vector<ValueRequest> values;
  std::vector<Annotation> annotations;
  bool needs_unknown_reason = false;
};
class ModelOwner;
class Session;

enum class CheckAnswer { kSat, kUnsat, kUnknown };
enum class UnknownReason {
  kUnknown,
  kIncomplete,
  kMemout,
  kTimeout,
  kResourceout
};

class ValueRef {
 public:
  ValueRef() noexcept = default;
  bool IsNull() const noexcept { return !value_; }
  SortView GetSort() const noexcept;
  std::string_view GetKey() const noexcept;
  std::shared_ptr<const void> GetPayload() const noexcept;

 private:
  friend class ModelOwner;
  explicit ValueRef(std::shared_ptr<const detail::ModelValue> value) noexcept;
  std::shared_ptr<const detail::ModelValue> value_;
};
struct DatatypeValue {
  DeclInstance constructor;
  std::vector<ValueRef> fields;
};
// A scalar contains its typed literal node. A datatype contains an
// instantiated constructor and its values. Abstract arrays and UF values
// require no printable description.
using ValueDescription = std::variant<std::monostate, Node, DatatypeValue>;

class ModelProxy {
 public:
  const Node& GetTerm() const noexcept { return term_; }
  const ValueRef& GetValue() const noexcept { return value_; }

 private:
  friend class ModelOwner;
  ModelProxy(Node term, ValueRef value) noexcept;
  Node term_;
  ValueRef value_;
};
struct ExportedFunction {
  Decl declaration;
  std::vector<Decl> formals;
  Node body;
};
struct ModelExport {
  std::vector<ExportedFunction> functions;
  std::vector<ModelProxy> proxies;
};
class ModelProvider {
 public:
  virtual ~ModelProvider() = default;
  virtual Outcome<ValueRef> Evaluate(ModelOwner& model, NodeView term) = 0;
  virtual Outcome<ValueDescription> Describe(ModelOwner& model,
                                             const ValueRef& value) = 0;
  virtual Outcome<ModelExport> ExportModel(ModelOwner& model) = 0;
};
class ModelOwner {
 public:
  static Outcome<std::shared_ptr<ModelOwner>> Create(
      std::shared_ptr<const QueryContext> query,
      std::unique_ptr<ModelProvider> provider, CheckAnswer answer,
      UnknownReason reason = UnknownReason::kUnknown);
  ModelOwner(const ModelOwner&) = delete;
  ModelOwner& operator=(const ModelOwner&) = delete;
  ~ModelOwner();
  const QueryContext& GetQuery() const noexcept;
  const std::shared_ptr<const QueryContext>& GetSharedQuery() const noexcept;
  CheckAnswer GetAnswer() const noexcept;
  UnknownReason GetReason() const noexcept;
  // The provider owns the semantic key space. Within one sort, equality of
  // complete keys must be equivalent to equality of the represented values.
  Outcome<ValueRef> MakeValue(SortView sort, std::string key,
                              std::shared_ptr<const void> payload = {}) const;
  Outcome<ModelProxy> MakeProxy(const ValueRef& value);
  Outcome<ValueRef> LookupProxy(NodeView proxy) const;
  Outcome<ValueRef> Evaluate(NodeView term);
  Outcome<std::pair<Node, std::string>> ResolveName(
      std::string_view name, std::optional<SortView> qualifier,
      SourceSpan where) const;
#ifdef DANDELION_FRONTEND_TESTING
  void ForceKeyHashCollisions();
#endif

 private:
  friend class detail::ModelPrinter;
  explicit ModelOwner(std::unique_ptr<detail::ModelState> state) noexcept;
  bool Owns(const ValueRef& value) const noexcept;
  ValueRef EvaluateResolved(NodeView term, SourceSpan where);
  std::unique_ptr<detail::ModelState> state_;
};

struct CheckResult {
  CheckAnswer answer;
  UnknownReason reason = UnknownReason::kUnknown;
  std::unique_ptr<ModelProvider> model;
};
using Engine =
    std::function<Outcome<CheckResult>(const QueryContext&, SessionOptions)>;
using WriteResult = std::expected<std::size_t, std::error_code>;
struct ResponseSink {
  std::function<WriteResult(std::string_view)> write;
  std::function<std::expected<void, std::error_code>()> flush;
};
struct RunResult {
  int status = 0;
  std::optional<Diagnostic> diagnostic;
  std::shared_ptr<ModelOwner> model;
};
RunResult Run(Session& session, const Engine& engine, const ResponseSink& sink);

class Session {
 public:
  // The text adapter borrows bytes until the session is destroyed or stops
  // reading. A query, typed command or retained model owns its own records.
  static Outcome<Session> FromText(std::string_view text,
                                   std::uint32_t source = 0);
  static Outcome<Session> FromReader(Reader reader, std::uint32_t source = 0);
  static Outcome<Session> ForModelText(std::shared_ptr<ModelOwner> model,
                                       std::string_view text,
                                       std::uint32_t source = 0);
  static Outcome<Session> ForModelReader(std::shared_ptr<ModelOwner> model,
                                         Reader reader,
                                         std::uint32_t source = 0);
  Session(Session&&) noexcept;
  Session& operator=(Session&&) noexcept;
  ~Session();

  Outcome<std::optional<CommandHeader>> NextSyntax();
  // Consumes the pending command. Supplying a model is needed only for
  // model-local names in a value request.
  Outcome<TypedCommand> Apply(const ModelOwner* model = nullptr);
  SessionPhase GetPhase() const noexcept;
  const SessionOptions& GetOptions() const noexcept;
  std::span<const Attribute> GetMetadata() const noexcept;
  const QueryContext& GetPrefix() const noexcept;
#ifdef DANDELION_FRONTEND_TESTING
  FrontendStats GetStats() const noexcept;
  void FailAllocationAfter(std::size_t checkpoints) noexcept;
#endif

 private:
  friend RunResult Run(Session&, const Engine&, const ResponseSink&);
  explicit Session(std::unique_ptr<detail::SessionState> state) noexcept;
  std::unique_ptr<detail::SessionState> state_;
};
// NOLINTEND(readability-identifier-naming)
}  // namespace dandelion::frontend
