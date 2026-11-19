module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {
namespace {
using enum DeclarationPermission;
constexpr auto kProfiles = std::to_array<Profile>({
    {"QF_SAT", 0, kConstants},
    {"QF_BV", kBitVectors, kConstants},
    {"QF_UFBV", kBitVectors, kFunctions},
    {"QF_ABV", kArrays | kBitVectors, kConstants},
    {"QF_AUFBV", kArrays | kBitVectors, kFunctions},
    {"QF_AX", kArrays, kSorts},
    {"QF_UF", 0, kFunctions},
    {"QF_IDL", kIntegers, kConstants},
    {"QF_UFIDL", kRestrictedIntegers, kFunctions},
    {"QF_LIA", kIntegers, kConstants},
    {"QF_UFLIA", kIntegers, kFunctions},
    {"QF_AUFLIA", kArrays | kIntegers, kFunctions},
    {"QF_NIA", kIntegers, kConstants},
    {"QF_EIA", kIntegers, kConstants},
    {"QF_DT", kDatatypes, kConstants},
    {"QF_UFDT", kDatatypes, kFunctions},
    {"QF_UFBVDT", kBitVectors | kDatatypes, kFunctions},
    {"QF_FP", kFloats, kConstants},
    {"QF_UFFP", kFloats, kFunctions},
    {"QF_FPLRA", kFloats | kReals, kConstants},
    {"QF_BVFP", kBitVectors | kFloats, kConstants},
    {"QF_BVFPLRA", kBitVectors | kFloats | kReals, kConstants},
    {"QF_ABVFP", kArrays | kBitVectors | kFloats, kConstants},
    {"QF_AUFBVFP", kArrays | kBitVectors | kFloats, kFunctions},
    {"QF_ABVFPLRA", kArrays | kBitVectors | kFloats | kReals, kConstants},
    {"QF_ALIA", kArrays | kIntegers, kConstants},
    {"QF_ANIA", kArrays | kIntegers, kConstants},
    {"QF_AUFNIA", kArrays | kIntegers, kFunctions},
    {"QF_UFNIA", kIntegers, kFunctions},
    {"QF_UFDTLIA", kDatatypes | kIntegers, kFunctions},
    {"QF_UFDTNIA", kDatatypes | kIntegers, kFunctions},
    {"QF_UFDTLIRA", kDatatypes | kIntegers | kReals | kRealInt, kFunctions},
    {"QF_UFBVLIA", kBitVectors | kIntegers, kFunctions},
    {"QF_AUFBVLIA", kArrays | kBitVectors | kIntegers, kFunctions},
    {"QF_AUFBVNIA", kArrays | kBitVectors | kIntegers, kFunctions},
    {"QF_LIRA", kIntegers | kReals | kRealInt, kConstants},
    {"QF_NIRA", kIntegers | kReals | kRealInt, kConstants},
    {"QF_UFFPDTNIRA", kDatatypes | kFloats | kIntegers | kReals | kRealInt,
     kFunctions},
    {"QF_BVLRA", kBitVectors | kReals, kConstants},
    {"QF_UFLRA", kReals, kFunctions},
    {"QF_UFNRA", kReals, kFunctions},
    {"QF_LRA", kReals, kConstants},
    {"QF_NRA", kReals, kConstants},
    {"QF_RDL", kReals, kConstants},
    {"ALL", kArrays | kBitVectors | kDatatypes | kFloats | kIntegers,
     kFunctions},
});

Attribute ReadInfo(const Syntax& syntax, ChildRange args) {
  const auto key = syntax.Raw(args.front());
  const auto value = args.size() == 2 ? args[1] : kNoExpr;
  const auto raw = value != kNoExpr ? syntax.Raw(value) : std::string_view{};
  const auto type = [&](TokenKind kind) {
    return value != kNoExpr && syntax.Type(value) == kind;
  };
  const auto where =
      value != kNoExpr ? syntax.Span(value) : syntax.header.where;
  const auto need = [&](bool ok, std::string_view message) {
    if (!ok) Fail(DiagnosticCode::kInvalidInput, where, message);
  };

  if (key == ":smt-lib-version") {
    need(type(TokenKind::kDecimal), "Expected a decimal SMT-LIB version");
    if (raw != "2.6" && raw != "2.7")
      Fail(DiagnosticCode::kUnsupported, where, "Unsupported SMT-LIB version");
  } else if (key == ":source") {
    need(type(TokenKind::kString) || type(TokenKind::kQuoted),
         "Source must be a string or a quoted symbol");
  } else if (key == ":category") {
    need(type(TokenKind::kString) &&
             (raw == "\"crafted\"" || raw == "\"random\"" ||
              raw == "\"industrial\""),
         "Invalid benchmark category");
  } else if (key == ":license") {
    need(type(TokenKind::kString), "License must be a string");
  } else if (key == ":status") {
    need(type(TokenKind::kSymbol) &&
             (raw == "sat" || raw == "unsat" || raw == "unknown"),
         "Invalid benchmark status");
  }

  auto attribute_span = syntax.Span(args.front());
  if (value != kNoExpr) attribute_span.end = where.end;
  return {std::string(key),
          value != kNoExpr ? std::optional(syntax.Render(value)) : std::nullopt,
          attribute_span};
}

std::string_view ReadOption(std::string_view name, SessionOptions options) {
  const auto* option = FindOption(name);
  if (!option) return "unsupported";
  switch (option->id) {
    case OptionId::kPrintSuccess:
      return options.print_success ? "true" : "false";
    case OptionId::kProduceModels:
      return options.produce_models ? "true" : "false";
    case OptionId::kGlobalDeclarations:
      return options.global_declarations ? "true" : "false";
    case OptionId::kFixedFalse:
    case OptionId::kUnsupported:
      return option->default_value;
  }
  std::unreachable();
}

SessionOptions ChangeOption(SessionOptions options, SessionPhase phase,
                            const Syntax& syntax, ChildRange args) {
  const auto* option = FindOption(syntax.Raw(args[0]));
  const auto where = syntax.Span(args[0]);
  if (!option || option->id == OptionId::kUnsupported)
    Fail(DiagnosticCode::kUnsupported, where, "Unsupported option setting");
  const auto enabled = syntax.Is(args[1], "true");
  if (option->id == OptionId::kPrintSuccess) {
    options.print_success = enabled;
    return options;
  }
  if (phase != SessionPhase::kBeforeLogic)
    Fail(DiagnosticCode::kInvalidState, where,
         "Option must be set before the logic");
  if (option->id == OptionId::kProduceModels) options.produce_models = enabled;
  if (option->id == OptionId::kGlobalDeclarations)
    options.global_declarations = enabled;
  if (option->id == OptionId::kFixedFalse && enabled)
    Fail(DiagnosticCode::kUnsupported, syntax.Span(args[1]),
         "This option only accepts false");
  return options;
}
}  // namespace

const Profile* FindProfile(std::string_view name) noexcept {
  const auto found = std::ranges::find(kProfiles, name, &Profile::name);
  return found != kProfiles.end() ? &*found : nullptr;
}

void NodeFailure(NodeError error, SourceSpan where) {
  auto code = DiagnosticCode::kInvalidInput;
  std::string_view message = "Invalid term or sort";
  switch (error.code) {
    case ErrorCode::kResourceLimit:
      code = DiagnosticCode::kResource;
      message = "Node or numeric representation exhausted";
      if (error.resource == ResourceKind::kCppAllocation)
        message = "Memory allocation failed";
      if (error.resource == ResourceKind::kReferenceCount)
        message = "Reference count exhausted";
      break;
    case ErrorCode::kInvalidDatatype:
      switch (error.datatype_reason) {
        case DatatypeReason::kEmptyGroup:
          message = "Empty datatype group";
          break;
        case DatatypeReason::kNoConstructor:
          message = "Datatype has no constructor";
          break;
        case DatatypeReason::kInvalidParameterScope:
          message = "Datatype parameter outside its scope";
          break;
        case DatatypeReason::kNestedRecursiveSort:
          message =
              "Nested recursive datatype field is outside the input profile";
          code = DiagnosticCode::kUnsupported;
          break;
        case DatatypeReason::kNonproductive:
          message = "Datatype has no finite value";
          break;
        case DatatypeReason::kNone:
          message = "Invalid datatype";
          break;
      }
      break;
    case ErrorCode::kUnsupportedFeature:
      code = DiagnosticCode::kUnsupported;
      message = "Feature is outside the input profile";
      break;
    case ErrorCode::kInvalidArity:
      message = "Wrong argument count";
      break;
    case ErrorCode::kInvalidParameter:
      message = "Invalid index or parameter";
      break;
    case ErrorCode::kSortMismatch:
      message = "Incompatible operand or result sort";
      break;
    case ErrorCode::kInvalidLiteral:
      message = "Literal is outside its value domain";
      break;
    case ErrorCode::kNullObject:
    case ErrorCode::kForeignManager:
    case ErrorCode::kInvalidKind:
    case ErrorCode::kBuilderClosed:
    case ErrorCode::kInvalidTraversalState:
      code = DiagnosticCode::kEngineFailure;
      message = "Inconsistent frontend use of NodeManager";
      break;
  }
  throw Failure{{code, where, message, error}};
}

SessionState::SessionState(Scanner input, ParseOptions parse)
    : scanner(std::move(input)),
      manager(Take(NodeManager::Create(), scanner.Here())),
      building(new QueryContext(Take(manager.RetainState(), scanner.Here()))),
      parse_options(parse) {
#ifdef DANDELION_FRONTEND_TESTING
  scanner.Pool().stats = &stats;
#endif
}
const QueryContext& SessionState::Prefix() const noexcept {
  return sealed ? *sealed : *building;
}

void SessionState::Checkpoint() {
#ifdef DANDELION_FRONTEND_TESTING
  if (fail_after) {
    if (*fail_after == 0) {
      fail_after.reset();
      throw std::bad_alloc{};
    }
    --*fail_after;
  }
#endif
}

TypedCommand SessionState::Apply(Syntax& syntax, const ModelOwner* model) {
  const auto args = syntax.Children(0).subspan(1);
  const auto [kind, where] = syntax.header;
  TypedCommand command{syntax.header, {}, nullptr, {}, {}};
  if (kind == CommandKind::kUnsupported)
    Fail(DiagnosticCode::kUnsupported, where,
         "Command is outside the input profile");
  if (kind == CommandKind::kGetOption) {
    command.text = ReadOption(syntax.Raw(args[0]), options);
    return command;
  }
  if (kind == CommandKind::kGetInfo) {
    const auto key = syntax.Raw(args[0]);
    if (key == ":name")
      command.text = "(:name \"Dandelion\")";
    else if (key == ":version")
      command.text = "(:version \"development\")";
    else if (key == ":authors")
      command.text = "(:authors \"\")";
    else if (key == ":error-behavior")
      command.text = "(:error-behavior immediate-exit)";
    else if (key == ":reason-unknown") {
      if (phase != SessionPhase::kAfterCheck)
        Fail(DiagnosticCode::kInvalidState, where,
             "No unknown result is available");
      command.needs_unknown_reason = true;
    } else
      command.text = "unsupported";
    return command;
  }
  if (kind == CommandKind::kEcho) {
    command.text = syntax.Raw(args[0]);
    return command;
  }
  if (kind == CommandKind::kSetOption) {
    const auto next = ChangeOption(options, phase, syntax, args);
    if (next.print_success) command.text = "success";
    Checkpoint();
    options = next;
    return command;
  }
  if (kind != CommandKind::kCheckSat && kind != CommandKind::kGetModel &&
      kind != CommandKind::kGetValue && options.print_success)
    command.text = "success";

  switch (kind) {
    case CommandKind::kExit:
      phase = SessionPhase::kTerminated;
      break;
    case CommandKind::kSetInfo: {
      auto info = ReadInfo(syntax, args);
      Checkpoint();
      metadata.push_back(std::move(info));
      break;
    }
    case CommandKind::kSetLogic: {
      if (phase != SessionPhase::kBeforeLogic)
        Fail(DiagnosticCode::kInvalidState, where,
             "Logic has already been set");
      const auto* profile = FindProfile(syntax.Name(args[0]));
      if (!profile)
        Fail(DiagnosticCode::kUnsupported, syntax.Span(args[0]),
             "Unregistered logic");
      Checkpoint();
      building->signature_->profile = profile;
      building->logic_ = profile->name;
      phase = SessionPhase::kBuilding;
      break;
    }
    case CommandKind::kCheckSat:
      if (phase != SessionPhase::kBuilding)
        Fail(DiagnosticCode::kInvalidState, where,
             "Expected one check-sat after set-logic");
      Checkpoint();
      building->metadata_ = metadata;
      building->options_ = options;
      sealed = std::move(building);
      command.query = sealed;
      phase = SessionPhase::kAfterCheck;
      break;
    case CommandKind::kGetModel:
    case CommandKind::kGetValue:
      if (phase != SessionPhase::kAfterCheck || !options.produce_models)
        Fail(DiagnosticCode::kInvalidState, where,
             "Model queries require check-sat and produce-models");
      if (kind == CommandKind::kGetValue)
        ApplyValues(*this, syntax, command, model);
      break;
    default:
      if (phase != SessionPhase::kBuilding)
        Fail(DiagnosticCode::kInvalidState, where,
             "Command requires a logic and an unchecked query");
      ApplySemantic(syntax, command);
      break;
  }
  return command;
}

}  // namespace dandelion::frontend::detail

namespace dandelion::frontend {

bool operator==(const SourcePosition& left,
                const SourcePosition& right) noexcept {
  return left.offset == right.offset && left.line == right.line &&
         left.column == right.column;
}
bool operator==(const SourceSpan& left, const SourceSpan& right) noexcept {
  return left.source == right.source && left.begin == right.begin &&
         left.end == right.end;
}

QueryContext::QueryContext(ManagerRef manager)
    : manager_(std::move(manager)),
      signature_(std::make_unique<detail::Signature>()) {}
QueryContext::~QueryContext() {
  const std::array ranges{
      RootRange::From<&Assertion::term>(std::span(assertions_)),
      RootRange::From<&DefinedFunction::body>(std::span(definitions_)),
      RootRange::From<&NamedTerm::term>(std::span(named_terms_)),
      RootRange::From<&Annotation::term>(std::span(annotations_))};
  static_cast<void>(NodeManager::ReleaseRoots(manager_, ranges));
}
const DefinedFunction* QueryContext::FindDefinition(DeclId id) const noexcept {
  const auto found = signature_->definition_index.find(id);
  return found == signature_->definition_index.end()
             ? nullptr
             : &definitions_[found->second];
}
const DefinedFunction* QueryContext::FindDefinition(
    DeclView decl) const noexcept {
  const auto* definition = FindDefinition(decl.GetId());
  return definition && definition->decl.Borrow() == decl ? definition : nullptr;
}
std::optional<std::size_t> detail::Signature::FunctionOrdinal(
    const QueryContext& query, DeclView decl) noexcept {
  const auto& runs = Of(query).function_runs;
  const auto id = decl.GetId();
  const auto next = std::ranges::upper_bound(
      runs, id, {}, &std::pair<DeclId, std::size_t>::first);
  if (next == runs.begin()) return std::nullopt;
  const auto [first, start] = *std::prev(next);
  const auto delta = id - first;
  const auto end = next == runs.end() ? query.functions_.size() : next->second;
  if (delta >= end - start) return std::nullopt;
  const auto ordinal = start + static_cast<std::size_t>(delta);
  return query.functions_[ordinal].decl.Borrow() == decl
             ? std::optional(ordinal)
             : std::nullopt;
}
bool QueryContext::HasFunction(DeclView decl) const noexcept {
  return detail::Signature::FunctionOrdinal(*this, decl).has_value();
}

Session::Session(std::unique_ptr<detail::SessionState> state) noexcept
    : state_(std::move(state)) {}
Session::Session(Session&&) noexcept = default;
Session& Session::operator=(Session&&) noexcept = default;
Session::~Session() = default;
Outcome<Session> Session::FromText(std::string_view text,
                                   std::uint32_t source) {
  return FromText(text, ParseOptions{}, source);
}
Outcome<Session> Session::FromReader(Reader reader, std::uint32_t source) {
  return FromReader(std::move(reader), ParseOptions{}, source);
}
Outcome<Session> Session::FromText(std::string_view text, ParseOptions options,
                                   std::uint32_t source) {
  return detail::Guard({source, {}, {}}, [&] {
    return Session(std::make_unique<detail::SessionState>(
        detail::Scanner(text, source), options));
  });
}
Outcome<Session> Session::FromReader(Reader reader, ParseOptions options,
                                     std::uint32_t source) {
  return detail::Guard({source, {}, {}}, [&] {
    return Session(std::make_unique<detail::SessionState>(
        detail::Scanner(std::move(reader), source), options));
  });
}
Outcome<std::optional<CommandHeader>> Session::NextSyntax() {
  if (state_->phase == SessionPhase::kTerminated) return std::nullopt;
  if (state_->pending)
    return std::unexpected(Diagnostic{DiagnosticCode::kInvalidState,
                                      state_->pending->header.where,
                                      "A parsed command is already pending"});
  auto next = detail::Guard(state_->scanner.Here(),
                            [&] { state_->scanner.Next(state_->pending); });
  if (!next || !state_->pending) {
    state_->pending.reset();
    state_->phase = SessionPhase::kTerminated;
    if (!next) return std::unexpected(next.error());
    return std::nullopt;
  }
  return state_->pending->header;
}
Outcome<TypedCommand> Session::Apply(const ModelOwner* model) {
  if (!state_->pending)
    return std::unexpected(Diagnostic{DiagnosticCode::kInvalidState,
                                      state_->scanner.Here(),
                                      "No parsed command is pending"});
  auto& syntax = *state_->pending;
  auto result = detail::Guard(syntax.header.where, [&] {
    const auto* bound = state_->inspection_model.get();
    if (bound && model && model != bound)
      detail::Fail(DiagnosticCode::kModelFailure, syntax.header.where,
                   "Inspection session belongs to a different model");
    return state_->Apply(syntax, bound ? bound : model);
  });
  state_->pending.reset();
  return result;
}
SessionPhase Session::GetPhase() const noexcept { return state_->phase; }
const SessionOptions& Session::GetOptions() const noexcept {
  return state_->options;
}
std::span<const Attribute> Session::GetMetadata() const noexcept {
  return state_->metadata;
}
const QueryContext& Session::GetPrefix() const noexcept {
  return state_->Prefix();
}
#ifdef DANDELION_FRONTEND_TESTING
FrontendStats Session::GetStats() const noexcept { return state_->stats; }
void Session::ForceSyntaxPromotionThreshold(std::uint64_t threshold) noexcept {
  state_->scanner.Pool().threshold = threshold;
}
void Session::FailAllocationAfter(std::size_t checkpoints) noexcept {
  state_->fail_after = checkpoints;
}
#endif

}  // namespace dandelion::frontend
