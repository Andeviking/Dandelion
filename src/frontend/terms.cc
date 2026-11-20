module dandelion.frontend;
import :internal;
import dandelion.util.numeric;
import std;

namespace dandelion::frontend::detail {
namespace {

enum class HeadKind {
  kOperator,
  kLocal,
  kGlobal,
  kStaged,
  kTester,
  kLiteral,
  kModel,
  kExcluded
};
using Literal = std::variant<bool, numeric::RoundingMode, numeric::FpSpecial,
                             std::string_view>;

// All operands in a command share the scanner's source identifier.
struct OperandMarks {
  // NOLINTBEGIN(readability-identifier-naming): Written operand coordinates.
  SourcePosition begin;
  SourcePosition end;
  // NOLINTEND(readability-identifier-naming)
  SourceSpan Span(std::uint32_t source) const noexcept {
    return {source, begin, end};
  }
};

struct Call {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  HeadKind kind = HeadKind::kOperator;
  ExprId head = kNoExpr;
  ChildRange args;
  std::span<const OperandMarks> operands;
  SourceSpan head_span;
  std::array<SourceSpan, 2> index_spans;
  std::size_t Count() const noexcept {
    return args.empty() ? operands.size() : args.size();
  }
  SourceSpan Operand(const Syntax& syntax, std::size_t i,
                     std::uint32_t source) const noexcept {
    return args.empty() ? operands[i].Span(source) : syntax.Span(args[i]);
  }
  Location Head(const Syntax& syntax) const noexcept {
    return head == kNoExpr ? Location{head_span} : Location{syntax, head};
  }
  std::optional<SortView> qualifier;
  std::vector<numeric::Integer> indices;
  std::array<std::uint64_t, 2> word_indices{};
  numeric::IntegerView Index(std::size_t ordinal) const noexcept {
    return indices.empty()
               ? numeric::IntegerView::FromUnsigned(word_indices[ordinal])
               : indices[ordinal].Borrow();
  }
  const Operator* op = nullptr;
  std::size_t local = kNoExpr;
  TermBinding global;
  const Node* staged = nullptr;
  Literal literal = false;
  std::optional<numeric::Rational> rational;
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
  ExprId selector_scope;
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

struct WrittenIndex {
  // NOLINTBEGIN(readability-identifier-naming): Decoded index fields.
  SourceSpan where;
  std::uint64_t word = 0;
  std::optional<numeric::NumericResult<numeric::Integer>> wide;
  bool numeric = false;
  // NOLINTEND(readability-identifier-naming)
  numeric::IntegerView View() const noexcept {
    return wide ? wide->value().Borrow()
                : numeric::IntegerView::FromUnsigned(word);
  }
};

struct IndexedHead {
  // NOLINTBEGIN(readability-identifier-naming): Indexed identifier fields.
  SourceSpan name;
  std::array<WrittenIndex, 2> indices;
  const Operator* op;
  // NOLINTEND(readability-identifier-naming)
};

struct ReadFrame : GrammarFrame {
  // NOLINTBEGIN(readability-identifier-naming): Reduction frame fields.
  ExprId id = kNoExpr;
  std::size_t base;
  std::size_t escape_base;
  std::size_t position_base;
  std::size_t lexical_mark = kNoExpr;
  const Operator* plain = nullptr;
  std::size_t binder_base = 0;
  std::pair<std::size_t, std::size_t> text_mark{};
  ExprId rational = kNoExpr;
  std::size_t rational_arity = 0;
  bool delayed = false;
  // This frame retains written syntax for FP lookahead or model requests.
  bool recipe = false;
  bool indexed = false;
  bool attached = false;
  bool has_call = false;
  bool excluded_array = false;
  bool captures = false;
  bool opaque = false;
  // NOLINTEND(readability-identifier-naming)
};

struct Binder {
  // NOLINTBEGIN(readability-identifier-naming): Pending binder fields.
  std::string_view name;
  SpellingId spelling;
  SourceSpan where;
  // NOLINTEND(readability-identifier-naming)
};

class BinderText {
 public:
  std::pair<std::size_t, std::size_t> Mark() const noexcept {
    return {used_, large_.size()};
  }
  void Restore(std::pair<std::size_t, std::size_t> mark) noexcept {
    used_ = mark.first;
    large_.resize(mark.second);
  }
  std::string_view Append(std::string_view name) {
    if (name.empty()) return {};
    if (name.size() > kPageSize) {
      return *large_.emplace_back(std::make_unique<std::string>(name));
    }
    auto offset = used_ % kPageSize;
    if (name.size() > kPageSize - offset) {
      used_ += kPageSize - offset;
      offset = 0;
    }
    const auto page = used_ / kPageSize;
    if (page == pages_.size())
      pages_.push_back(std::make_unique_for_overwrite<char[]>(kPageSize));
    auto* const text = pages_[page].get() + offset;
    std::ranges::copy(name, text);
    used_ += name.size();
    return {text, name.size()};
  }
  void Clear() noexcept {
    used_ = 0;
    ClearScratch(large_);
    constexpr std::size_t kRetainedPages = 64;
    if (pages_.capacity() > 2 * kRetainedPages)
      std::vector<std::unique_ptr<char[]>>{}.swap(pages_);
    else if (pages_.size() > kRetainedPages)
      pages_.resize(kRetainedPages);
  }

 private:
  static constexpr std::size_t kPageSize = 4096;
  std::vector<std::unique_ptr<char[]>> pages_;
  std::vector<std::unique_ptr<std::string>> large_;
  std::size_t used_ = 0;
};

}  // namespace

class ElaborationScratch {
 public:
  void Clear() noexcept {
    ClearScratch(values);
    ClearScratch(escapes);
    ClearScratch(positions);
    ClearScratch(binders);
    ClearScratch(indices);
    ClearScratch(calls);
    ClearScratch(locals);
    ClearScratch(heads);
    ClearScratch(matches);
    ClearScratch(swaps);
    replacements.clear();
    ClearScratch(binder_names);
    binder_text.Clear();
  }

  // NOLINTBEGIN(readability-identifier-naming): Shared scratch fields.
  std::vector<Node> values;
  // Only operands produced under active lexical bindings have a slot.
  // Frame bases delimit marks independently of the owning node stack.
  std::vector<ExprId> escapes;
  std::vector<OperandMarks> positions;
  std::vector<ReadFrame> frames;
  std::vector<Binder> binders;
  std::vector<IndexedHead> indices;
  BinderText binder_text;
  std::vector<Call> calls;
  std::vector<Local> locals;
  std::unordered_map<std::string_view, std::size_t> heads;
  std::vector<MatchState> matches;
  std::vector<std::pair<std::size_t, std::size_t>> swaps;
  std::map<std::size_t, Replacement> replacements;
  std::unordered_set<std::string_view> binder_names;
  // NOLINTEND(readability-identifier-naming)
};

void DeleteElaborationScratch::operator()(
    ElaborationScratch* scratch) const noexcept {
  delete scratch;
}

namespace {
ElaborationScratch& Scratch(SessionState& session) {
  if (!session.elaboration) session.elaboration.reset(new ElaborationScratch);
  return *session.elaboration;
}

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
void CheckArity(std::size_t actual, std::uint64_t expected, Location where) {
  if (actual != expected)
    Fail(DiagnosticCode::kInvalidInput, where,
         "Wrong number of term arguments");
}
std::optional<std::size_t> BlamedIndex(Kind kind, const Call& call) {
  const auto below = [&](std::size_t index, std::int64_t minimum) {
    return call.Index(index).Compare(numeric::IntegerView(minimum)) < 0;
  };
  switch (kind) {
    case Kind::kBvRepeat:
    case Kind::kIntDivisible:
    case Kind::kIntToBv:
    case Kind::kFpToSbv:
    case Kind::kFpToUbv:
      if (below(0, 1)) return 0;
      break;
    case Kind::kFpFromBits:
    case Kind::kFpFromUbv:
      if (below(0, 2)) return 0;
      if (below(1, 2)) return 1;
      break;
    case Kind::kBvExtract: {
      const auto high = call.Index(0);
      if (high.Compare(numeric::IntegerView::FromUnsigned(kMaxWidth)) >= 0)
        return 0;
      if (call.Index(1).Compare(high) > 0) return 1;
      break;
    }
    default:
      break;
  }
  return std::nullopt;
}
void CheckSort(SortView actual, SortView expected, Location where) {
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
  Elaborator(SessionState& session, const Syntax& syntax, PendingTerms& pending,
             const TypedCommand& command, const ModelOwner* model);
  ~Elaborator() { scratch_.Clear(); }
  void ReduceAtom(ExprId expr);
  void ReplayRational(ExprId root);
  void Formal(ExprId name, Node node);
  std::string Render(ExprId root) const {
    return syntax_.Render(root,
                          replacements_.empty() ? nullptr : &replacements_);
  }
  void ReserveName(std::string_view name) noexcept { reserved_ = name; }

 private:
  friend class StreamingCommand;
  ExprId Scope(ExprId expr) const noexcept {
    return static_cast<ExprId>(syntax_.Span(expr).begin.offset);
  }
  bool EnterLeaf(const Token& token);
  void ReduceAtom(const Token& token);
  Call PrepareIdentifier(const Token& token, ExprId head, ChildRange indices,
                         ExprId qualifier, ExprId expr, ChildRange args,
                         bool application, bool open);
  void PushValue(Node node, ExprId escape = kNoExpr);
  ExprId LastEscape() const noexcept {
    return locals_.empty() ? kNoExpr : escapes_.back();
  }
  Call Prepare(ExprId expr, ExprId head, ChildRange args, bool application,
               bool open = false);
  Resolved FinishCall(Location where, Call& call, std::span<Node> operands,
                      std::span<const ExprId> escapes);
  ExprId Escape(std::span<const ExprId> escapes) const noexcept;
  Node TakeApplication(Result<Node> result, const Call& call, Location where);
  [[noreturn]] void ApplicationFailure(NodeError error, const Call& call,
                                       Location where);
  Node ApplyOrdinaryLiteral(Token token, Location where);
  Node ApplyLiteral(const Call& call, Location where);
  Node ApplyOperator(Location where, Call& call, std::span<Node> nodes);
  numeric::Rational FoldRational(ExprId root);
  Node ApplyGlobal(const TermBinding& binding, const Call& call,
                   std::span<Node> nodes, Location where);
  Result<Node> ApplyDatatype(DatatypeMember member,
                             std::optional<SortView> qualifier,
                             std::span<Node> nodes, const Call* call,
                             Location where);
  Result<Node> ApplyDeclared(DeclInstanceView instance, std::span<Node> nodes);
  Node ApplyTester(DatatypeMember member, NodeView operand, Location where);
  std::vector<SortView> Infer(DatatypeMember member, DeclView ctor,
                              std::optional<SortView> qualifier,
                              std::span<const Node> operands, Location where);
  DatatypeGroupView Group(DatatypeMember member) const noexcept;
  std::optional<DatatypeMember> Constructor(
      std::string_view name) const noexcept;

  void PushLocal(std::string_view spelling, ExprId scope, ExprId escape,
                 Node node, DeclView selector = {}, NodeView scrutinee = {},
                 ExprId selector_scope = kNoExpr);
  Node LocalValue(std::size_t binding);
  void Restore(std::size_t mark) noexcept;
  void UniqueBinders(ChildRange slots, bool let = false);
  std::size_t FindLocal(std::string_view spelling) const noexcept;
  void BindLet(ExprId expr, std::span<Node> values,
               std::span<const ExprId> escapes);
  void FinishAnnotation(ExprId expr, Resolved& result);

  void StartMatch(ExprId expr, NodeView scrutinee, ExprId escape);
  void BeginBranch(ExprId expr, ExprId branch);
  void FinishBranch(ExprId expr, ExprId field, Resolved result);
  Node FinishMatch(MatchState& match, Location where);

  SessionState& session_;
  const Syntax& syntax_;
  NodeManager& manager_;
  const Signature& signature_;
  const Profile& profile_;
  PendingTerms& pending_;
  const ModelOwner* model_;
  std::optional<std::string_view> reserved_;
  ElaborationScratch& scratch_;
  std::vector<Node>& values_;
  std::vector<ExprId>& escapes_;
  std::vector<Call>& calls_;
  std::vector<Local>& locals_;
  std::unordered_map<std::string_view, std::size_t>& heads_;
  std::vector<MatchState>& matches_;
  std::vector<std::pair<std::size_t, std::size_t>>& swaps_;
  std::map<std::size_t, Replacement>& replacements_;
  const bool value_mode_;
};

Elaborator::Elaborator(SessionState& session, const Syntax& syntax,
                       PendingTerms& pending, const TypedCommand& command,
                       const ModelOwner* model)
    : session_(session),
      syntax_(syntax),
      manager_(session.manager),
      signature_(session.GetSignature()),
      profile_(*signature_.profile),
      pending_(pending),
      model_(model),
      scratch_(Scratch(session)),
      values_(scratch_.values),
      escapes_(scratch_.escapes),
      calls_(scratch_.calls),
      locals_(scratch_.locals),
      heads_(scratch_.heads),
      matches_(scratch_.matches),
      swaps_(scratch_.swaps),
      replacements_(scratch_.replacements),
      value_mode_(command.header.kind == CommandKind::kGetValue) {}

void Elaborator::ReduceAtom(ExprId expr) {
  if (EnterLeaf(syntax_.GetTokenInfo(expr))) return;
  auto call = Prepare(expr, expr, {}, false);
  auto result = FinishCall({syntax_, expr}, call, {}, {});
  PushValue(std::move(result.term), result.escape);
}

void Elaborator::PushValue(Node node, ExprId escape) {
#ifdef DANDELION_FRONTEND_TESTING
  if (values_.size() == values_.capacity()) session_.Checkpoint();
#endif
  values_.push_back(std::move(node));
  if (!locals_.empty()) escapes_.push_back(escape);
}
void Elaborator::ReduceAtom(const Token& token) {
  if (EnterLeaf(token)) return;
  auto call =
      PrepareIdentifier(token, kNoExpr, {}, kNoExpr, kNoExpr, {}, false, false);
  auto result = FinishCall(token.where, call, {}, {});
  PushValue(std::move(result.term), result.escape);
}

bool Elaborator::EnterLeaf(const Token& token) {
  const Location where{token.where};
  if (token.kind >= TokenKind::kNumeral) {
    PushValue(ApplyOrdinaryLiteral(token, where));
    return true;
  }
  const auto spelling = token.spelling_id;
  if (spelling == SpellingId::kTrue || spelling == SpellingId::kFalse) {
    PushValue(TakeAt(manager_.MakeBool(spelling == SpellingId::kTrue), where));
    return true;
  }
  if (spelling >= SpellingId::kRne && spelling <= SpellingId::kRtz &&
      profile_.Has(kFloats)) {
    if (const auto mode = GetRoundingMode(spelling)) {
      PushValue(TakeAt(manager_.MakeRoundingMode(*mode), where));
      return true;
    }
  }
  const auto* op = GetOperator(spelling);
  if (op && op->indices == 0 &&
      (profile_.Has(op->features) ||
       (profile_.Has(kRestrictedIntegers) && IsRestrictedInt(op->kind))))
    return false;
  const auto name = syntax_.Name(token);
  if (IsReservedName(name)) return false;
  if (const auto binding = FindLocal(name); binding != kNoExpr) {
    const auto& local = locals_[binding];
    PushValue(LocalValue(binding), std::min(local.scope, local.escape));
    return true;
  }
  if (pending_.named) {
    if (const auto* staged = pending_.Find(name)) {
      PushValue(*staged);
      return true;
    }
  }
  const auto* entry = signature_.terms.find(name);
  if (!entry) return false;
  const auto binding = signature_.ResolveTerm(entry->second);
  if (const auto* instance = std::get_if<DeclInstanceView>(&binding)) {
    CheckArity(0, instance->GetDomain().size(), where);
    PushValue(TakeAt(ApplyDeclared(*instance, {}), where));
  } else if (const auto* member = std::get_if<DatatypeMember>(&binding)) {
    PushValue(TakeAt(ApplyDatatype(*member, std::nullopt, {}, nullptr, where),
                     where));
  } else {
    PushValue(
        signature_
            .aliases[std::to_underlying(std::get<AliasOrdinal>(binding))]);
  }
  return true;
}

Call Elaborator::Prepare(ExprId expr, ExprId head, ChildRange args,
                         bool application, bool open) {
  const auto id = GetIdentifier(syntax_, head);
  return PrepareIdentifier(syntax_.GetTokenInfo(id.name), head, id.indices,
                           id.qualifier, expr, args, application, open);
}

Call Elaborator::PrepareIdentifier(const Token& token, ExprId head,
                                   ChildRange indices, ExprId qualifier,
                                   ExprId expr, ChildRange args,
                                   bool application, bool open) {
  const Identifier id{kNoExpr, indices, qualifier};
  const Location where =
      head == kNoExpr ? Location{token.where} : Location{syntax_, head};
  const auto spelling = token.spelling_id;
  const bool indexed = !id.indices.empty();
  const auto* descriptor = GetOperator(spelling);
  const auto* op = descriptor && indexed == (descriptor->indices != 0)
                       ? descriptor
                       : nullptr;
  const auto resolve_qualifier = [&]() -> std::optional<SortView> {
    if (id.qualifier == kNoExpr) return std::nullopt;
    return ResolveSort(session_, syntax_, id.qualifier);
  };
  Call call;
  call.head = head;
  if (head == kNoExpr) call.head_span = token.where;
  call.args = args;
  if (!open && application && args.empty() &&
      (!op || (op->kind != Kind::kAnd && op->kind != Kind::kOr))) [[unlikely]]
    Fail(DiagnosticCode::kInvalidInput, {syntax_, expr},
         "Use a bare or qualified nullary identifier");

  const bool active_operator =
      op && (profile_.Has(op->features) ||
             (profile_.Has(kRestrictedIntegers) && IsRestrictedInt(op->kind)));
  const auto name = syntax_.Name(token);
  if (IsReservedName(name)) {
    if (name == "@" && application && !indexed &&
        token.kind == TokenKind::kSymbol) {
      if (!open) CheckArity(args.size(), 2, where);
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
    const bool boolean =
        spelling == SpellingId::kTrue || spelling == SpellingId::kFalse;
    const auto rm =
        profile_.Has(kFloats) ? GetRoundingMode(spelling) : std::nullopt;
    if (!active_operator && !boolean && !rm) {
      if (const auto binding = FindLocal(name); binding != kNoExpr) {
        if (!open) CheckArity(args.size(), 0, where);
        if (id.qualifier != kNoExpr)
          Fail(DiagnosticCode::kInvalidInput, where,
               "A lexical binding cannot be qualified");
        call.kind = HeadKind::kLocal;
        call.local = binding;
        return call;
      }
      if (const auto* staged = pending_.Find(name)) {
        if (!open) CheckArity(args.size(), 0, where);
        call.kind = HeadKind::kStaged;
        call.staged = staged;
        call.qualifier = resolve_qualifier();
        return call;
      }
      if (const auto it = signature_.terms.find(name); it != nullptr) {
        call.kind = HeadKind::kGlobal;
        call.global = signature_.ResolveTerm(it->second);
        if (!open) {
          if (const auto* ordinary =
                  std::get_if<DeclInstanceView>(&call.global))
            CheckArity(args.size(), ordinary->GetDomain().size(), where);
          else if (std::holds_alternative<AliasOrdinal>(call.global))
            CheckArity(args.size(), 0, where);
        }
        call.qualifier = resolve_qualifier();
        return call;
      }
    }
    if (boolean) {
      if (!open) CheckArity(args.size(), 0, where);
      call.kind = HeadKind::kLiteral;
      call.literal = spelling == SpellingId::kTrue;
      call.qualifier = resolve_qualifier();
      return call;
    }
    if (spelling >= SpellingId::kRne && spelling <= SpellingId::kRtz) {
      if (!open) CheckArity(args.size(), 0, where);
      if (!rm)
        Fail(DiagnosticCode::kUnsupported, where,
             "Rounding mode outside the active signature");
      call.kind = HeadKind::kLiteral;
      call.literal = *rm;
      call.qualifier = resolve_qualifier();
      return call;
    }
  } else {
    if (spelling == SpellingId::kIs) {
      CheckArity(id.indices.size(), 1, where);
      if (!syntax_.IsSymbol(id.indices.front()))
        Fail(DiagnosticCode::kInvalidInput, syntax_.Span(id.indices.front()),
             "Tester needs a constructor name");
      if (!open) CheckArity(args.size(), 1, where);
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
      if (!open) CheckArity(args.size(), 0, where);
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
      if (!open) CheckArity(args.size(), 0, where);
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
    if (op->indices != 0) {
      if (id.indices.size() != op->indices)
        Fail(DiagnosticCode::kInvalidInput, where, "Wrong number of indices");
      for (std::size_t i = 0; i < id.indices.size(); ++i) {
        const auto index = id.indices[i];
        if (syntax_.Type(index) != TokenKind::kNumeral)
          Fail(DiagnosticCode::kInvalidInput, {syntax_, index},
               "Expected a numeral index");
        if (const auto word = ParseWord(syntax_.Raw(index))) {
          call.word_indices[i] = *word;
        } else {
          call.indices = NumericIndices(syntax_, id, op->indices, where);
          break;
        }
      }
    }
    if (!application)
      Fail(DiagnosticCode::kInvalidInput, where,
           "Operator requires an application");
    if (!open && (args.size() < op->min_arity || args.size() > op->max_arity))
      NodeFailure({ErrorCode::kInvalidArity}, syntax_.Span(expr));
    if (op->kind == Kind::kArrayConst && id.qualifier == kNoExpr)
      NodeFailure({ErrorCode::kInvalidParameter}, where);
    if (!active_operator)
      Fail(DiagnosticCode::kUnsupported, where,
           "Operator is outside the active signature");
  } else if (descriptor || FindFpSpecial(name) || IsBvNumeral(name) ||
             spelling == SpellingId::kIs) {
    Fail(DiagnosticCode::kInvalidInput, where,
         "Wrong identifier form for this operator");
  } else if (IsKnownExcludedTerm(name)) {
    const std::size_t count = name == "re.^" ? 1 : name == "re.loop" ? 2 : 0;
    if (id.indices.size() != count)
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

Node Elaborator::ApplyOrdinaryLiteral(Token token, Location where) {
  const auto raw = token.raw;
  const auto type = token.kind;
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
    const auto sort =
        CachedBitVectorSort(session_, digits.size() * factor, where);
    return session_.NumericLiteral(digits, factor == 1 ? 2 : 16, sort, where);
  }
  Fail(DiagnosticCode::kUnsupported, where,
       "Real or String term is outside the input profile");
}

Node Elaborator::ApplyLiteral(const Call& call, Location where) {
  if (const auto* boolean = std::get_if<bool>(&call.literal))
    return TakeAt(manager_.MakeBool(*boolean), where);
  if (const auto* rm = std::get_if<numeric::RoundingMode>(&call.literal))
    return TakeAt(manager_.MakeRoundingMode(*rm), where);
  if (const auto* special = std::get_if<numeric::FpSpecial>(&call.literal)) {
    const auto sort = CachedFloatingPointSort(
        session_, call.indices[0].Borrow(), call.indices[1].Borrow(), where);
    const auto value = numeric::FloatingPointView::FromCanonicalSpecial(
        *sort.GetFpFormat(), *special);
    return TakeAt(manager_.MakeFpValue(sort, value), where);
  }
  const auto sort =
      CachedBitVectorSort(session_, call.indices[0].Borrow(), where);
  return session_.NumericLiteral(std::get<std::string_view>(call.literal), 10,
                                 sort, where);
}

numeric::Rational Elaborator::FoldRational(ExprId root) {
  // The incremental candidate check admits only numeric leaves and
  // unqualified unary minus or binary division. Its child slots are the
  // exact numeric recipe in written order.
  std::vector<std::pair<ExprId, std::size_t>> work{{root, 1}};
  std::vector<numeric::Rational> values;
  while (!work.empty()) {
    auto& [id, next] = work.back();
    const auto children = syntax_.Children(id);
    if (next < children.size()) {
      work.emplace_back(children[next++], 1);
      continue;
    }
    const Location where{syntax_, id};
    if (syntax_.Type(id) == TokenKind::kDecimal) {
      values.push_back(TakeNumericAt(
          numeric::Rational::ParseDecimalFraction(syntax_.Raw(id)), where));
    } else if (syntax_.Type(id) == TokenKind::kNumeral) {
      const auto value =
          TakeNumericAt(numeric::Integer::ParseDecimal(syntax_.Raw(id)), where);
      values.push_back(
          TakeNumericAt(numeric::Rational::FromInteger(value.Borrow()), where));
    } else if (children.size() == 2) {
      values.back() = TakeNumericAt(
          numeric::Rational::Negate(values.back().Borrow()), where);
    } else {
      const auto divisor = std::move(values.back());
      values.pop_back();
      auto quotient = values.back().Borrow() / divisor.Borrow();
      const auto location =
          !quotient &&
                  quotient.error() == numeric::NumericError::kDivisionByZero
              ? Location{syntax_, children[2]}
              : where;
      values.back() = TakeNumericAt(std::move(quotient), location);
    }
    work.pop_back();
  }
  return std::move(values.back());
}

Node Elaborator::ApplyOperator(Location where, Call& call,
                               std::span<Node> nodes) {
  auto kind = call.op->kind;
  if ((kind == Kind::kAnd || kind == Kind::kOr) && nodes.size() <= 1) {
    if (nodes.empty())
      return TakeAt(manager_.MakeBool(kind == Kind::kAnd), where);
    if (nodes[0].Borrow().GetSort().GetKind() != SortKind::kBool)
      NodeFailure({ErrorCode::kSortMismatch},
                  call.Operand(syntax_, 0, where.Span().source));
    return std::move(nodes[0]);
  }
  if (IsRestrictedInt(kind) && profile_.Has(kRestrictedIntegers) &&
      nodes.size() != 2) {
    if (kind == Kind::kIntSub && value_mode_ && call.args.size() == 1 &&
        !call.qualifier && syntax_.Type(call.args[0]) == TokenKind::kNumeral &&
        syntax_.Raw(call.args[0]) != "0") {
      const auto negative =
          TakeNumericAt(numeric::Integer::Negate(
                            nodes[0].Borrow().GetValue()->GetInteger().value()),
                        where);
      return TakeAt(manager_.MakeInt(negative.Borrow()), where);
    }
    Fail(DiagnosticCode::kUnsupported, where,
         "Integer rank outside the restricted signature");
  }
  if (kind == Kind::kIntSub && nodes.size() == 1) kind = Kind::kIntNeg;
  if (kind == Kind::kFpFromBits && call.Count() == 2) {
    if (call.rational)
      kind = Kind::kFpFromRational;
    else if (nodes[1].Borrow().GetSort().GetKind() == SortKind::kFloatingPoint)
      kind = Kind::kFpConvert;
    else
      kind = Kind::kFpFromSbv;
  }
  const auto rational =
      call.rational ? std::optional(call.rational->Borrow()) : std::nullopt;
  const auto qualifier =
      kind == Kind::kArrayConst ? call.qualifier : std::nullopt;
  const bool word_indexed =
      call.op->indices != 0 && call.indices.empty() && !rational;
  if (!word_indexed && rational && call.indices.empty()) {
    for (std::size_t i = 0; i < call.op->indices; ++i)
      call.indices.push_back(
          numeric::IntegerView::FromUnsigned(call.word_indices[i]).Copy());
  }
  const BuiltinOpView operation{kind, call.indices, qualifier, rational};
  const auto make = [&] {
    if (word_indexed)
      return manager_.MakeWordIndexedBuiltinConsuming(
          kind, std::span(call.word_indices).first(call.op->indices), nodes);
    return manager_.MakeBuiltinConsuming(operation, nodes);
  };
  swaps_.clear();
  auto reorder = std::span<Node>{};
  if (session_.parse_options.canonicalize_commutative) {
    reorder = CommutativeOperands(kind, nodes);
    if (!std::ranges::is_sorted(reorder, {}, &Node::GetId)) {
      std::vector<std::size_t> order(reorder.size());
      std::ranges::iota(order, std::size_t{0});
      std::ranges::sort(order, {},
                        [&](std::size_t idx) { return reorder[idx].GetId(); });
      swaps_.reserve(order.size());
      for (std::size_t i = 0; i < order.size(); ++i) {
        auto j = i;
        while (order[j] != i) {
          const auto next = order[j];
          swaps_.emplace_back(j, next);
          std::swap(reorder[j], reorder[next]);
          order[j] = j;
          j = next;
        }
        order[j] = j;
      }
    }
  }
  auto result = make();
  if (!swaps_.empty()) {
    if (result) {
      session_.Count(&FrontendStats::commutative_canonicalizations);
    } else if (result.error().code != ErrorCode::kResourceLimit) {
      for (auto [left, right] : swaps_ | std::views::reverse)
        std::swap(reorder[left], reorder[right]);
      const auto checked = manager_.CheckBuiltinOperands(operation, nodes);
      if (!checked) ApplicationFailure(checked.error(), call, where);
    }
  }
  if (!result && result.error().code == ErrorCode::kInvalidParameter &&
      result.error().parameter_index ==
          std::numeric_limits<std::uint64_t>::max() &&
      call.op->indices != 0) {
    if (const auto index = BlamedIndex(call.op->kind, call))
      result.error().parameter_index = *index;
  }
  return TakeApplication(std::move(result), call, where);
}

Node Elaborator::TakeApplication(Result<Node> result, const Call& call,
                                 Location where) {
  if (!result) [[unlikely]] {
    ApplicationFailure(result.error(), call, where);
  }
  return std::move(*result);
}

void Elaborator::ApplicationFailure(NodeError error, const Call& call,
                                    Location where) {
  if (error.operand_index < call.Count())
    NodeFailure(
        error, call.Operand(syntax_, error.operand_index, where.Span().source));
  if (error.parameter_index != std::numeric_limits<std::uint64_t>::max()) {
    if (call.head == kNoExpr) {
      if (call.op && error.parameter_index < call.op->indices)
        NodeFailure(error, call.index_spans[error.parameter_index]);
    } else {
      const auto indices = GetIdentifier(syntax_, call.head).indices;
      if (error.parameter_index < indices.size())
        NodeFailure(error, syntax_.Span(indices[error.parameter_index]));
    }
  }
  NodeFailure(error, where);
}

ExprId Elaborator::Escape(std::span<const ExprId> escapes) const noexcept {
  auto escape = kNoExpr;
  for (const auto mark : escapes) {
    session_.Count(&FrontendStats::escape_operands);
    escape = std::min(escape, mark);
  }
  return escape;
}

Resolved Elaborator::FinishCall(Location where, Call& call,
                                std::span<Node> operands,
                                std::span<const ExprId> escapes) {
  if (call.kind == HeadKind::kLocal) {
    const auto& local = locals_[call.local];
    return {LocalValue(call.local), std::min(local.scope, local.escape)};
  }
  if (call.kind == HeadKind::kExcluded)
    Fail(DiagnosticCode::kUnsupported, where,
         "Operator is outside the input profile");

  const auto escape = Escape(escapes);
  Node term;
  switch (call.kind) {
    case HeadKind::kOperator:
      term = ApplyOperator(where, call, operands);
      break;
    case HeadKind::kLiteral:
      term = ApplyLiteral(call, where);
      break;
    case HeadKind::kGlobal:
      term = ApplyGlobal(call.global, call, operands, where);
      break;
    case HeadKind::kStaged:
      term = *call.staged;
      break;
    case HeadKind::kTester:
      term = ApplyTester(std::get<DatatypeMember>(call.global),
                         operands[0].Borrow(),
                         call.Operand(syntax_, 0, where.Span().source));
      break;
    case HeadKind::kModel: {
      const auto id = GetIdentifier(syntax_, call.head);
      auto bound = ResolveModelReference(*model_, session_.Prefix(),
                                         syntax_.Name(id.name), call.qualifier,
                                         where.Span());
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
    if (const auto* ordinal = std::get_if<MemberOrdinal>(&it->second)) {
      const auto& member = signature_.members[std::to_underlying(*ordinal)];
      if (!member.field) return member;
    }
  }
  return std::nullopt;
}

std::vector<SortView> Elaborator::Infer(DatatypeMember member, DeclView ctor,
                                        std::optional<SortView> qualifier,
                                        std::span<const Node> operands,
                                        Location where) {
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
    stack.push_back({TakeAt(ctor.GetDomainRecipe(i - 1), where),
                     operands[i - 1].Borrow().GetSort()});
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
          {TakeAt(recipe.Argument(1), where), *actual.GetArrayElement()});
      stack.push_back(
          {TakeAt(recipe.Argument(0), where), *actual.GetArrayIndex()});
      continue;
    }
    const auto expected =
        recipe.GetKind() == SortRecipeKind::kRecursive
            ? TakeAt(group.GetSortConstructor(*recipe.GetRecursiveSlot()),
                     where)
            : *recipe.GetConstructor();
    if (actual.GetSortConstructor() != expected)
      NodeFailure({ErrorCode::kSortMismatch}, where);
    stack.push_back({recipe, actual, true});
    const auto args = actual.GetArguments();
    for (std::size_t i = args.size(); i > 0; --i)
      stack.push_back({TakeAt(recipe.Argument(i - 1), where), args[i - 1]});
  }
  if (std::ranges::any_of(types, &SortView::IsNull))
    Fail(DiagnosticCode::kInvalidInput, where,
         "Ambiguous datatype constructor needs a result qualifier");
  return types;
}

Result<Node> Elaborator::ApplyDatatype(DatatypeMember member,
                                       std::optional<SortView> qualifier,
                                       std::span<Node> nodes, const Call* call,
                                       Location where) {
  const auto group = Group(member);
  if (member.field) {
    CheckArity(nodes.size(), 1, where);
    const auto operand = nodes[0].Borrow().GetSort();
    if (operand.GetSortConstructor() !=
        TakeAt(group.GetSortConstructor(member.datatype), where))
      NodeFailure({.code = ErrorCode::kSortMismatch, .operand_index = 0},
                  call->Operand(syntax_, 0, where.Span().source));
    const auto selector = TakeAt(
        group.GetSelector(member.datatype, member.ctor, *member.field), where);
    const auto instance = TakeAt(
        manager_.InstantiateDeclaration(selector, TypeArguments(operand)),
        where);
    return manager_.MakeApplyConsuming(instance.Borrow(), nodes);
  }
  CheckArity(nodes.size(),
             TakeAt(group.GetFieldCount(member.datatype, member.ctor), where),
             where);
  const auto ctor =
      TakeAt(group.GetConstructor(member.datatype, member.ctor), where);
  const auto actuals = Infer(member, ctor, qualifier, nodes, where);
  const auto instance =
      TakeAt(manager_.InstantiateDeclaration(ctor, actuals), where);
  return manager_.MakeApplyConsuming(instance.Borrow(), nodes);
}
Node Elaborator::ApplyTester(DatatypeMember member, NodeView operand,
                             Location where) {
  const auto group = Group(member);
  if (operand.GetSort().GetSortConstructor() !=
      TakeAt(group.GetSortConstructor(member.datatype), where))
    NodeFailure({ErrorCode::kSortMismatch}, where);
  const auto tester =
      TakeAt(group.GetTester(member.datatype, member.ctor), where);
  const auto instance = TakeAt(
      manager_.InstantiateDeclaration(tester, TypeArguments(operand.GetSort())),
      where);
  const std::array args{operand};
  return TakeAt(manager_.MakeApply(instance.Borrow(), args), where);
}
Result<Node> Elaborator::ApplyDeclared(DeclInstanceView instance,
                                       std::span<Node> nodes) {
#ifdef DANDELION_FRONTEND_TESTING
  if (session_.Prefix().FindDefinition(instance.GetDeclaration()))
    session_.Count(&FrontendStats::defined_calls);
#endif
  return manager_.MakeApplyConsuming(instance, nodes);
}

Node Elaborator::ApplyGlobal(const TermBinding& binding, const Call& call,
                             std::span<Node> nodes, Location where) {
  if (const auto* instance = std::get_if<DeclInstanceView>(&binding))
    return TakeApplication(ApplyDeclared(*instance, nodes), call, where);
  if (const auto* member = std::get_if<DatatypeMember>(&binding))
    return TakeApplication(
        ApplyDatatype(*member, call.qualifier, nodes, &call, where), call,
        where);
  return signature_
      .aliases[std::to_underlying(std::get<AliasOrdinal>(binding))];
}

std::size_t Elaborator::FindLocal(std::string_view spelling) const noexcept {
  if (!heads_.empty()) {
    const auto it = heads_.find(spelling);
    return it == heads_.end() ? kNoExpr : it->second;
  }
  for (auto i = locals_.size(); i != 0; --i)
    if (locals_[i - 1].name == spelling) return i - 1;
  return kNoExpr;
}

void Elaborator::PushLocal(std::string_view spelling, ExprId scope,
                           ExprId escape, Node node, DeclView selector,
                           NodeView scrutinee, ExprId selector_scope) {
  constexpr std::size_t kInlineBindings = 8;
  const auto binding = locals_.size();
  if (heads_.empty() && binding == kInlineBindings) {
    for (std::size_t i = 0; i < binding; ++i) {
      auto& local = locals_[i];
      const auto [it, inserted] = heads_.try_emplace(local.name, i);
      if (!inserted) local.previous = std::exchange(it->second, i);
    }
  }
  auto previous = kNoExpr;
  if (!heads_.empty()) {
    const auto [it, inserted] = heads_.try_emplace(spelling, binding);
    if (!inserted) previous = std::exchange(it->second, binding);
  }
  locals_.push_back({spelling, scope, escape, previous, std::move(node),
                     selector, scrutinee, selector_scope});
}
Node Elaborator::LocalValue(std::size_t binding) {
  auto& local = locals_[binding];
  if (local.value.Borrow().IsNull()) {
    const Location where{syntax_, local.selector_scope};
    const auto instance =
        TakeAt(manager_.InstantiateDeclaration(
                   local.selector, TypeArguments(local.scrutinee.GetSort())),
               where);
    const std::array args{local.scrutinee};
    local.value = TakeAt(manager_.MakeApply(instance.Borrow(), args), where);
  }
  return local.value;
}
void Elaborator::Restore(std::size_t mark) noexcept {
  if (heads_.empty()) {
    locals_.resize(mark);
    return;
  }
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
  if (FindLocal(syntax_.Name(name)) != kNoExpr)
    Fail(DiagnosticCode::kInvalidInput, syntax_.Span(name),
         "Duplicate function formal");
  PushLocal(syntax_.Name(name), 0, kNoExpr, std::move(node));
}
void Elaborator::UniqueBinders(ChildRange slots, bool let) {
  std::array<std::string_view, 8> small;
  std::size_t count = 0;
  const bool small_scope = slots.size() <= small.size();
  auto& names = scratch_.binder_names;
  if (!small_scope) names.clear();
  for (const auto slot : slots) {
    const auto name = let ? syntax_.Children(slot)[0] : slot;
    if (!let && syntax_.Spelling(name) == SpellingId::kUnderscore) continue;
    CheckTermName(session_, syntax_, name, false);
    const auto spelling = syntax_.Name(name);
    bool unique;
    if (small_scope) {
      const auto used = std::span(small).first(count);
      unique = std::ranges::find(used, spelling) == used.end();
      small[count++] = spelling;
    } else {
      unique = names.insert(spelling).second;
    }
    if (!unique) [[unlikely]]
      Fail(DiagnosticCode::kInvalidInput, {syntax_, name},
           "Duplicate local name");
  }
}

void Elaborator::BindLet(ExprId expr, std::span<Node> values,
                         std::span<const ExprId> escapes) {
  const auto bindings = syntax_.Children(syntax_.Children(expr)[1]);
  UniqueBinders(bindings, true);
  std::size_t i = 0;
  for (auto binding : bindings) {
    const auto escape = escapes.empty() ? kNoExpr : escapes[i];
    PushLocal(syntax_.Name(syntax_.Children(binding)[0]), Scope(expr), escape,
              std::move(values[i++]));
  }
}

void Elaborator::FinishAnnotation(ExprId expr, Resolved& result) {
  const auto fields = syntax_.Children(expr);
  const bool capture = session_.parse_options.capture_source_records;
  std::vector<Attribute> attributes;
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
      if (pending_.Find(syntax_.Name(value)))
        Fail(DiagnosticCode::kInvalidInput, syntax_.Span(value),
             "Term name is already in use");
      if (syntax_.Name(value) == reserved_)
        Fail(DiagnosticCode::kInvalidInput, syntax_.Span(value),
             "Name is reserved for this definition");
      label = value;
    }
    if (capture) {
      auto where = syntax_.Span(key_id);
      if (value != kNoExpr) where.end = syntax_.Span(value).end;
      attributes.push_back({std::string(key),
                            value != kNoExpr
                                ? std::optional(syntax_.Render(value))
                                : std::nullopt,
                            where});
    }
  }
  if (label != kNoExpr) {
    session_.Count(&FrontendStats::closure_visits);
    if (result.escape != kNoExpr)
      Fail(DiagnosticCode::kInvalidInput, syntax_.Span(fields[1]),
           "Named term has an external lexical dependency");
    pending_.AddNamed(session_, syntax_.Name(label), result.term,
                      syntax_.Span(label));
  }
  if (capture) {
    Annotation annotation{result.term, std::move(attributes),
                          syntax_.Span(expr)};
    pending_.annotations.push_back(std::move(annotation));
  }
}

void Elaborator::StartMatch(ExprId expr, NodeView scrutinee, ExprId escape) {
  if (!profile_.Has(kDatatypes))
    Fail(DiagnosticCode::kUnsupported, syntax_.Span(expr),
         "Match outside the active signature");
  const auto sort = scrutinee.GetSort();
  if (!IsDatatype(sort))
    Fail(DiagnosticCode::kInvalidInput, syntax_.Span(syntax_.Children(expr)[1]),
         "Match requires a datatype");
  matches_.push_back(
      {scrutinee, *sort.GetSortConstructor(), {}, {}, {}, {}, escape});
}
void Elaborator::BeginBranch(ExprId expr, ExprId entry) {
  auto& match = matches_.back();
  const auto branch = syntax_.Children(entry);
  const auto pattern = branch[0];
  const auto fields = syntax_.Children(pattern);
  const Location where{syntax_, pattern};
  const auto head = fields.empty() ? pattern : fields[0];
  const auto name = syntax_.Name(head);
  auto member = syntax_.Spelling(head) == SpellingId::kUnderscore
                    ? std::nullopt
                    : Constructor(name);
  if (member && TakeAt(Group(*member).GetSortConstructor(member->datatype),
                       where) != match.type)
    member.reset();

  match.mark = locals_.size();
  if (member) {
    const auto group = Group(*member);
    const auto count =
        TakeAt(group.GetFieldCount(member->datatype, member->ctor), where);
    const auto slots = fields.empty() ? fields : fields.subspan(1);
    CheckArity(slots.size(), count, where);
    UniqueBinders(slots);
    for (std::size_t i = 0; i < slots.size(); ++i) {
      if (syntax_.Spelling(slots[i]) == SpellingId::kUnderscore) continue;
      const auto selector =
          TakeAt(group.GetSelector(member->datatype, member->ctor, i), where);
      PushLocal(syntax_.Name(slots[i]), Scope(expr), kNoExpr, {}, selector,
                match.scrutinee, expr);
    }
  } else if (!fields.empty()) {
    Fail(DiagnosticCode::kInvalidInput, where,
         "Pattern constructor belongs to another datatype or is unknown");
  } else if (syntax_.Spelling(pattern) != SpellingId::kUnderscore) {
    CheckTermName(session_, syntax_, pattern, false);
    PushLocal(syntax_.Name(pattern), Scope(expr), kNoExpr,
              TakeAt(manager_.RetainNode(match.scrutinee), where));
  }
  match.member = member;
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
    CheckSort(sort, match.result, {syntax_, field});

  if (!match.fallback &&
      (match.seen.empty() || match.seen.size() < match.constructor_count)) {
    if (member) {
      if (match.seen.empty())
        match.constructor_count =
            TakeAt(Group(*member).GetConstructorCount(member->datatype),
                   {syntax_, expr});
      if (match.seen.insert(member->ctor).second)
        match.effective.push_back({member, std::move(result.term)});
    } else {
      match.fallback = true;
      match.effective.push_back({member, std::move(result.term)});
    }
  }
}
Node Elaborator::FinishMatch(MatchState& match, Location where) {
  if (!match.fallback && match.seen.size() != match.constructor_count)
    Fail(DiagnosticCode::kInvalidInput, where, "Nonexhaustive match");
  auto result = std::move(match.effective.back().body);
  for (std::size_t i = match.effective.size() - 1; i > 0; --i) {
    const auto& branch = match.effective[i - 1];
    const auto test = ApplyTester(*branch.member, match.scrutinee, where);
    std::array args{test, branch.body, std::move(result)};
    result = TakeAt(
        manager_.MakeBuiltinConsuming({Kind::kIte, {}, {}, {}}, args), where);
  }
  return result;
}

void Elaborator::ReplayRational(ExprId root) {
  if (!syntax_.List(root)) {
    ReduceAtom(root);
    return;
  }
  struct ReplayFrame {
    // NOLINTBEGIN(readability-identifier-naming): Replay frame fields.
    ExprId expr;
    Call call;
    std::size_t base;
    std::size_t escape_base;
    std::size_t next = 0;
    // NOLINTEND(readability-identifier-naming)
  };
  std::vector<ReplayFrame> work;
  const auto open = [&](ExprId expr) {
    const auto children = syntax_.Children(expr);
    work.push_back({expr,
                    Prepare(expr, children.front(), children.subspan(1), true),
                    values_.size(), escapes_.size()});
  };
  open(root);
  while (!work.empty()) {
    auto& frame = work.back();
    if (frame.next != frame.call.args.size()) {
      const auto child = frame.call.args[frame.next++];
      if (syntax_.List(child))
        open(child);
      else
        ReduceAtom(child);
      continue;
    }
    auto result = FinishCall({syntax_, frame.expr}, frame.call,
                             std::span(values_).subspan(frame.base),
                             std::span(escapes_).subspan(frame.escape_base));
    values_.resize(frame.base);
    escapes_.resize(frame.escape_base);
    PushValue(std::move(result.term), result.escape);
    work.pop_back();
  }
}

DefinitionInputs PrepareDefinition(SessionState& session, const Syntax& syntax,
                                   Elaborator& elaborator, CommandKind kind) {
  const auto args = syntax.Children(0).subspan(1);
  const auto name = syntax.Name(args[0]);
  CheckTermName(session, syntax, args[0], true);
  elaborator.ReserveName(name);
  const auto domain =
      kind == CommandKind::kDefineFun ? syntax.Children(args[1]) : ChildRange{};
  const auto range_id = kind == CommandKind::kDefineFun ? args[2] : args[1];
  DefinitionInputs inputs;
  inputs.domain.reserve(domain.size());
  inputs.formals.reserve(domain.size());
  const bool capture = session.parse_options.capture_source_records;
  for (const auto entry : domain) {
    const auto [formal, sort] =
        std::pair{syntax.Children(entry)[0], syntax.Children(entry)[1]};
    const auto rank = ResolveSort(session, syntax, sort);
    inputs.domain.push_back(rank);
    if (capture) inputs.source_rank.push_back(syntax.Render(sort));
    const Location where{syntax, formal};
    auto decl =
        TakeAt(session.manager.MakeFormal(syntax.Name(formal), rank), where);
    const auto instance = TakeAt(
        session.manager.InstantiateDeclaration(decl.Borrow(), {}), where);
    elaborator.Formal(
        formal,
        TakeAt(session.manager.MakeApply(instance.Borrow(), {}), where));
    inputs.formals.push_back(std::move(decl));
  }
  inputs.range = ResolveSort(session, syntax, range_id);
  if (capture) inputs.source_rank.push_back(syntax.Render(range_id));
  session.Count(&FrontendStats::definition_bodies);
  return inputs;
}

void DeclareOrDefine(SessionState& session, Transaction& txn,
                     const Syntax& syntax, Node body,
                     DefinitionInputs* definition) {
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

  const bool capture = session.parse_options.capture_source_records;
  if (!defined) {
    auto& domain_sorts = session.domain_sorts;
    domain_sorts.reserve(domain.size());
    std::vector<std::string> source_rank;
    for (const auto rank : domain) {
      domain_sorts.push_back(ResolveSort(session, syntax, rank));
      if (capture) source_rank.push_back(syntax.Render(rank));
    }
    const auto range = ResolveSort(session, syntax, return_expr);
    if (capture) source_rank.push_back(syntax.Render(return_expr));
    auto decl =
        Take(session.manager.DeclareFunction(name, domain_sorts, range), where);
    const auto instance =
        Take(session.manager.InstantiateDeclaration(decl.Borrow(), {}), where);
    txn.AddTerm(name, instance.Borrow());
    txn.AddFunction(std::move(decl), syntax.Raw(args[0]),
                    std::move(source_rank), where);
    return;
  }

  auto& [formals, domain_sorts, source_rank, range] = *definition;
  CheckSort(body.Borrow().GetSort(), range, {syntax, args.back()});
  Node captured_body;
  if (capture) captured_body = body;
  std::optional<std::size_t> ordinal;
  if (formals.empty()) {
    txn.AddAlias(name, std::move(body));
  } else {
    auto decl =
        Take(session.manager.FreshFunction(name, domain_sorts, range), where);
    const auto instance =
        Take(session.manager.InstantiateDeclaration(decl.Borrow(), {}), where);
    txn.AddTerm(name, instance.Borrow());
    ordinal = txn.AddDefinition(
        {std::move(decl), std::move(formals), std::move(body)});
  }
  if (capture)
    txn.AddDefinitionSource({kind, std::string(syntax.Raw(args[0])),
                             std::move(source_rank), where,
                             std::move(captured_body), ordinal});
}
}  // namespace

Node SessionState::NumericLiteral(std::string_view digits, int radix,
                                  SortView sort, Location where) {
  constexpr std::size_t kMaxKeySize = 4 * 1024;
  constexpr std::size_t kMaxKeyBytes = 256 * 1024;
  const auto width = sort.IsNull() ? 0u : *sort.GetWidth();
  const auto make_word = [&](std::uint64_t word) {
    if (width == 0)
      return TakeAt(manager.MakeInt(numeric::IntegerView::FromUnsigned(word)),
                    where);
    return TakeAt(
        manager.MakeBvValue(
            sort, numeric::BitVectorView::FromCanonicalWord(width, word)),
        where);
  };
  const auto make = [&](const LiteralCache::Value& value) {
    if (const auto* word = std::get_if<std::uint64_t>(&value))
      return make_word(*word);
    if (const auto* integer = std::get_if<numeric::Integer>(&value))
      return TakeAt(manager.MakeInt(integer->Borrow()), where);
    return TakeAt(
        manager.MakeBvValue(sort, std::get<numeric::BitVector>(value).Borrow()),
        where);
  };

  const auto limit = radix == 10 ? 20u : radix == 16 ? 16u : 64u;
  auto word = digits.size() <= limit ? ParseWord(digits, radix) : std::nullopt;
  const bool indexed_bv = width != 0 && radix == 10;
  if (word && indexed_bv && width < 64 && (*word >> width) != 0) {
    *word = TakeNumericAt(numeric::BitVector::FromIntegerModulo(
                              numeric::IntegerView(width),
                              numeric::IntegerView::FromUnsigned(*word)),
                          where)
                .Chunk(0);
  }
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

  LiteralCache::Value value;
  if (word) {
    value = *word;
  } else if (width == 0) {
    value = TakeNumericAt(numeric::Integer::ParseDecimal(digits), where);
  } else if (indexed_bv) {
    value = TakeNumericAt(numeric::BitVector::FromDecimalModulo(
                              numeric::IntegerView(width), digits),
                          where);
  } else {
    value =
        TakeNumericAt(radix == 2 ? numeric::BitVector::FromBinaryDigits(digits)
                                 : numeric::BitVector::FromHexDigits(digits),
                      where);
  }
  auto result = make(value);
  if (!eligible) return result;
  if (!literal_cache) {
#ifdef DANDELION_FRONTEND_TESTING
    Checkpoint();
#endif
    literal_cache = std::make_unique<LiteralCache>();
  }
  auto& cache = *literal_cache;
  std::string key(digits);
  const auto evict = [&](std::size_t index) {
    auto& entry = cache.entries[index];
    cache.key_bytes -= entry.digits.size();
    entry = LiteralCache::Entry{};
  };
  evict(slot);
  while (key.size() > kMaxKeyBytes - cache.key_bytes) {
    evict(cache.eviction);
    cache.eviction = (cache.eviction + 1) & (kCacheSlots - 1);
  }
  cache.key_bytes += key.size();
  cache.entries[slot] = {std::move(key), std::move(value), width, radix};
  return result;
}

void ApplyTerms(SessionState& state, const Syntax& syntax,
                TypedCommand& command, Transaction& txn, Node term,
                DefinitionInputs* definition) {
  if (command.header.kind == CommandKind::kAssert) {
    const auto expr = syntax.Children(0)[1];
    if (term.Borrow().GetSort().GetKind() != SortKind::kBool)
      NodeFailure({ErrorCode::kSortMismatch}, syntax.Span(expr));
    txn.AddAssertion(std::move(term), syntax.Span(expr));
  } else {
    DeclareOrDefine(state, txn, syntax, std::move(term), definition);
  }
}

namespace {
using enum GrammarRole;

class StreamingCommand {
 public:
  StreamingCommand(SessionState& session, const ModelOwner* model,
                   const std::optional<CheckAnswer>* answer)
      : session_(session),
        model_(model),
        answer_(answer),
        scratch_(Scratch(session)),
        capture_(session.parse_options.capture_source_records) {}

  ~StreamingCommand() {
    Abort();
    // Retain ordinary deeply nested commands across assertions.
    constexpr std::size_t kRetainedFrameBytes = 2 * 1024 * 1024;
    scratch_.frames.clear();
    if (scratch_.frames.capacity() > kRetainedFrameBytes / sizeof(Frame))
      std::vector<Frame>{}.swap(scratch_.frames);
  }
  std::optional<CommandHeader> Read(std::optional<Diagnostic>& deferred);
  TypedCommand Publish(CommandHeader header);

 private:
  using Frame = ReadFrame;
  void Open(Frame& frame, const Token& token);
  void Atom(Frame& parent, GrammarRole role, const Token& token,
            CommandKind command);
  void Close(Frame& frame, const Token& token);
  void Abort() noexcept {
    elaborator_.reset();
    pending_ = {};
    command_.values.clear();
    definition_.reset();
    datatypes_.reset();
    sort_alias_.reset();
    if (syntax_) std::exchange(syntax_, nullptr)->Reset();
    if (std::exchange(held_literal_, false)) session_.scanner.ReleaseToken();
  }
  Frame& Parent() noexcept {
    return scratch_.frames[scratch_.frames.size() - 2];
  }
  void OpenSyntax(Frame& frame, Frame& parent);
  void OpenFlatHead(Frame& frame, const Token& token);
  void OpenCallForList(Frame& parent, const Frame& head);
  bool IndexedAtom(Frame& frame, GrammarRole role, const Token& token);
  void CloseIndexed(Frame& frame, Frame& parent, const SourceSpan& span);
  void StartCommand(Frame& frame, const Token& token, CommandKind kind);
  void FinishFlatCall(Frame& frame, const SourceSpan& span);
  Resolved ReduceApplication(Frame& frame, const SourceSpan& span,
                             std::span<Node> operands,
                             std::span<const ExprId> escapes,
                             std::span<const OperandMarks> positions);
  void FinishFlatLet(const Frame& list, Frame& owner);
  void CompleteTerm(Frame& parent, const Token& token, ExprId expr = kNoExpr);
  void KeepTerm(Frame& parent, const Token& token, ExprId expr);
  bool NeedsTerm(const Frame& frame) const noexcept;
  void BeforeOperand(const Frame& parent, const Token& token);
  [[gnu::cold]] void Defer(std::optional<Diagnostic>& deferred,
                           Diagnostic error,
                           std::optional<std::uint64_t>& close_offset,
                           bool closing);
  template <class Action>
  void Reduce(Action action, const Token& token,
              std::optional<Diagnostic>& deferred,
              std::optional<std::uint64_t>& close_offset,
              bool closing = false) {
    if (deferred) return;
    try {
      action();
    } catch (...) {
      auto error = Guard(token.where, [] { throw; });
      Defer(deferred, std::move(error.error()), close_offset, closing);
    }
  }
  ExprId Add(const Token& token, bool borrow) {
    auto id = syntax_->Add(token, borrow);
    ObserveFragments();
    return id;
  }
  void BeforeChild(Frame& parent, GrammarRole role, const Token& token);
  bool StartsRational(GrammarRole role, const Frame& parent) const;
  bool RationalChild(Frame& parent, GrammarRole role, const Token& token) const;
  void ReplayRational(std::size_t owner);
  void AfterChild(Frame& parent, GrammarRole role, ExprId expr);
  void FinishTerm(Frame& frame);
  void FinishCall(Frame& frame, ChildRange children);
  void CallArity(Location where, const Call& call, bool application);
  void OpenCall(Frame& frame, ExprId head) {
    elaborator_->calls_.push_back(
        elaborator_->Prepare(frame.id, head, {}, true, true));
    frame.has_call = true;
  }
  void StartDefinition();
  void StartDatatype(ExprId body, ChildRange parameters);
  void DatatypeChild(const Frame& parent, GrammarRole role, ExprId expr);
  void ReduceSort(ExprId expr);
  void AppendToken(const Token& token, GrammarRole role = kOpaque) {
    if (values_ || (capture_ && (capture_depth_ || CaptureFragment(role))))
      syntax_->Capture(token);
  }
  bool CaptureFragment(GrammarRole role) const noexcept {
    if (role == kAttributeValue) return true;
    if (role != kSort) return false;
    switch (command_.header.kind) {
      case CommandKind::kDeclareFun:
      case CommandKind::kDeclareConst:
      case CommandKind::kDefineFun:
      case CommandKind::kDefineConst:
        return true;
      default:
        return false;
    }
  }
  void ObserveFragments() noexcept {
#ifdef DANDELION_FRONTEND_TESTING
    session_.stats.peak_retained_fragments =
        std::max(session_.stats.peak_retained_fragments, syntax_->LiveCount());
#endif
  }
  void ObserveOperands() noexcept {
#ifdef DANDELION_FRONTEND_TESTING
    if (elaborator_) {
      session_.stats.peak_live_operands = std::max(
          session_.stats.peak_live_operands, elaborator_->values_.size());
    }
#endif
  }

  SessionState& session_;
  const ModelOwner* model_;
  const std::optional<CheckAnswer>* answer_;
  Syntax* syntax_ = nullptr;
  ElaborationScratch& scratch_;
  std::optional<DefinitionInputs> definition_;
  std::optional<PendingDatatypeGroup> datatypes_;
  std::optional<PendingSortAlias> sort_alias_;
  PendingTerms pending_;
  TypedCommand command_{};
  std::optional<Elaborator> elaborator_;
  const bool capture_;
  bool values_ = false;
  bool held_literal_ = false;
  std::size_t capture_depth_ = 0;
};

void StreamingCommand::StartDefinition() {
  const auto kind = command_.header.kind;
  if (kind == CommandKind::kDefineConst || kind == CommandKind::kDefineFun)
    definition_.emplace(
        PrepareDefinition(session_, *syntax_, *elaborator_, kind));
}

bool StreamingCommand::StartsRational(GrammarRole role,
                                      const Frame& parent) const {
  if (role != kTerm || !parent.has_call || parent.count != 3) return false;
  const auto* op = elaborator_->calls_.back().op;
  return op && op->kind == Kind::kFpFromBits;
}

bool StreamingCommand::RationalChild(Frame& parent, GrammarRole role,
                                     const Token& token) const {
  if (role == kHead && Syntax::Symbol(token.kind)) {
    const auto name = syntax_->Name(token);
    if (name != "-" && name != "/") return false;
    if (elaborator_->FindLocal(name) != kNoExpr || pending_.Find(name) ||
        session_.GetSignature().terms.contains(name))
      return false;
    parent.rational_arity = name == "-" ? 1 : 2;
    return true;
  }
  return role == kTerm && parent.count <= parent.rational_arity + 1 &&
         (token.kind == TokenKind::kLeft || token.kind == TokenKind::kNumeral ||
          token.kind == TokenKind::kDecimal);
}

void StreamingCommand::ReplayRational(std::size_t last) {
  auto& frames = scratch_.frames;
  auto owner = last;
  while (frames[owner].delayed) --owner;
  auto& terms = *elaborator_;
  frames[owner].rational = kNoExpr;
  // At most one open child connects adjacent frames. Replay completed
  // siblings before continuing down that open path.
  for (auto i = owner + 1; i <= last; ++i) {
    auto& frame = frames[i];
    frame.delayed = false;
    frame.base = terms.values_.size();
    frame.escape_base = terms.escapes_.size();
    auto children = syntax_->Children(frame.id);
    if (i < last) children = children.first(children.size() - 1);
    if (children.empty()) continue;
    OpenCall(frame, children.front());
    for (const auto child : children.subspan(1)) {
      terms.ReplayRational(child);
      syntax_->DiscardChildren(child);
      syntax_->ForgetSpelling(child);
    }
  }
}

void StreamingCommand::BeforeOperand(const Frame& parent, const Token& token) {
  if (!parent.has_call) return;
  const auto& call = elaborator_->calls_.back();
  if (call.kind == HeadKind::kLocal || call.kind == HeadKind::kStaged)
    CheckArity(1, 0, call.Head(*syntax_));
  if (call.kind != HeadKind::kGlobal) return;
  if (const auto* decl = std::get_if<DeclInstanceView>(&call.global)) {
    const auto index = parent.count - 2;
    const auto domain = decl->GetDomain();
    if (index >= domain.size())
      Fail(DiagnosticCode::kInvalidInput, call.Head(*syntax_),
           "Wrong number of term arguments");
    if (token.kind == TokenKind::kNumeral &&
        !elaborator_->profile_.Has(kIntegers) &&
        !elaborator_->profile_.Has(kRestrictedIntegers) &&
        domain[index].GetKind() != SortKind::kInt)
      NodeFailure({.code = ErrorCode::kSortMismatch, .operand_index = index},
                  token.where);
  } else if (std::holds_alternative<AliasOrdinal>(call.global)) {
    CheckArity(1, 0, call.Head(*syntax_));
  }
}

void StreamingCommand::BeforeChild(Frame& parent, GrammarRole role,
                                   const Token& token) {
  if (parent.delayed) {
    if (RationalChild(parent, role, token)) return;
    ReplayRational(scratch_.frames.size() - 1);
  }
  if (role == kTerm) {
    if (parent.role == kCommand) StartDefinition();
    if (parent.rational != kNoExpr) {
      elaborator_->ReplayRational(parent.rational);
      syntax_->DiscardChildren(parent.rational);
      syntax_->ForgetSpelling(parent.rational);
      parent.rational = kNoExpr;
    }
    BeforeOperand(parent, token);
  }
  if (role == kConstructor && parent.role == kDatatype)
    StartDatatype(parent.id, {});
  if (role == kCases)
    elaborator_->StartMatch(parent.id, elaborator_->values_.back().Borrow(),
                            elaborator_->LastEscape());
}

void StreamingCommand::OpenSyntax(Frame& frame, Frame& parent) {
  frame.id = Add({TokenKind::kLeft, kUnknownSpelling, "(", frame.where}, true);
  if (parent.id != kNoExpr) {
    syntax_->Append(parent.id, frame.id);
    frame.attached = true;
  }
}

bool StreamingCommand::NeedsTerm(const Frame& frame) const noexcept {
  return frame.recipe || frame.role == kCommand || frame.role == kBranch ||
         frame.role == kValues || frame.head == SpellingId::kAnnotation ||
         frame.head == SpellingId::kMatch;
}

void StreamingCommand::Open(Frame& frame, const Token& token) {
  auto& frames = scratch_.frames;
  if (frame.role == kCommand) {
    if (!session_.fragments) session_.fragments = std::make_unique<Syntax>();
    syntax_ = session_.fragments.get();
    frame.id = Add(token, true);
    return;
  }
  auto& parent = Parent();
  if (parent.opaque) {
    frame.opaque = true;
    AppendToken(token);
    return;
  }
  // Grammar has already shifted the child slot, but the child's semantic
  // state starts only after the parent has admitted that slot.
  if (parent.delayed && !RationalChild(parent, frame.role, token)) {
    // Replay only the previously admitted open frames.
    ReplayRational(frames.size() - 2);
  }
  if (!parent.delayed) {
    if (frame.role == kTerm) {
      if (parent.role == kCommand || parent.has_call)
        BeforeChild(parent, frame.role, token);
    } else if (frame.role == kConstructor || frame.role == kCases) {
      BeforeChild(parent, frame.role, token);
    }
  }
  frame.base = scratch_.values.size();
  frame.escape_base = scratch_.escapes.size();
  frame.position_base = scratch_.positions.size();
  const bool starts_rational = frame.role == kTerm && parent.recipe &&
                               !parent.delayed &&
                               StartsRational(frame.role, parent);
  frame.delayed = parent.delayed || starts_rational;
  frame.recipe =
      values_ || frame.delayed || (parent.recipe && frame.role != kTerm);
  frame.captures = capture_ && CaptureFragment(frame.role);
  if (frame.captures) ++capture_depth_;
  AppendToken(token, frame.role);

  if (frame.role == kTerm) {
    if (!frame.recipe) return;
    OpenSyntax(frame, parent);
    if (starts_rational) parent.rational = frame.id;
    return;
  }
  if (!frame.recipe && (frame.role == kHead || frame.role == kLetBindings ||
                        frame.role == kLetBinding)) {
    if (frame.role == kLetBindings) {
      parent.text_mark = scratch_.binder_text.Mark();
      parent.binder_base = scratch_.binders.size();
    }
    return;
  }
  OpenSyntax(frame, parent);
  frame.opaque = frame.role == kAttributeValue ||
                 (parent.excluded_array && frame.role == kSort);
}

void StreamingCommand::StartCommand(Frame& frame, const Token& token,
                                    CommandKind command) {
  command_.header = {command, frame.where};
  session_.CheckCommand(command, frame.where, model_, answer_);
  values_ = command == CommandKind::kGetValue;
  frame.recipe = values_;
  if (command == CommandKind::kDeclareDatatype ||
      command == CommandKind::kDeclareDatatypes) {
    if (!session_.GetSignature().profile->Has(detail::kDatatypes))
      Fail(DiagnosticCode::kUnsupported, frame.where,
           "Datatypes are outside the active signature");
    datatypes_.emplace(session_);
  }
  if (values_ || command == CommandKind::kAssert ||
      command == CommandKind::kDefineFun ||
      command == CommandKind::kDefineConst)
    elaborator_.emplace(session_, *syntax_, pending_, command_, model_);
  AppendToken(token, kCommandName);
  syntax_->Append(frame.id, Add(token, true));
}

void StreamingCommand::OpenFlatHead(Frame& frame, const Token& token) {
  auto& terms = *elaborator_;
  if (const auto* op = GetOperator(token.spelling_id);
      op && op->indices == 0 && terms.profile_.Has(op->features) &&
      op->kind != Kind::kArrayConst) {
    frame.plain = op;
    return;
  }
  terms.calls_.push_back(terms.PrepareIdentifier(token, kNoExpr, {}, kNoExpr,
                                                 kNoExpr, {}, true, true));
  frame.has_call = true;
}

void StreamingCommand::KeepTerm(Frame& parent, const Token& token,
                                ExprId expr) {
  if (NeedsTerm(parent)) {
    if (expr == kNoExpr)
      expr = Add({token.kind, token.spelling_id, {}, token.where}, true);
    syntax_->Append(parent.id, expr);
  } else {
    if (expr != kNoExpr) syntax_->Discard(expr);
    if (parent.has_call || parent.plain) {
#ifdef DANDELION_FRONTEND_TESTING
      if (scratch_.positions.size() == scratch_.positions.capacity())
        session_.Checkpoint();
#endif
      scratch_.positions.push_back({token.where.begin, token.where.end});
    }
  }
}

void StreamingCommand::CompleteTerm(Frame& parent, const Token& token,
                                    ExprId expr) {
  KeepTerm(parent, token, expr);
  ObserveOperands();
}

bool StreamingCommand::IndexedAtom(Frame& frame, GrammarRole role,
                                   const Token& token) {
  if (role == kOpaque) return true;
  if (role == kSymbol) {
    const auto* op = GetOperator(token.spelling_id);
    const auto name = Syntax::Name(token);
    const bool bv = IsBvNumeral(name);
    if (bv || (op && op->indices != 0 && op->kind != Kind::kFpFromBits)) {
      frame.indexed = true;
      auto& head = scratch_.indices.emplace_back();
      head.op = bv ? nullptr : op;
      head.name = token.where;
      if (bv && frame.role == kTerm) {
        session_.scanner.HoldToken(name.substr(2));
        held_literal_ = true;
      }
      return true;
    }
    // Other identifier forms use the same qualified/tester preparation as
    // other special forms. Recreate the fixed underscore token.
    frame.id =
        Add({TokenKind::kLeft, kUnknownSpelling, "(", frame.where}, true);
    const auto begin = frame.where.end;
    syntax_->Append(frame.id, Add({TokenKind::kReserved,
                                   SpellingId::kUnderscore,
                                   "_",
                                   {frame.where.source, begin, begin}},
                                  true));
    return false;
  }
  if (!frame.indexed) return false;
  const auto ordinal = frame.count - 3;
  if (ordinal < 2) {
    auto& index = scratch_.indices.back().indices[ordinal];
    index.where = token.where;
    if ((index.numeric = token.kind == TokenKind::kNumeral)) {
      if (const auto word = ParseWord(token.raw))
        index.word = *word;
      else
        index.wide.emplace(numeric::Integer::ParseDecimal(token.raw));
    }
  }
  return true;
}

void StreamingCommand::CloseIndexed(Frame& frame, Frame& parent,
                                    const SourceSpan& span) {
  auto& terms = *elaborator_;
  const auto& head = scratch_.indices.back();
  const auto count = head.op ? head.op->indices : 1;
  if (frame.count - 2 != static_cast<std::size_t>(count))
    Fail(DiagnosticCode::kInvalidInput, span, "Wrong number of indices");
  const auto indices = std::span(head.indices).first(count);
  for (const auto& index : indices) {
    if (!index.numeric)
      Fail(DiagnosticCode::kInvalidInput, index.where,
           "Expected a numeral index");
    if (index.wide && !*index.wide)
      NumericFailure(index.wide->error(), index.where);
  }
  if (head.op) {
    if (frame.role == kTerm)
      Fail(DiagnosticCode::kInvalidInput, span,
           "Operator requires an application");
    const auto& profile = terms.profile_;
    if (!profile.Has(head.op->features) &&
        !(profile.Has(kRestrictedIntegers) && IsRestrictedInt(head.op->kind)))
      Fail(DiagnosticCode::kUnsupported, span,
           "Operator is outside the active signature");
    auto& call = terms.calls_.emplace_back();
    call.op = head.op;
    call.head_span = span;
    if (std::ranges::any_of(indices, [](const WrittenIndex& index) {
          return index.wide.has_value();
        })) {
      for (const auto& index : indices)
        call.indices.push_back(index.View().Copy());
    }
    for (std::size_t i = 0; i < indices.size(); ++i) {
      call.word_indices[i] = indices[i].word;
      call.index_spans[i] = indices[i].where;
    }
    parent.has_call = true;
  } else {
    const auto& width = indices.front();
    if (width.View().Compare(numeric::IntegerView(1)) < 0)
      NodeFailure({.code = ErrorCode::kInvalidParameter, .parameter_index = 0},
                  width.where);
    if (!terms.profile_.Has(kBitVectors) && !terms.profile_.Has(kFloats))
      Fail(DiagnosticCode::kUnsupported, span,
           "Bit-vector literal outside the active signature");
    if (frame.role == kTerm) {
      const auto sort = CachedBitVectorSort(session_, width.View(), span);
      const auto digits = session_.scanner.HeldToken(head.name);
      auto term = session_.NumericLiteral(digits, 10, sort, span);
      session_.scanner.ReleaseToken();
      held_literal_ = false;
      terms.PushValue(std::move(term));
      CompleteTerm(parent, {TokenKind::kLeft, kUnknownSpelling, {}, span});
    } else {
      // A literal used as a head always fails application arity.
      auto& call = terms.calls_.emplace_back();
      call.kind = HeadKind::kLiteral;
      call.head_span = span;
      parent.has_call = true;
    }
  }
  scratch_.indices.pop_back();
}

void StreamingCommand::Atom(Frame& parent, GrammarRole role, const Token& token,
                            CommandKind command) {
  if (parent.opaque) {
    AppendToken(token);
    return;
  }
  if (role == kCommandName) {
    StartCommand(parent, token, command);
    return;
  }
  if (!parent.recipe) {
    if ((parent.role == kTerm || parent.role == kHead) &&
        parent.head == SpellingId::kUnderscore &&
        IndexedAtom(parent, role, token))
      return;
    if (role == kTerm) {
      if (parent.role == kCommand || parent.has_call)
        BeforeChild(parent, role, token);
      elaborator_->ReduceAtom(token);
      CompleteTerm(parent, token);
      return;
    }
    if (role == kHead) {
      OpenFlatHead(parent, token);
      return;
    }
    if (parent.role == kLetBinding && role == kSymbol) {
#ifdef DANDELION_FRONTEND_TESTING
      if (scratch_.binders.size() == scratch_.binders.capacity())
        session_.Checkpoint();
#endif
      const auto name = scratch_.binder_text.Append(Syntax::Name(token));
      scratch_.binders.push_back({name, token.spelling_id, token.where});
      return;
    }
    if (parent.role == kTerm && parent.count == 1 && role == kOpaque) {
      if (parent.head == SpellingId::kLet) return;
      if (parent.head == SpellingId::kForall ||
          parent.head == SpellingId::kExists ||
          parent.head == SpellingId::kLambda)
        Fail(DiagnosticCode::kUnsupported, parent.where,
             "Binder is outside the input profile");
      // These heads need syntax for their uncommon non-term arguments.
      parent.id =
          Add({TokenKind::kLeft, kUnknownSpelling, "(", parent.where}, true);
    }
    if (parent.role == kHead && parent.id == kNoExpr)
      parent.id =
          Add({TokenKind::kLeft, kUnknownSpelling, "(", parent.where}, true);
  }

  if (role == kKeyword && parent.role == kCommand &&
      command == CommandKind::kGetInfo && token.raw == ":reason-unknown" &&
      (session_.phase != SessionPhase::kAfterCheck ||
       (answer_ && *answer_ != CheckAnswer::kUnknown)))
    Fail(DiagnosticCode::kInvalidState, parent.where,
         "No unknown result is available");

  if (parent.recipe) BeforeChild(parent, role, token);
  AppendToken(token, role);
  const bool rational_leaf =
      StartsRational(role, parent) &&
      (token.kind == TokenKind::kNumeral || token.kind == TokenKind::kDecimal);
  auto stored = token;
  if (role == kString && parent.role == kCommand &&
      command == CommandKind::kEcho) {
#ifdef DANDELION_FRONTEND_TESTING
    session_.Checkpoint();
#endif
    command_.response = std::string(token.raw);
    stored.raw = {};
  }
  if (role == kAttributeValue && !capture_ && !values_) {
    const auto key = syntax_->Raw(syntax_->Children(parent.id).back());
    const bool needed =
        key == ":named" ||
        (parent.role == kCommand && command == CommandKind::kSetInfo &&
         (key == ":smt-lib-version" || key == ":source" || key == ":category" ||
          key == ":license" || key == ":status"));
    if (!needed) stored.raw = {};
  }
  const auto expr = Add(
      stored, session_.scanner.IsText() || (!parent.delayed && !rational_leaf &&
                                            (role == kTerm || role == kHead)));
  syntax_->Append(parent.id, expr);
  if (rational_leaf) {
    parent.rational = expr;
    return;
  }
  if (parent.delayed) return;
  if (parent.role == kTerm && role == kOpaque &&
      (token.spelling_id == SpellingId::kForall ||
       token.spelling_id == SpellingId::kExists ||
       token.spelling_id == SpellingId::kLambda))
    Fail(DiagnosticCode::kUnsupported, parent.where,
         "Binder is outside the input profile");
  if (role == kTerm) {
    elaborator_->ReduceAtom(expr);
    syntax_->ForgetSpelling(expr);
    ObserveOperands();
  }
  AfterChild(parent, role, expr);
  if (role == kHead) syntax_->ForgetSpelling(expr);
}

void StreamingCommand::AfterChild(Frame& parent, GrammarRole role,
                                  ExprId expr) {
  if (parent.delayed) return;
  if (parent.role == kTerm && role == kHead) {
    OpenCall(parent, expr);
  }
  if (role == kLetBindings) {
    parent.lexical_mark = elaborator_->locals_.size();
    elaborator_->BindLet(
        parent.id, std::span(elaborator_->values_).subspan(parent.base),
        std::span(elaborator_->escapes_).subspan(parent.escape_base));
    elaborator_->values_.resize(parent.base);
    elaborator_->escapes_.resize(parent.escape_base);
  }
  if (role == kPattern && parent.role == kBranch) {
    auto index = scratch_.frames.size() - (syntax_->List(expr) ? 4 : 3);
    elaborator_->BeginBranch(scratch_.frames[index].id, parent.id);
  }
  if (role == kBranch) {
    auto& terms = *elaborator_;
    const auto field = syntax_->Children(expr)[1];
    const auto escape = terms.LastEscape();
    if (!terms.locals_.empty()) terms.escapes_.pop_back();
    terms.FinishBranch(scratch_.frames[scratch_.frames.size() - 3].id, field,
                       {std::move(terms.values_.back()), escape});
    terms.values_.pop_back();
    syntax_->RemoveLast(parent.id);
  }
  if (role == kPlainIdentifier && parent.role == kSort &&
      syntax_->Spelling(expr) == SpellingId::kArray &&
      !session_.GetSignature().profile->Has(kArrays) &&
      !session_.GetSignature().sorts.contains("Array") &&
      !(sort_alias_ && sort_alias_->HasParameter("Array")) &&
      !(datatypes_ && datatypes_->HasSortName("Array")))
    parent.excluded_array = true;
  if (role == kSort && !parent.excluded_array) ReduceSort(expr);
  if (role == kSymbols && command_.header.kind == CommandKind::kDefineSort &&
      !syntax_->Children(expr).empty()) {
    sort_alias_.emplace(session_, *syntax_, syntax_->Children(0)[1],
                        syntax_->Children(expr));
    syntax_->DiscardChildren(expr);
  }
  if (datatypes_) DatatypeChild(parent, role, expr);
  if (role == kTerm && parent.role == kValues) {
    command_.values.push_back({std::move(elaborator_->values_.back()),
                               elaborator_->Render(expr), syntax_->Span(expr)});
    elaborator_->values_.pop_back();
    elaborator_->replacements_.clear();
    syntax_->RemoveLast(parent.id);
    syntax_->ClearCapture();
  }
}

void StreamingCommand::ReduceSort(ExprId expr) {
  if (datatypes_) {
    syntax_->SetReducedSort(expr, datatypes_->ReduceSort(*syntax_, expr));
  } else if (sort_alias_) {
    syntax_->SetReducedSort(expr, sort_alias_->Reduce(expr));
  } else {
    syntax_->SetReducedSort(expr, ResolveSort(session_, *syntax_, expr));
  }
}

void StreamingCommand::StartDatatype(ExprId body, ChildRange parameters) {
  const auto name = syntax_->Children(0)[1];
  datatypes_->StartBody(*syntax_, body, name, parameters);
}

void StreamingCommand::DatatypeChild(const Frame& parent, GrammarRole role,
                                     ExprId expr) {
  switch (role) {
    case kDatatypeHeaders:
      datatypes_->ReadHeaders(*syntax_, expr, syntax_->Span(0));
      syntax_->DiscardChildren(expr);
      break;
    case kParameters:
      StartDatatype(parent.id, syntax_->Children(expr));
      syntax_->DiscardChildren(expr);
      break;
    case kSymbol:
      if (parent.role == kConstructor)
        datatypes_->StartConstructor(*syntax_, expr);
      break;
    case kFormal:
      datatypes_->AddField(*syntax_, expr);
      syntax_->RemoveLast(parent.id);
      break;
    case kConstructor:
      datatypes_->EndConstructor(syntax_->Span(expr));
      syntax_->RemoveLast(parent.id);
      break;
    case kDatatype:
      if (syntax_->Children(expr).empty()) StartDatatype(expr, {});
      datatypes_->EndBody();
      syntax_->RemoveLast(parent.id);
      break;
    default:
      break;
  }
}

void StreamingCommand::CallArity(Location application_span, const Call& call,
                                 bool application) {
  const auto where = call.Head(*syntax_);
  const auto count = call.Count();
  if (application && count == 0 &&
      (!call.op || (call.op->kind != Kind::kAnd && call.op->kind != Kind::kOr)))
    Fail(DiagnosticCode::kInvalidInput, application_span,
         "Use a bare or qualified nullary identifier");
  switch (call.kind) {
    case HeadKind::kOperator:
      if (count < call.op->min_arity || count > call.op->max_arity)
        NodeFailure({ErrorCode::kInvalidArity}, application_span);
      break;
    case HeadKind::kGlobal:
      if (const auto* decl = std::get_if<DeclInstanceView>(&call.global))
        CheckArity(count, decl->GetDomain().size(), where);
      else if (std::holds_alternative<AliasOrdinal>(call.global))
        CheckArity(count, 0, where);
      break;
    case HeadKind::kLocal:
    case HeadKind::kLiteral:
    case HeadKind::kStaged:
      CheckArity(count, 0, where);
      break;
    case HeadKind::kTester:
      CheckArity(count, 1, where);
      break;
    case HeadKind::kModel:
    case HeadKind::kExcluded:
      break;
  }
}

void StreamingCommand::FinishCall(Frame& frame, ChildRange children) {
  const auto head = syntax_->Spelling(children.front());
  const bool application =
      head != SpellingId::kUnderscore && head != SpellingId::kAs;
  const auto args = application ? children.subspan(1) : ChildRange{};
  auto& terms = *elaborator_;
  auto call = application ? std::move(terms.calls_.back())
                          : terms.Prepare(frame.id, frame.id, args, false);
  if (application) terms.calls_.pop_back();
  call.args = args;
  CallArity({*syntax_, frame.id}, call, application);
  if (frame.rational != kNoExpr) {
    call.rational = terms.FoldRational(frame.rational);
  }
  auto result = terms.FinishCall(
      {*syntax_, frame.id}, call, std::span(terms.values_).subspan(frame.base),
      std::span(terms.escapes_).subspan(frame.escape_base));
  terms.values_.resize(frame.base);
  terms.escapes_.resize(frame.escape_base);
  terms.PushValue(std::move(result.term), result.escape);
}

void StreamingCommand::FinishTerm(Frame& frame) {
  auto& terms = *elaborator_;
  const auto expr = frame.id;
  const auto children = syntax_->Children(expr);
  switch (syntax_->Spelling(children.front())) {
    case SpellingId::kLet: {
      const auto escape = terms.LastEscape();
      terms.Restore(frame.lexical_mark);
      terms.escapes_.resize(frame.escape_base);
      if (!terms.locals_.empty())
        terms.escapes_.push_back(escape < terms.Scope(expr) ? escape : kNoExpr);
      break;
    }
    case SpellingId::kMatch: {
      auto& match = terms.matches_.back();
      const auto escape =
          match.escape < terms.Scope(expr) ? match.escape : kNoExpr;
      auto result = terms.FinishMatch(match, {*syntax_, expr});
      terms.matches_.pop_back();
      terms.values_.resize(frame.base);
      terms.escapes_.resize(frame.escape_base);
      terms.PushValue(std::move(result), escape);
      break;
    }
    case SpellingId::kAnnotation: {
      Resolved result{std::move(terms.values_.back()), terms.LastEscape()};
      terms.FinishAnnotation(expr, result);
      terms.values_.back() = std::move(result.term);
      break;
    }
    case SpellingId::kForall:
    case SpellingId::kExists:
    case SpellingId::kLambda:
      std::unreachable();
    default:
      FinishCall(frame, children);
      break;
  }
  syntax_->DiscardChildren(expr);
  ObserveOperands();
}

void StreamingCommand::FinishFlatLet(const Frame& list, Frame& owner) {
  auto& terms = *elaborator_;
  const auto binders = std::span(scratch_.binders).subspan(owner.binder_base);
  const auto rhs = std::span(terms.values_).subspan(list.base);
  const auto marks = std::span(terms.escapes_).subspan(list.escape_base);
  auto& names = scratch_.binder_names;
  const bool wide = binders.size() > 8;
  if (wide) names.clear();
  for (std::size_t i = 0; i < binders.size(); ++i) {
    const auto& binder = binders[i];
    CheckTermName(session_, binder.name, binder.spelling, binder.where, false);
    const auto prefix = binders.first(i);
    if (wide ? !names.insert(binder.name).second
             : std::ranges::find(prefix, binder.name, &Binder::name) !=
                   prefix.end())
      Fail(DiagnosticCode::kInvalidInput, binder.where, "Duplicate local name");
  }
  if (wide) names.clear();
  owner.lexical_mark = terms.locals_.size();
  for (std::size_t i = 0; i < binders.size(); ++i)
    terms.PushLocal(binders[i].name, owner.where.begin.offset,
                    marks.empty() ? kNoExpr : marks[i], std::move(rhs[i]));
  terms.values_.resize(list.base);
  terms.escapes_.resize(list.escape_base);
  scratch_.binders.resize(owner.binder_base);
}

Resolved StreamingCommand::ReduceApplication(
    Frame& frame, const SourceSpan& span, std::span<Node> operands,
    std::span<const ExprId> escapes, std::span<const OperandMarks> positions) {
  auto& terms = *elaborator_;
  if (frame.plain) {
    const auto& op = *frame.plain;
    const auto count = operands.size();
    if (count >= op.min_arity && count <= op.max_arity && count != 0 &&
        (count != 1 || (op.kind != Kind::kAnd && op.kind != Kind::kOr &&
                        op.kind != Kind::kIntSub)) &&
        !session_.parse_options.canonicalize_commutative) {
      auto result =
          terms.manager_.MakeBuiltinConsuming({op.kind, {}, {}, {}}, operands);
      if (!result) [[unlikely]] {
        const auto error = result.error();
        NodeFailure(error,
                    error.operand_index < positions.size()
                        ? positions[error.operand_index].Span(span.source)
                        : span);
      }
      return {std::move(*result), terms.Escape(escapes)};
    }
    Call call;
    call.op = &op;
    call.operands = positions;
    CallArity(span, call, true);
    return terms.FinishCall(span, call, operands, escapes);
  }

  auto& call = terms.calls_.back();
  call.operands = positions;
  CallArity(span, call, true);
  auto result = terms.FinishCall(span, call, operands, escapes);
  if (call.head != kNoExpr) syntax_->Discard(call.head);
  terms.calls_.pop_back();
  return result;
}

void StreamingCommand::FinishFlatCall(Frame& frame, const SourceSpan& span) {
  auto& terms = *elaborator_;
  auto result = ReduceApplication(
      frame, span, std::span(terms.values_).subspan(frame.base),
      std::span(terms.escapes_).subspan(frame.escape_base),
      std::span(scratch_.positions).subspan(frame.position_base));
  terms.values_.resize(frame.base);
  terms.escapes_.resize(frame.escape_base);
  scratch_.positions.resize(frame.position_base);
  terms.PushValue(std::move(result.term), result.escape);
}

void StreamingCommand::Close(Frame& frame, const Token& token) {
  AppendToken(token);
  auto span = frame.where;
  span.end = token.where.end;
  if (frame.id != kNoExpr) syntax_->Close(frame.id, span.end);
  if (frame.role == kCommand) return;
  auto& parent = Parent();
  if (parent.opaque) return;
  if (frame.captures) --capture_depth_;
  if (frame.delayed && frame.count != frame.rational_arity + 1)
    ReplayRational(scratch_.frames.size() - 1);

  if (!frame.recipe) {
    if (frame.indexed) {
      CloseIndexed(frame, parent, span);
      return;
    }
    if (frame.role == kLetBindings) {
      FinishFlatLet(frame, parent);
      return;
    }
    if (frame.role == kLetBinding) return;
    if (frame.role == kHead) {
      OpenCallForList(parent, frame);
      return;
    }
    if (frame.role == kTerm) {
      if (frame.head == SpellingId::kLet) {
        auto& terms = *elaborator_;
        const auto escape = terms.LastEscape();
        terms.Restore(frame.lexical_mark);
        scratch_.binder_text.Restore(frame.text_mark);
        terms.escapes_.resize(frame.escape_base);
        if (!terms.locals_.empty())
          terms.escapes_.push_back(escape < span.begin.offset ? escape
                                                              : kNoExpr);
      } else if (frame.id == kNoExpr) {
        FinishFlatCall(frame, span);
      } else {
        FinishTerm(frame);
      }
      CompleteTerm(parent, {TokenKind::kLeft, kUnknownSpelling, {}, span},
                   frame.id);
      return;
    }
  }

  if (frame.excluded_array) {
    CheckArity(syntax_->Children(frame.id).size() - 1, 2, {*syntax_, frame.id});
    Fail(DiagnosticCode::kUnsupported, syntax_->Span(frame.id),
         "Sort is outside the active signature");
  }
  if (frame.role == kTerm && !frame.delayed) {
    FinishTerm(frame);
    if (!frame.attached) {
      CompleteTerm(parent, {TokenKind::kLeft, kUnknownSpelling, {}, span},
                   frame.id);
      return;
    }
  }
  AfterChild(parent, frame.role, frame.id);
}

void StreamingCommand::OpenCallForList(Frame& parent, const Frame& head) {
  auto& terms = *elaborator_;
  terms.calls_.push_back(terms.Prepare(kNoExpr, head.id, {}, true, true));
  parent.has_call = true;
  const auto* op = terms.calls_.back().op;
  if (op && op->kind == Kind::kFpFromBits) {
    parent.id =
        Add({TokenKind::kLeft, kUnknownSpelling, "(", parent.where}, true);
    syntax_->Append(parent.id, head.id);
    parent.recipe = true;
  }
}

void StreamingCommand::Defer(std::optional<Diagnostic>& deferred,
                             Diagnostic error,
                             std::optional<std::uint64_t>& close_offset,
                             bool closing) {
  deferred = std::move(error);
  Abort();
  auto frames = std::span(scratch_.frames);
  if (closing) frames = frames.first(frames.size() - 1);
  const auto& span = deferred->where;
  if (std::ranges::any_of(frames, [&](const GrammarFrame& frame) {
        return frame.where == span;
      }))
    close_offset = span.begin.offset;
}

std::optional<CommandHeader> StreamingCommand::Read(
    std::optional<Diagnostic>& deferred) {
  auto& scanner = session_.scanner;
  Token first;
  if (!BeginCommand(scanner, first)) return std::nullopt;
  auto& frames = scratch_.frames;
  GrammarCommand command;
  std::optional<std::uint64_t> close_offset;

  auto& root = PushFrame(scanner, frames, kCommand, first);
#ifdef DANDELION_FRONTEND_TESTING
  session_.stats.peak_grammar_depth =
      std::max(session_.stats.peak_grammar_depth, frames.size());
#endif
  Reduce([&] { Open(root, first); }, first, deferred, close_offset);

  for (;;) {
    const auto token = ScanCommandToken(scanner);
    auto& frame = frames.back();
    const bool closing = token.kind == TokenKind::kRight;
    if (closing) {
      auto span = frame.where;
      span.end = token.where.end;
      if (!Complete(frame, command)) [[unlikely]]
        Fail(DiagnosticCode::kSyntax, span, "Wrong number of elements");
      ClosedGrammarFrame(frame, command);
      Reduce([&] { Close(frame, token); }, token, deferred, close_offset, true);
      frames.pop_back();
      if (close_offset == span.begin.offset) deferred->where.end = span.end;
      if (frames.empty()) {
        if (!deferred && session_.phase == SessionPhase::kBuilding &&
            command.kind == CommandKind::kDeclareDatatypes &&
            command.datatype_headers != command.datatype_bodies)
          deferred = Diagnostic{DiagnosticCode::kInvalidInput, span,
                                "Mismatched datatype headers and bodies"};
        return CommandHeader{command.kind, span};
      }
      continue;
    }

    const auto role = NextRole(frame, token, command);
    if (role == kInvalid && frame.role == kCommand) [[unlikely]]
      Fail(DiagnosticCode::kSyntax, frame.where,
           "Wrong number of command arguments");
    if (token.kind == TokenKind::kLeft) {
      if (!ListAllowed(role)) [[unlikely]]
        Fail(DiagnosticCode::kSyntax, token.where, "Unexpected list");
      auto& child = PushFrame(scanner, frames, role, token);
#ifdef DANDELION_FRONTEND_TESTING
      session_.stats.peak_grammar_depth =
          std::max(session_.stats.peak_grammar_depth, frames.size());
#endif
      Reduce([&] { Open(child, token); }, token, deferred, close_offset);
    } else {
      if (!AtomAllowed(role, token)) [[unlikely]]
        Fail(DiagnosticCode::kSyntax, token.where,
             "Unexpected token in this position");
      ReadGrammarAtom(frame, role, token, command);
      Reduce([&] { Atom(frame, role, token, command.kind); }, token, deferred,
             close_offset);
    }
  }
}

TypedCommand StreamingCommand::Publish(CommandHeader header) {
  syntax_->header = header;
  command_.header = header;
  Node term;
  if (elaborator_ && !elaborator_->values_.empty()) {
    term = std::move(elaborator_->values_.back());
  }
  session_.Publish(*syntax_, command_, std::move(term), pending_,
                   definition_ ? &*definition_ : nullptr,
                   datatypes_ ? &*datatypes_ : nullptr,
                   sort_alias_ ? &*sort_alias_ : nullptr);
  return std::move(command_);
}
}  // namespace

Outcome<std::optional<TypedCommand>> SessionState::ReadCommand(
    const ModelOwner* model, const std::optional<CheckAnswer>* answer) {
  if (in_read) return std::unexpected(ReentrantRead(scanner.Here()));
  if (phase == SessionPhase::kTerminated) return std::nullopt;
  const ReadScope reading(in_read);
  std::optional<StreamingCommand> command;
  std::optional<Diagnostic> deferred;
  const auto header = Guard(scanner.Here(), [&] {
    command.emplace(*this, model ? model : inspection_model.get(), answer);
    return command->Read(deferred);
  });
  if (!header || !*header) phase = SessionPhase::kTerminated;
  if (!header) return std::unexpected(header.error());
  if (!*header) return std::nullopt;
  if (deferred) return std::unexpected(*deferred);
  return Guard((**header).where, [&]() -> std::optional<TypedCommand> {
    return command->Publish(**header);
  });
}

}  // namespace dandelion::frontend::detail
