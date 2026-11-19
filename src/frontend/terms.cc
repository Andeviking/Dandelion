module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {
namespace {

enum class Action {
  kEnter,
  kCall,
  kAnnotation,
  kLetBindings,
  kLetBody,
  kMatchStart,
  kMatchNext,
  kMatchBody
};
enum class HeadKind {
  kOperator,
  kLocal,
  kGlobal,
  kTester,
  kLiteral,
  kModel,
  kExcluded
};
using Literal = std::variant<bool, RoundingMode, FpSpecial, std::string_view>;

struct Call {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  HeadKind kind = HeadKind::kOperator;
  ExprId head;
  ChildRange args;
  std::optional<SortView> qualifier;
  std::vector<Integer> indices;
  const Operator* op = nullptr;
  std::size_t local = kNoExpr;
  TermBinding global;
  Literal literal = false;
  std::optional<Rational> rational;
  // NOLINTEND(readability-identifier-naming)
};

struct Local {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::string_view name;
  ExprId scope;
  ExprId escape;
  ExprId previous;
  Node value;
  DeclView selector;
  NodeView scrutinee;
  // NOLINTEND(readability-identifier-naming)
};

struct Frame {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  ExprId expr;
  std::size_t base;
  Action action = Action::kEnter;
  std::size_t next = 0;
  std::size_t mark = 0;
  // NOLINTEND(readability-identifier-naming)
};

// The first enclosing binder still needed by this source operand, or kNoExpr.
// Preorder puts every lexical ancestor before the subtree that refers to it.
struct Resolved {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  Node term;
  ExprId escape = kNoExpr;
  // NOLINTEND(readability-identifier-naming)
};

struct MatchBranch {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::optional<DatatypeMember> member;
  Node body;
  // NOLINTEND(readability-identifier-naming)
};

struct MatchState {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  NodeView scrutinee;
  SortConstructorView type;
  std::unordered_set<std::uint64_t> seen;
  std::vector<MatchBranch> effective;
  SortView result;
  std::optional<DatatypeMember> member;
  ExprId escape;
  std::uint64_t constructor_count = 0;
  std::size_t mark = 0;
  bool fallback = false;
  // NOLINTEND(readability-identifier-naming)
};

struct RecipeSortKey {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::uint64_t recipe;
  SortId sort;
  // NOLINTEND(readability-identifier-naming)
  friend bool operator==(RecipeSortKey, RecipeSortKey) = default;
};
struct RecipeSortHash {
  std::size_t operator()(RecipeSortKey key) const noexcept {
    const auto hash = std::hash<std::uint64_t>{}(key.recipe);
    return hash ^ (std::hash<SortId>{}(key.sort) + std::size_t{0x9e3779b9} +
                   (hash << 6) + (hash >> 2));
  }
};

constexpr auto kMaxWidth = std::numeric_limits<std::uint32_t>::max();
bool Digit(char value) noexcept { return value >= '0' && value <= '9'; }
bool IsBvNumeral(std::string_view name) noexcept {
  return name.starts_with("bv") && name.size() > 2 &&
         std::ranges::all_of(name.substr(2), Digit);
}
void CheckArity(std::size_t actual, std::uint64_t expected, SourceSpan where) {
  if (actual != expected)
    Fail(DiagnosticCode::kInvalidInput, where,
         "Wrong number of term arguments");
}
std::optional<NodeError> CheckOperatorIndices(
    Kind kind, std::span<const Integer> indices) {
  const auto below = [&](std::size_t index, std::int64_t minimum) {
    return indices[index].Borrow().Compare(IntegerView(minimum)) < 0;
  };
  const auto invalid = [](std::size_t index) -> NodeError {
    return {.code = ErrorCode::kInvalidParameter, .parameter_index = index};
  };
  switch (kind) {
    case Kind::kBvRepeat:
    case Kind::kIntDivisible:
    case Kind::kIntToBv:
    case Kind::kFpToSbv:
    case Kind::kFpToUbv:
      if (below(0, 1)) return invalid(0);
      break;
    case Kind::kFpFromBits:
    case Kind::kFpFromUbv:
      if (below(0, 2)) return invalid(0);
      if (below(1, 2)) return invalid(1);
      break;
    case Kind::kBvExtract: {
      const auto high = indices[0].Borrow();
      if (high.Compare(IntegerView::FromUnsigned(kMaxWidth)) >= 0)
        return invalid(0);
      if (indices[1].Borrow().Compare(high) > 0) return invalid(1);
      break;
    }
    default:
      break;
  }
  return std::nullopt;
}
void CheckSort(SortView actual, SortView expected, SourceSpan where) {
  if (actual != expected) NodeFailure({ErrorCode::kSortMismatch}, where);
}
bool IsDatatype(SortView sort) noexcept {
  return sort.GetKind() == SortKind::kDatatype;
}
std::vector<SortView> TypeArguments(SortView sort) {
  const auto range = sort.GetArguments();
  std::vector<SortView> args;
  args.reserve(range.size());
  for (auto argument : range) args.push_back(argument);
  return args;
}

class Elaborator {
 public:
  Elaborator(SessionState& session, const Syntax& syntax, Transaction* txn,
             TypedCommand& command, const ModelOwner* model);
  Node Resolve(ExprId root);
  void Formal(ExprId name, Node node);
  std::string Render(ExprId root) const {
    return syntax_.Render(root, &replacements_);
  }
  void ReserveName(std::string_view name) noexcept { reserved_ = name; }

 private:
  void Enter(Frame& frame);
  void Push(ExprId expr);
  void Complete(Resolved result);
  Call Prepare(ExprId expr, ExprId head, ChildRange args, bool application);
  Resolved FinishCall(ExprId expr, Call& call,
                      std::span<const Resolved> operands);
  Node TakeApplication(Result<Node> result, const Call& call, SourceSpan where);
  [[noreturn]] void ApplicationFailure(NodeError error, const Call& call,
                                       SourceSpan where);
  Node ApplyOrdinaryLiteral(ExprId expr);
  Node ApplyLiteral(const Call& call, SourceSpan where);
  Node ApplyOperator(ExprId expr, Call& call, std::span<NodeView> nodes);
  std::optional<Rational> RationalArgument(ExprId root);
  Node ApplyGlobal(const TermBinding& binding, const Call& call,
                   std::span<const NodeView> nodes, SourceSpan where);
  Node ApplyDatatype(DatatypeMember member, const Call& call,
                     std::span<const NodeView> nodes, SourceSpan where);
  Node ApplyTester(DatatypeMember member, NodeView operand, SourceSpan where);
  std::vector<SortView> Infer(DatatypeMember member, DeclView ctor,
                              std::optional<SortView> qualifier,
                              std::span<const NodeView> operands,
                              SourceSpan where);
  DatatypeGroupView Group(DatatypeMember member) const noexcept;
  std::optional<DatatypeMember> Constructor(
      std::string_view name) const noexcept;

  void PushLocal(ExprId name, ExprId scope, ExprId escape, Node node,
                 DeclView selector = {}, NodeView scrutinee = {});
  Node LocalValue(std::size_t binding);
  void Restore(std::size_t mark) noexcept;
  void UniqueBinders(ChildRange slots);
  void BindLet(ExprId expr, std::span<Resolved> values);
  void FinishAnnotation(ExprId expr, Resolved& result);

  void StartMatch(ExprId expr, const Resolved& scrutinee);
  ExprId BeginBranch(ExprId expr, ExprId branch);
  void FinishBranch(ExprId expr, ExprId field, Resolved result);
  Node FinishMatch(MatchState& match, SourceSpan where);

  SessionState& session_;
  const Syntax& syntax_;
  NodeManager& manager_;
  const Signature& signature_;
  const Profile& profile_;
  Transaction* txn_;
  TypedCommand& command_;
  const ModelOwner* model_;
  std::optional<std::string_view> reserved_;
  std::vector<Resolved> values_;
  std::vector<Frame> frames_;
  std::vector<Call> calls_;
  std::vector<Local> locals_;
  std::unordered_map<std::string_view, std::size_t> heads_;
  std::vector<MatchState> matches_;
  std::vector<NodeView> arguments_;
  std::unordered_map<std::size_t, Replacement> replacements_;
  const bool value_mode_;
};

Elaborator::Elaborator(SessionState& session, const Syntax& syntax,
                       Transaction* txn, TypedCommand& command,
                       const ModelOwner* model)
    : session_(session),
      syntax_(syntax),
      manager_(session.manager),
      signature_(session.GetSignature()),
      profile_(*signature_.profile),
      txn_(txn),
      command_(command),
      model_(model),
      value_mode_(command.header.kind == CommandKind::kGetValue) {}

void Elaborator::Push(ExprId expr) {
  frames_.push_back({expr, values_.size()});
}
void Elaborator::Complete(Resolved result) {
  values_.resize(frames_.back().base);
  frames_.pop_back();
  values_.push_back(std::move(result));
}
void Elaborator::Enter(Frame& frame) {
  const auto expr = frame.expr;
  if (!syntax_.List(expr) && syntax_.Type(expr) >= TokenKind::kNumeral) {
    Complete({ApplyOrdinaryLiteral(expr)});
    return;
  }
  auto head = expr;
  ChildRange args;
  bool application = false;
  if (syntax_.List(expr)) {
    const auto children = syntax_.Children(expr);
    head = children.front();
    if (syntax_.Is(head, "let")) {
      frame.action = Action::kLetBindings;
      return;
    }
    if (syntax_.Is(head, "match")) {
      frame.action = Action::kMatchStart;
      Push(children[1]);
      return;
    }
    if (syntax_.Is(head, "!")) {
      frame.action = Action::kAnnotation;
      Push(children[1]);
      return;
    }
    if (syntax_.Is(head, "forall") || syntax_.Is(head, "exists") ||
        syntax_.Is(head, "lambda"))
      Fail(DiagnosticCode::kUnsupported, syntax_.Span(expr),
           "Binder is outside the input profile");
    if (syntax_.Is(head, "_") || syntax_.Is(head, "as")) {
      head = expr;
    } else {
      application = true;
      args = children.subspan(1);
    }
  }
  auto call = Prepare(expr, head, args, application);
  if (args.empty()) {
    Complete(FinishCall(expr, call, {}));
    return;
  }
  calls_.push_back(std::move(call));
  frame.action = Action::kCall;
}

Call Elaborator::Prepare(ExprId expr, ExprId head, ChildRange args,
                         bool application) {
  const auto id = GetIdentifier(syntax_, head);
  const auto name = syntax_.Name(id.name);
  const auto where = syntax_.Span(head);
  const bool indexed = !id.indices.empty();
  const auto* descriptor = syntax_.Type(id.name) == TokenKind::kQuoted
                               ? GetOperator(ClassifyOperator(name))
                               : GetOperator(syntax_.Spelling(id.name));
  const auto* op = descriptor && indexed == (descriptor->indices != 0)
                       ? descriptor
                       : nullptr;
  const auto resolve_qualifier = [&]() -> std::optional<SortView> {
    if (id.qualifier == kNoExpr) return std::nullopt;
    const auto sort = ResolveSort(session_, syntax_, id.qualifier, txn_);
    return sort.Borrow();
  };
  Call call;
  call.head = head;
  call.args = args;
  if (application && args.empty() &&
      (indexed || (name != "and" && name != "or")))
    Fail(DiagnosticCode::kInvalidInput, syntax_.Span(expr),
         "Use a bare or qualified nullary identifier");

  if (IsReservedName(name)) {
    if (name == "@" && application && !indexed &&
        syntax_.Type(id.name) == TokenKind::kSymbol) {
      CheckArity(args.size(), 2, where);
      Fail(DiagnosticCode::kUnsupported, where,
           "Higher-order application is outside the input profile");
    }
    if (!value_mode_ || indexed || application || !AbstractOrdinal(name))
      Fail(DiagnosticCode::kInvalidInput, where, "Reserved term name");
    if (!model_)
      Fail(DiagnosticCode::kModelDependent, where,
           "Abstract value requires its model");
    call.kind = HeadKind::kModel;
    call.qualifier = resolve_qualifier();
    return call;
  }
  if (!indexed) {
    const bool active_operator =
        op &&
        (profile_.Has(op->features) ||
         (profile_.Has(kRestrictedIntegers) && IsRestrictedInt(op->kind)));
    const bool boolean = name == "true" || name == "false";
    const auto rm = op ? std::nullopt : FindRoundingMode(name);
    if (!active_operator && !boolean && !(rm && profile_.Has(kFloats))) {
      if (const auto it = heads_.find(name); it != heads_.end()) {
        CheckArity(args.size(), 0, where);
        if (id.qualifier != kNoExpr)
          Fail(DiagnosticCode::kInvalidInput, where,
               "A lexical binding cannot be qualified");
        call.kind = HeadKind::kLocal;
        call.local = it->second;
        return call;
      }
      if (const auto it = signature_.terms.find(name); it != nullptr) {
        call.kind = HeadKind::kGlobal;
        call.global = it->second;
        if (const auto* ordinary = std::get_if<DeclInstanceView>(&call.global))
          CheckArity(args.size(), ordinary->GetDomain().size(), where);
        else if (std::holds_alternative<std::size_t>(call.global))
          CheckArity(args.size(), 0, where);
        call.qualifier = resolve_qualifier();
        return call;
      }
    }
    if (boolean) {
      CheckArity(args.size(), 0, where);
      call.kind = HeadKind::kLiteral;
      call.literal = name == "true";
      call.qualifier = resolve_qualifier();
      return call;
    }
    if (rm) {
      CheckArity(args.size(), 0, where);
      if (!profile_.Has(kFloats))
        Fail(DiagnosticCode::kUnsupported, where,
             "Rounding mode outside the active signature");
      call.kind = HeadKind::kLiteral;
      call.literal = *rm;
      call.qualifier = resolve_qualifier();
      return call;
    }
  } else {
    if (name == "is") {
      CheckArity(id.indices.size(), 1, where);
      if (!syntax_.IsSymbol(id.indices.front()))
        Fail(DiagnosticCode::kInvalidInput, syntax_.Span(id.indices.front()),
             "Tester needs a constructor name");
      CheckArity(args.size(), 1, where);
      if (!profile_.Has(kDatatypes))
        Fail(DiagnosticCode::kUnsupported, where,
             "Datatype tester outside the active signature");
      const auto member = Constructor(syntax_.Name(id.indices.front()));
      if (!member)
        Fail(DiagnosticCode::kInvalidInput, syntax_.Span(id.indices.front()),
             "Unknown datatype constructor");
      call.kind = HeadKind::kTester;
      call.global = *member;
      call.qualifier = resolve_qualifier();
      return call;
    }
    if (const auto special = FindFpSpecial(name)) {
      CheckArity(args.size(), 0, where);
      call.indices = NumericIndices(syntax_, id, 2, where);
      CheckMinimumIndex(syntax_, id, call.indices, 0, 2);
      CheckMinimumIndex(syntax_, id, call.indices, 1, 2);
      if (!profile_.Has(kFloats))
        Fail(DiagnosticCode::kUnsupported, where,
             "Floating-point literal outside the active signature");
      call.kind = HeadKind::kLiteral;
      call.literal = *special;
      call.qualifier = resolve_qualifier();
      return call;
    }
    if (IsBvNumeral(name)) {
      CheckArity(args.size(), 0, where);
      call.indices = NumericIndices(syntax_, id, 1, where);
      CheckMinimumIndex(syntax_, id, call.indices, 0, 1);
      if (!profile_.Has(kFloats) && !profile_.Has(kBitVectors))
        Fail(DiagnosticCode::kUnsupported, where,
             "Bit-vector literal outside the active signature");
      call.kind = HeadKind::kLiteral;
      call.literal = name.substr(2);
      call.qualifier = resolve_qualifier();
      return call;
    }
  }

  if (op) {
    call.op = op;
    call.indices = NumericIndices(syntax_, id, op->indices, where);
    if (const auto error = CheckOperatorIndices(op->kind, call.indices))
      ApplicationFailure(*error, call, where);
    if (!application)
      Fail(DiagnosticCode::kInvalidInput, where,
           "Operator requires an application");
    if (args.size() < op->min_arity || args.size() > op->max_arity)
      NodeFailure({ErrorCode::kInvalidArity}, syntax_.Span(expr));
    if (op->kind == Kind::kArrayConst && id.qualifier == kNoExpr)
      NodeFailure({ErrorCode::kInvalidParameter}, where);
    if (!profile_.Has(op->features) &&
        !(profile_.Has(kRestrictedIntegers) && IsRestrictedInt(op->kind)))
      Fail(DiagnosticCode::kUnsupported, where,
           "Operator is outside the active signature");
  } else if (descriptor || FindFpSpecial(name) || IsBvNumeral(name) ||
             name == "is") {
    Fail(DiagnosticCode::kInvalidInput, where,
         "Wrong identifier form for this operator");
  } else if (IsKnownExcludedTerm(name)) {
    const std::size_t indices = name == "re.^" ? 1 : name == "re.loop" ? 2 : 0;
    if (id.indices.size() != indices)
      Fail(DiagnosticCode::kInvalidInput, where,
           "Wrong identifier form for this operator");
    for (auto index : id.indices) {
      if (syntax_.Type(index) != TokenKind::kNumeral)
        Fail(DiagnosticCode::kInvalidInput, syntax_.Span(index),
             "Expected a numeral index");
    }
    call.kind = HeadKind::kExcluded;
  } else {
    Fail(DiagnosticCode::kInvalidInput, where, "Unknown term name");
  }
  call.qualifier = resolve_qualifier();
  return call;
}

Node Elaborator::ApplyOrdinaryLiteral(ExprId expr) {
  const auto where = syntax_.Span(expr);
  const auto raw = syntax_.Raw(expr);
  const auto type = syntax_.Type(expr);
  if (type == TokenKind::kNumeral) {
    if (!profile_.Has(kIntegers) && !profile_.Has(kRestrictedIntegers))
      Fail(DiagnosticCode::kUnsupported, where,
           "Integer numeral outside the active signature");
    return session_.NumericLiteral(raw, 10, {}, where);
  }
  if (type == TokenKind::kBinary || type == TokenKind::kHexadecimal) {
    if (!profile_.Has(kBitVectors) && !profile_.Has(kFloats))
      Fail(DiagnosticCode::kUnsupported, where,
           "Bit-vector literal outside the active signature");
    const auto digits = raw.substr(2);
    const unsigned factor = type == TokenKind::kBinary ? 1 : 4;
    if (digits.size() > kMaxWidth / factor)
      NodeFailure({ErrorCode::kResourceLimit, ResourceKind::kSize}, where);
    const auto sort = Take(
        manager_.MakeBvSort(IntegerView::FromUnsigned(digits.size() * factor)),
        where);
    return session_.NumericLiteral(digits, factor == 1 ? 2 : 16, sort.Borrow(),
                                   where);
  }
  Fail(DiagnosticCode::kUnsupported, where,
       "Real or String term is outside the input profile");
}

Node Elaborator::ApplyLiteral(const Call& call, SourceSpan where) {
  if (const auto* boolean = std::get_if<bool>(&call.literal))
    return Take(manager_.MakeBool(*boolean), where);
  if (const auto* rm = std::get_if<RoundingMode>(&call.literal))
    return Take(manager_.MakeRoundingMode(*rm), where);
  if (const auto* special = std::get_if<FpSpecial>(&call.literal)) {
    const auto sort = Take(
        manager_.MakeFpSort(call.indices[0].Borrow(), call.indices[1].Borrow()),
        where);
    return Take(manager_.MakeFpSpecial(sort.Borrow(), *special), where);
  }
  const auto sort = Take(manager_.MakeBvSort(call.indices[0].Borrow()), where);
  return session_.NumericLiteral(std::get<std::string_view>(call.literal), 10,
                                 sort.Borrow(), where);
}

std::optional<Rational> Elaborator::RationalArgument(ExprId root) {
  std::vector<ExprId> order;
  std::vector<ExprId> work{root};
  while (!work.empty()) {
    const auto id = work.back();
    work.pop_back();
    order.push_back(id);
    if (syntax_.Type(id) == TokenKind::kNumeral ||
        syntax_.Type(id) == TokenKind::kDecimal)
      continue;
    const auto children = syntax_.Children(id);
    if (children.size() < 2 || !syntax_.IsSymbol(children[0]))
      return std::nullopt;
    const auto name = syntax_.Name(children[0]);
    if (heads_.contains(name) || signature_.terms.contains(name))
      return std::nullopt;
    if (name == "-" && children.size() == 2) {
      work.push_back(children[1]);
    } else if (name == "/" && children.size() == 3) {
      work.push_back(children[1]);
      work.push_back(children[2]);
    } else {
      return std::nullopt;
    }
  }
  std::vector<Rational> values;
  for (auto id : order | std::views::reverse) {
    const auto where = syntax_.Span(id);
    if (syntax_.Type(id) == TokenKind::kDecimal) {
      values.push_back(
          Take(Rational::ParseDecimalFraction(syntax_.Raw(id)), where));
    } else if (syntax_.Type(id) == TokenKind::kNumeral) {
      const auto value = Take(Integer::ParseDecimal(syntax_.Raw(id)), where);
      values.push_back(
          Take(Rational::Make(value.Borrow(), IntegerView(1)), where));
    } else if (syntax_.Children(id).size() == 2) {
      values.back() = Take(Rational::Negate(values.back().Borrow()), where);
    } else {
      const auto divisor = std::move(values.back());
      values.pop_back();
      if (divisor.Borrow().Numerator().Sign() == 0)
        NodeFailure({ErrorCode::kUnsupportedFeature},
                    syntax_.Span(syntax_.Children(id)[2]));
      values.back() = Take(
          Rational::Divide(values.back().Borrow(), divisor.Borrow()), where);
    }
  }
  return std::move(values.back());
}

Node Elaborator::ApplyOperator(ExprId expr, Call& call,
                               std::span<NodeView> nodes) {
  auto kind = call.op->kind;
  const auto where = syntax_.Span(expr);
  if ((kind == Kind::kAnd || kind == Kind::kOr) && nodes.size() <= 1) {
    if (nodes.empty())
      return Take(manager_.MakeBool(kind == Kind::kAnd), where);
    if (nodes[0].GetSort().GetKind() != SortKind::kBool)
      NodeFailure({ErrorCode::kSortMismatch}, syntax_.Span(call.args[0]));
    return Take(manager_.RetainNode(nodes[0]), where);
  }
  if (IsRestrictedInt(kind) && profile_.Has(kRestrictedIntegers) &&
      nodes.size() != 2) {
    if (kind == Kind::kIntSub && value_mode_ && call.args.size() == 1 &&
        !call.qualifier && syntax_.Type(call.args[0]) == TokenKind::kNumeral &&
        syntax_.Raw(call.args[0]) != "0") {
      const auto negative = Take(
          Integer::Negate(nodes[0].GetValue()->GetInteger().value()), where);
      return Take(manager_.MakeInt(negative.Borrow()), where);
    }
    Fail(DiagnosticCode::kUnsupported, where,
         "Integer rank outside the restricted signature");
  }
  if (kind == Kind::kIntSub && nodes.size() == 1) kind = Kind::kIntNeg;
  if (kind == Kind::kFpFromBits && call.args.size() == 2) {
    if (call.rational)
      kind = Kind::kFpFromRational;
    else if (nodes[1].GetSort().GetKind() == SortKind::kFloatingPoint)
      kind = Kind::kFpConvert;
    else
      kind = Kind::kFpFromSbv;
  }
  const auto rational =
      call.rational ? std::optional(call.rational->Borrow()) : std::nullopt;
  const auto qualifier =
      kind == Kind::kArrayConst ? call.qualifier : std::nullopt;
  const BuiltinOpView operation{kind, call.indices, qualifier, rational};
  const auto operands = session_.parse_options.canonicalize_commutative
                            ? CommutativeOperands(kind, nodes)
                            : std::span<NodeView>{};
  const bool reordered =
      !std::ranges::is_sorted(operands, {}, &NodeView::GetId);
  if (reordered) {
    const auto checked = manager_.CheckBuiltin(operation, nodes);
    if (!checked) ApplicationFailure(checked.error(), call, where);
    OrderCommutative(operands);
  }
  auto term =
      TakeApplication(manager_.MakeBuiltin(operation, nodes), call, where);
  if (reordered) session_.Count(&FrontendStats::commutative_canonicalizations);
  return term;
}

Node Elaborator::TakeApplication(Result<Node> result, const Call& call,
                                 SourceSpan where) {
  if (!result) ApplicationFailure(result.error(), call, where);
  return std::move(*result);
}

void Elaborator::ApplicationFailure(NodeError error, const Call& call,
                                    SourceSpan where) {
  if (error.operand_index < call.args.size()) {
    where = syntax_.Span(call.args[error.operand_index]);
  } else if (error.parameter_index < call.indices.size()) {
    const auto indices = GetIdentifier(syntax_, call.head).indices;
    where = syntax_.Span(indices[error.parameter_index]);
  }
  NodeFailure(error, where);
}

Resolved Elaborator::FinishCall(ExprId expr, Call& call,
                                std::span<const Resolved> operands) {
  const auto where = syntax_.Span(expr);
  if (call.kind == HeadKind::kLocal) {
    const auto& local = locals_[call.local];
    return {LocalValue(call.local), std::min(local.scope, local.escape)};
  }
  if (call.kind == HeadKind::kExcluded)
    Fail(DiagnosticCode::kUnsupported, where,
         "Operator is outside the input profile");

  arguments_.clear();
  auto escape = kNoExpr;
  for (const auto& operand : operands) {
    escape = std::min(escape, operand.escape);
    arguments_.push_back(operand.term.Borrow());
  }
  Node term;
  switch (call.kind) {
    case HeadKind::kOperator:
      term = ApplyOperator(expr, call, arguments_);
      break;
    case HeadKind::kLiteral:
      term = ApplyLiteral(call, where);
      break;
    case HeadKind::kGlobal:
      term = ApplyGlobal(call.global, call, arguments_, where);
      break;
    case HeadKind::kTester:
      term = ApplyTester(std::get<DatatypeMember>(call.global), arguments_[0],
                         syntax_.Span(call.args[0]));
      break;
    case HeadKind::kModel: {
      const auto id = GetIdentifier(syntax_, call.head);
      auto bound =
          ResolveModelReference(*model_, session_.Prefix(),
                                syntax_.Name(id.name), call.qualifier, where);
      const auto& entry = syntax_.GetExpression(call.head);
      replacements_.emplace(
          entry.token, Replacement{entry.last_token, std::move(bound.second)});
      return {std::move(bound.first), escape};
    }
    case HeadKind::kLocal:
    case HeadKind::kExcluded:
      std::unreachable();
  }
  if (call.qualifier && (!call.op || call.op->kind != Kind::kArrayConst))
    CheckSort(term.Borrow().GetSort(), *call.qualifier, where);
  return {std::move(term), escape};
}

DatatypeGroupView Elaborator::Group(DatatypeMember member) const noexcept {
  return session_.Prefix().GetDatatypeGroups()[member.group].Borrow();
}
std::optional<DatatypeMember> Elaborator::Constructor(
    std::string_view name) const noexcept {
  const auto it = signature_.terms.find(name);
  if (it != nullptr) {
    const auto* member = std::get_if<DatatypeMember>(&it->second);
    if (member && !member->field) return *member;
  }
  return std::nullopt;
}

std::vector<SortView> Elaborator::Infer(DatatypeMember member, DeclView ctor,
                                        std::optional<SortView> qualifier,
                                        std::span<const NodeView> operands,
                                        SourceSpan where) {
  std::vector<SortView> types(ctor.GetParameterCount());
  if (types.empty()) return types;
  struct Pair {
    // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
    SortRecipeView recipe;
    SortView actual;
    bool checked = false;
    // NOLINTEND(readability-identifier-naming)
  };
  const auto group = Group(member);
  std::vector<Pair> stack;
  std::unordered_set<RecipeSortKey, RecipeSortHash> matched;
  if (qualifier) stack.push_back({ctor.GetRangeRecipe().value(), *qualifier});
  for (std::size_t i = operands.size(); i > 0; --i)
    stack.push_back(
        {Take(ctor.GetDomainRecipe(i - 1), where), operands[i - 1].GetSort()});
  while (!stack.empty()) {
    const auto [recipe, actual, checked] = stack.back();
    stack.pop_back();
    const RecipeSortKey key{recipe.GetId(), actual.GetId()};
    if (checked) {
      matched.insert(key);
      continue;
    }
    if (matched.contains(key)) continue;
    session_.Count(&FrontendStats::recipe_pairs);
    if (recipe.GetKind() == SortRecipeKind::kParameter) {
      auto& binding = types[recipe.GetParameter()->second];
      if (!binding.IsNull())
        CheckSort(actual, binding, where);
      else
        binding = actual;
      matched.insert(key);
      continue;
    }
    if (recipe.GetKind() == SortRecipeKind::kConcrete) {
      CheckSort(actual, *recipe.GetConcrete(), where);
      matched.insert(key);
      continue;
    }
    if (recipe.GetKind() == SortRecipeKind::kArray) {
      if (actual.GetKind() != SortKind::kArray)
        NodeFailure({ErrorCode::kSortMismatch}, where);
      stack.push_back({recipe, actual, true});
      stack.push_back(
          {Take(recipe.Argument(1), where), *actual.GetArrayElement()});
      stack.push_back(
          {Take(recipe.Argument(0), where), *actual.GetArrayIndex()});
      continue;
    }
    const auto expected =
        recipe.GetKind() == SortRecipeKind::kRecursive
            ? Take(group.GetSortConstructor(*recipe.GetRecursiveSlot()), where)
            : *recipe.GetConstructor();
    if (actual.GetSortConstructor() != expected)
      NodeFailure({ErrorCode::kSortMismatch}, where);
    stack.push_back({recipe, actual, true});
    const auto args = actual.GetArguments();
    for (std::size_t i = args.size(); i > 0; --i)
      stack.push_back({Take(recipe.Argument(i - 1), where), args[i - 1]});
  }
  if (std::ranges::any_of(types, &SortView::IsNull))
    Fail(DiagnosticCode::kInvalidInput, where,
         "Ambiguous datatype constructor needs a result qualifier");
  return types;
}

Node Elaborator::ApplyDatatype(DatatypeMember member, const Call& call,
                               std::span<const NodeView> nodes,
                               SourceSpan where) {
  const auto group = Group(member);
  if (member.field) {
    CheckArity(nodes.size(), 1, where);
    const auto operand = nodes[0].GetSort();
    if (operand.GetSortConstructor() !=
        Take(group.GetSortConstructor(member.datatype), where))
      NodeFailure({.code = ErrorCode::kSortMismatch, .operand_index = 0},
                  syntax_.Span(call.args[0]));
    const auto selector = Take(
        group.GetSelector(member.datatype, member.ctor, *member.field), where);
    const auto instance =
        Take(manager_.InstantiateDeclaration(selector, TypeArguments(operand)),
             where);
    return TakeApplication(manager_.MakeApply(instance.Borrow(), nodes), call,
                           where);
  }
  CheckArity(nodes.size(),
             Take(group.GetFieldCount(member.datatype, member.ctor), where),
             where);
  const auto ctor =
      Take(group.GetConstructor(member.datatype, member.ctor), where);
  const auto actuals = Infer(member, ctor, call.qualifier, nodes, where);
  const auto instance =
      Take(manager_.InstantiateDeclaration(ctor, actuals), where);
  return TakeApplication(manager_.MakeApply(instance.Borrow(), nodes), call,
                         where);
}
Node Elaborator::ApplyTester(DatatypeMember member, NodeView operand,
                             SourceSpan where) {
  const auto group = Group(member);
  if (operand.GetSort().GetSortConstructor() !=
      Take(group.GetSortConstructor(member.datatype), where))
    NodeFailure({ErrorCode::kSortMismatch}, where);
  const auto tester =
      Take(group.GetTester(member.datatype, member.ctor), where);
  const auto instance = Take(
      manager_.InstantiateDeclaration(tester, TypeArguments(operand.GetSort())),
      where);
  const std::array args{operand};
  return Take(manager_.MakeApply(instance.Borrow(), args), where);
}
Node Elaborator::ApplyGlobal(const TermBinding& binding, const Call& call,
                             std::span<const NodeView> nodes,
                             SourceSpan where) {
  if (const auto* instance = std::get_if<DeclInstanceView>(&binding)) {
#ifdef DANDELION_FRONTEND_TESTING
    if (session_.Prefix().FindDefinition(instance->GetDeclaration().GetId()))
      session_.Count(&FrontendStats::defined_calls);
#endif
    return TakeApplication(manager_.MakeApply(*instance, nodes), call, where);
  }
  if (const auto* member = std::get_if<DatatypeMember>(&binding))
    return ApplyDatatype(*member, call, nodes, where);
  return session_.Prefix().GetNamedTerms()[std::get<std::size_t>(binding)].term;
}

void Elaborator::PushLocal(ExprId name, ExprId scope, ExprId escape, Node node,
                           DeclView selector, NodeView scrutinee) {
  const auto spelling = syntax_.Name(name);
  const auto it = heads_.find(spelling);
  const auto previous = it == heads_.end() ? kNoExpr : it->second;
  const auto binding = locals_.size();
  locals_.push_back({spelling, scope, escape, previous, std::move(node),
                     selector, scrutinee});
  heads_.insert_or_assign(spelling, binding);
}
Node Elaborator::LocalValue(std::size_t binding) {
  auto& local = locals_[binding];
  if (local.value.Borrow().IsNull()) {
    const auto where = syntax_.Span(local.scope);
    const auto instance =
        Take(manager_.InstantiateDeclaration(
                 local.selector, TypeArguments(local.scrutinee.GetSort())),
             where);
    const std::array args{local.scrutinee};
    local.value = Take(manager_.MakeApply(instance.Borrow(), args), where);
  }
  return local.value;
}
void Elaborator::Restore(std::size_t mark) noexcept {
  while (locals_.size() != mark) {
    const auto& local = locals_.back();
    const auto it = heads_.find(local.name);
    if (local.previous == kNoExpr)
      heads_.erase(it);
    else
      it->second = local.previous;
    locals_.pop_back();
  }
}
void Elaborator::Formal(ExprId name, Node node) {
  CheckTermName(session_, syntax_, name, false);
  if (heads_.contains(syntax_.Name(name)))
    Fail(DiagnosticCode::kInvalidInput, syntax_.Span(name),
         "Duplicate function formal");
  PushLocal(name, 0, kNoExpr, std::move(node));
}
void Elaborator::UniqueBinders(ChildRange slots) {
  std::unordered_set<std::string_view> names;
  for (const auto slot : slots) {
    if (syntax_.Is(slot, "_")) continue;
    CheckTermName(session_, syntax_, slot, false);
    if (!names.insert(syntax_.Name(slot)).second)
      Fail(DiagnosticCode::kInvalidInput, syntax_.Span(slot),
           "Duplicate local name");
  }
}
void Elaborator::BindLet(ExprId expr, std::span<Resolved> values) {
  const auto bindings = syntax_.Children(syntax_.Children(expr)[1]);
  std::unordered_set<std::string_view> names;
  for (auto binding : bindings) {
    const auto name = syntax_.Children(binding)[0];
    CheckTermName(session_, syntax_, name, false);
    if (!names.insert(syntax_.Name(name)).second)
      Fail(DiagnosticCode::kInvalidInput, syntax_.Span(name),
           "Duplicate local name");
  }
  std::size_t i = 0;
  for (auto binding : bindings) {
    auto& rhs = values[i++];
    PushLocal(syntax_.Children(binding)[0], expr, rhs.escape,
              std::move(rhs.term));
  }
}

void Elaborator::FinishAnnotation(ExprId expr, Resolved& result) {
  const auto fields = syntax_.Children(expr);
  Annotation annotation{result.term, {}, syntax_.Span(expr)};
  auto label = kNoExpr;
  for (std::size_t i = 2; i < fields.size(); ++i) {
    const auto key_id = fields[i];
    const auto key = syntax_.Raw(key_id);
    auto value = kNoExpr;
    if (i + 1 < fields.size() &&
        syntax_.Type(fields[i + 1]) != TokenKind::kKeyword)
      value = fields[++i];
    if (key == ":pattern")
      Fail(DiagnosticCode::kUnsupported, syntax_.Span(key_id),
           "Patterns are outside the input profile");
    if (key == ":named") {
      if (value_mode_)
        Fail(DiagnosticCode::kUnsupported, syntax_.Span(key_id),
             "Naming a model request term is unsupported");
      if (label != kNoExpr || value == kNoExpr || !syntax_.IsSymbol(value))
        Fail(DiagnosticCode::kInvalidInput, syntax_.Span(key_id),
             "Expected one fresh name");
      CheckTermName(session_, syntax_, value, true);
      if (syntax_.Name(value) == reserved_)
        Fail(DiagnosticCode::kInvalidInput, syntax_.Span(value),
             "Name is reserved for this definition");
      label = value;
    }
    auto where = syntax_.Span(key_id);
    if (value != kNoExpr) where.end = syntax_.Span(value).end;
    annotation.attributes.push_back(
        {std::string(key),
         value != kNoExpr ? std::optional(syntax_.Render(value)) : std::nullopt,
         where});
  }
  if (label != kNoExpr) {
    session_.Count(&FrontendStats::closure_visits);
    if (result.escape != kNoExpr)
      Fail(DiagnosticCode::kInvalidInput, syntax_.Span(fields[1]),
           "Named term has an external lexical dependency");
    txn_->AddNamed(syntax_.Name(label), result.term, syntax_.Span(label));
  }
  if (txn_)
    txn_->AddAnnotation(std::move(annotation));
  else
    command_.annotations.push_back(std::move(annotation));
}

void Elaborator::StartMatch(ExprId expr, const Resolved& value) {
  const auto scrutinee = value.term.Borrow();
  if (!profile_.Has(kDatatypes))
    Fail(DiagnosticCode::kUnsupported, syntax_.Span(expr),
         "Match outside the active signature");
  const auto sort = scrutinee.GetSort();
  if (!IsDatatype(sort))
    Fail(DiagnosticCode::kInvalidInput, syntax_.Span(syntax_.Children(expr)[1]),
         "Match requires a datatype");
  matches_.push_back(
      {scrutinee, *sort.GetSortConstructor(), {}, {}, {}, {}, value.escape});
}
ExprId Elaborator::BeginBranch(ExprId expr, ExprId entry) {
  auto& match = matches_.back();
  const auto branch = syntax_.Children(entry);
  const auto pattern = branch[0];
  const auto fields = syntax_.Children(pattern);
  const auto where = syntax_.Span(pattern);
  const auto head = fields.empty() ? pattern : fields[0];
  const auto name = syntax_.Name(head);
  auto member = syntax_.Is(head, "_") ? std::nullopt : Constructor(name);
  if (member && Take(Group(*member).GetSortConstructor(member->datatype),
                     where) != match.type)
    member.reset();

  match.mark = locals_.size();
  if (member) {
    const auto group = Group(*member);
    const auto count =
        Take(group.GetFieldCount(member->datatype, member->ctor), where);
    const auto slots = fields.empty() ? fields : fields.subspan(1);
    CheckArity(slots.size(), count, where);
    UniqueBinders(slots);
    for (std::size_t i = 0; i < slots.size(); ++i) {
      if (syntax_.Is(slots[i], "_")) continue;
      const auto selector =
          Take(group.GetSelector(member->datatype, member->ctor, i), where);
      PushLocal(slots[i], expr, kNoExpr, {}, selector, match.scrutinee);
    }
  } else if (!fields.empty()) {
    Fail(DiagnosticCode::kInvalidInput, where,
         "Pattern constructor belongs to another datatype or is unknown");
  } else if (!syntax_.Is(pattern, "_")) {
    CheckTermName(session_, syntax_, pattern, false);
    PushLocal(pattern, expr, kNoExpr,
              Take(manager_.RetainNode(match.scrutinee), where));
  }
  match.member = member;
  return branch[1];
}
void Elaborator::FinishBranch(ExprId expr, ExprId field, Resolved result) {
  auto& match = matches_.back();
  Restore(match.mark);
  const auto member = match.member;
  match.escape = std::min(match.escape, result.escape);
  const auto sort = result.term.Borrow().GetSort();
  if (match.result.IsNull())
    match.result = sort;
  else
    CheckSort(sort, match.result, syntax_.Span(field));

  if (!match.fallback &&
      (match.seen.empty() || match.seen.size() < match.constructor_count)) {
    if (member) {
      if (match.seen.empty())
        match.constructor_count =
            Take(Group(*member).GetConstructorCount(member->datatype),
                 syntax_.Span(expr));
      if (match.seen.insert(member->ctor).second)
        match.effective.push_back({member, std::move(result.term)});
    } else {
      match.fallback = true;
      match.effective.push_back({member, std::move(result.term)});
    }
  }
}
Node Elaborator::FinishMatch(MatchState& match, SourceSpan where) {
  if (!match.fallback && match.seen.size() != match.constructor_count)
    Fail(DiagnosticCode::kInvalidInput, where, "Nonexhaustive match");
  auto result = std::move(match.effective.back().body);
  for (std::size_t i = match.effective.size() - 1; i > 0; --i) {
    const auto& branch = match.effective[i - 1];
    const auto test = ApplyTester(*branch.member, match.scrutinee, where);
    const std::array args{test.Borrow(), branch.body.Borrow(), result.Borrow()};
    result = Take(manager_.MakeBuiltin({Kind::kIte, {}, {}, {}}, args), where);
  }
  return result;
}

Node Elaborator::Resolve(ExprId root) {
  Push(root);
  while (!frames_.empty()) {
    auto& frame = frames_.back();
    const auto expr = frame.expr;
    switch (frame.action) {
      case Action::kEnter:
        Enter(frame);
        break;
      case Action::kCall: {
        auto& call = calls_.back();
        if (frame.next == call.args.size()) {
          auto result =
              FinishCall(expr, call, std::span(values_).subspan(frame.base));
          calls_.pop_back();
          Complete(std::move(result));
          break;
        }
        const auto index = frame.next++;
        const auto arg = call.args[index];
        if (index == 1 && call.op && call.op->kind == Kind::kFpFromBits &&
            call.args.size() == 2) {
          call.rational = RationalArgument(arg);
          if (call.rational) break;
        }
        Push(arg);
        break;
      }
      case Action::kAnnotation: {
        auto result = std::move(values_.back());
        FinishAnnotation(expr, result);
        Complete(std::move(result));
        break;
      }
      case Action::kLetBindings: {
        const auto fields = syntax_.Children(expr);
        const auto bindings = syntax_.Children(fields[1]);
        if (frame.next < bindings.size()) {
          Push(syntax_.Children(bindings[frame.next++])[1]);
          break;
        }
        frame.mark = locals_.size();
        BindLet(expr, std::span(values_).subspan(frame.base));
        values_.resize(frame.base);
        frame.action = Action::kLetBody;
        Push(fields[2]);
        break;
      }
      case Action::kLetBody: {
        auto result = std::move(values_.back());
        Restore(frame.mark);
        if (result.escape >= expr) result.escape = kNoExpr;
        Complete(std::move(result));
        break;
      }
      case Action::kMatchStart:
        StartMatch(expr, values_.back());
        frame.action = Action::kMatchNext;
        break;
      case Action::kMatchNext: {
        const auto branches = syntax_.Children(syntax_.Children(expr)[2]);
        if (frame.next < branches.size()) {
          const auto branch = branches[frame.next++];
          frame.action = Action::kMatchBody;
          Push(BeginBranch(expr, branch));
          break;
        }
        auto& match = matches_.back();
        const auto escape = match.escape < expr ? match.escape : kNoExpr;
        auto term = FinishMatch(match, syntax_.Span(expr));
        matches_.pop_back();
        Complete({std::move(term), escape});
        break;
      }
      case Action::kMatchBody: {
        const auto branches = syntax_.Children(syntax_.Children(expr)[2]);
        const auto field = syntax_.Children(branches[frame.next - 1])[1];
        FinishBranch(expr, field, std::move(values_.back()));
        values_.pop_back();
        frame.action = Action::kMatchNext;
        break;
      }
    }
  }
  auto result = std::move(values_.back().term);
  values_.pop_back();
  return result;
}

void DeclareOrDefine(SessionState& session, Transaction& txn,
                     const Syntax& syntax, TypedCommand& command) {
  const auto args = syntax.Children(0).subspan(1);
  const auto [kind, where] = syntax.header;
  const auto name = syntax.Name(args[0]);
  CheckTermName(session, syntax, args[0], true);
  const bool defined =
      kind == CommandKind::kDefineFun || kind == CommandKind::kDefineConst;
  const auto domain =
      kind == CommandKind::kDeclareFun || kind == CommandKind::kDefineFun
          ? syntax.Children(args[1])
          : ChildRange{};
  const auto return_expr =
      kind == CommandKind::kDeclareFun || kind == CommandKind::kDefineFun
          ? args[2]
          : args[1];
  if (!defined && !domain.empty() &&
      session.GetSignature().profile->declarations !=
          DeclarationPermission::kFunctions)
    Fail(DiagnosticCode::kUnsupported, where,
         "Free functions are outside the active profile");

  std::vector<SortView> domain_sorts;
  domain_sorts.reserve(domain.size());
  std::vector<std::string> source_rank;
  for (auto entry : domain) {
    const auto rank_expr = defined ? syntax.Children(entry)[1] : entry;
    const auto sort = ResolveSort(session, syntax, rank_expr, &txn);
    domain_sorts.push_back(sort.Borrow());
    if (!defined) source_rank.push_back(syntax.Render(rank_expr));
  }
  const auto range = ResolveSort(session, syntax, return_expr, &txn);
  if (!defined) {
    source_rank.push_back(syntax.Render(return_expr));
    auto decl = Take(
        session.manager.DeclareFunction(name, domain_sorts, range.Borrow()),
        where);
    const auto instance =
        Take(session.manager.InstantiateDeclaration(decl.Borrow(), {}), where);
    txn.AddTerm(name, instance.Borrow());
    txn.AddFunction(std::move(decl), syntax.Type(args[0]) == TokenKind::kQuoted,
                    source_rank, where);
    return;
  }

  Elaborator elaborator(session, syntax, &txn, command, nullptr);
  elaborator.ReserveName(name);
  std::vector<Decl> formals;
  formals.reserve(domain.size());
  for (std::size_t i = 0; i < domain.size(); ++i) {
    const auto formal = syntax.Children(domain[i])[0];
    auto decl =
        Take(session.manager.MakeFormal(syntax.Name(formal), domain_sorts[i]),
             syntax.Span(formal));
    const auto instance =
        Take(session.manager.InstantiateDeclaration(decl.Borrow(), {}), where);
    elaborator.Formal(
        formal, Take(session.manager.MakeApply(instance.Borrow(), {}), where));
    formals.push_back(std::move(decl));
  }
  session.Count(&FrontendStats::definition_bodies);
  auto body = elaborator.Resolve(args.back());
  CheckSort(body.Borrow().GetSort(), range.Borrow(), syntax.Span(args.back()));
  auto decl = Take(
      session.manager.FreshFunction(name, domain_sorts, range.Borrow()), where);
  auto instance =
      Take(session.manager.InstantiateDeclaration(decl.Borrow(), {}), where);
  txn.AddTerm(name, instance.Borrow());
  txn.AddDefinition({std::move(decl), std::move(instance), std::move(formals),
                     std::move(body), std::string(syntax.Raw(args[0])), where});
}
}  // namespace

Node SessionState::NumericLiteral(std::string_view digits, int radix,
                                  SortView sort, SourceSpan where) {
  constexpr std::size_t kMaxKeySize = 4 * 1024;
  constexpr std::size_t kMaxKeyBytes = 256 * 1024;
  const auto width = sort.IsNull() ? 0u : *sort.GetWidth();
  const auto make = [&](IntegerView value) {
    return Take(sort.IsNull() ? manager.MakeInt(value)
                              : manager.MakeBvValue(sort, value),
                where);
  };
  const bool eligible = digits.size() <= kMaxKeySize;
  const auto hash = eligible ? TextHash{}(digits) : 0;
  const auto slot = (hash ^ (hash >> 8) ^ static_cast<std::size_t>(width) ^
                     static_cast<std::size_t>(radix)) &
                    (kCacheSlots - 1);
  if (eligible && literal_cache) {
    const auto& entry = literal_cache->entries[slot];
    if (entry.radix == radix && entry.width == width &&
        entry.digits == digits) {
      auto result = make(entry.value);
      Count(&FrontendStats::literal_cache_hits);
      return result;
    }
  }
  auto integer = Take(radix == 10 ? Integer::ParseDecimal(digits)
                                  : Integer::ParseRadix(digits, radix),
                      where);
  auto result = make(integer.Borrow());
  if (!eligible) return result;
  if (!literal_cache) {
    Checkpoint();
    literal_cache = std::make_unique<LiteralCache>();
  }
  auto& cache = *literal_cache;
  std::string key(digits);
  const auto evict = [&](std::size_t index) {
    auto& entry = cache.entries[index];
    cache.key_bytes -= entry.digits.size();
    entry = LiteralCache::Entry{};
    cache.backing[index] = Integer{};
  };
  evict(slot);
  while (key.size() > kMaxKeyBytes - cache.key_bytes) {
    evict(cache.eviction);
    cache.eviction = (cache.eviction + 1) & (kCacheSlots - 1);
  }
  cache.key_bytes += key.size();
  if (const auto word = integer.Borrow().TryUint64()) {
    cache.entries[slot] = {std::move(key), IntegerView::FromUnsigned(*word),
                           width, radix};
  } else {
    cache.backing[slot] = std::move(integer);
    cache.entries[slot] = {std::move(key), cache.backing[slot].Borrow(), width,
                           radix};
  }
  return result;
}

void ApplyTerms(SessionState& state, const Syntax& syntax,
                TypedCommand& command, Transaction& txn) {
  const auto args = syntax.Children(0).subspan(1);
  if (command.header.kind == CommandKind::kAssert) {
    Elaborator elaborator(state, syntax, &txn, command, nullptr);
    auto term = elaborator.Resolve(args[0]);
    if (term.Borrow().GetSort().GetKind() != SortKind::kBool)
      NodeFailure({ErrorCode::kSortMismatch}, syntax.Span(args[0]));
    txn.AddAssertion({std::move(term), syntax.Span(args[0])});
  } else {
    DeclareOrDefine(state, txn, syntax, command);
  }
}
void ApplyValues(SessionState& state, const Syntax& syntax,
                 TypedCommand& command, const ModelOwner* model) {
  Elaborator elaborator(state, syntax, nullptr, command, model);
  for (auto expr : syntax.Children(syntax.Children(0)[1])) {
    auto term = elaborator.Resolve(expr);
    command.values.push_back(
        {std::move(term), elaborator.Render(expr), syntax.Span(expr)});
  }
}
}  // namespace dandelion::frontend::detail
