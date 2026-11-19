module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {
namespace {
struct CommandSpec {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::string_view spelling;
  CommandKind kind;
  // NOLINTEND(readability-identifier-naming)
};

constexpr std::array kCommands{
    CommandSpec{"assert", CommandKind::kAssert},
    CommandSpec{"check-sat", CommandKind::kCheckSat},
    CommandSpec{"check-sat-assuming", CommandKind::kUnsupported},
    CommandSpec{"declare-const", CommandKind::kDeclareConst},
    CommandSpec{"declare-datatype", CommandKind::kDeclareDatatype},
    CommandSpec{"declare-datatypes", CommandKind::kDeclareDatatypes},
    CommandSpec{"declare-fun", CommandKind::kDeclareFun},
    CommandSpec{"declare-sort", CommandKind::kDeclareSort},
    CommandSpec{"declare-sort-parameter", CommandKind::kUnsupported},
    CommandSpec{"define-const", CommandKind::kDefineConst},
    CommandSpec{"define-fun", CommandKind::kDefineFun},
    CommandSpec{"define-fun-rec", CommandKind::kUnsupported},
    CommandSpec{"define-funs-rec", CommandKind::kUnsupported},
    CommandSpec{"define-sort", CommandKind::kDefineSort},
    CommandSpec{"echo", CommandKind::kEcho},
    CommandSpec{"exit", CommandKind::kExit},
    CommandSpec{"get-assertions", CommandKind::kUnsupported},
    CommandSpec{"get-assignment", CommandKind::kUnsupported},
    CommandSpec{"get-info", CommandKind::kGetInfo},
    CommandSpec{"get-model", CommandKind::kGetModel},
    CommandSpec{"get-option", CommandKind::kGetOption},
    CommandSpec{"get-proof", CommandKind::kUnsupported},
    CommandSpec{"get-unsat-assumptions", CommandKind::kUnsupported},
    CommandSpec{"get-unsat-core", CommandKind::kUnsupported},
    CommandSpec{"get-value", CommandKind::kGetValue},
    CommandSpec{"pop", CommandKind::kUnsupported},
    CommandSpec{"push", CommandKind::kUnsupported},
    CommandSpec{"reset", CommandKind::kUnsupported},
    CommandSpec{"reset-assertions", CommandKind::kUnsupported},
    CommandSpec{"set-info", CommandKind::kSetInfo},
    CommandSpec{"set-logic", CommandKind::kSetLogic},
    CommandSpec{"set-option", CommandKind::kSetOption},
};
static_assert(std::ranges::is_sorted(kCommands, {}, &CommandSpec::spelling));
constexpr auto kReserved = std::to_array<std::string_view>(
    {"!", "BINARY", "DECIMAL", "HEXADECIMAL", "NUMERAL", "STRING", "_", "as",
     "exists", "forall", "lambda", "let", "match", "par"});
static_assert(std::ranges::is_sorted(kReserved));

constexpr auto kReservedSpellings = [] {
  std::array<std::string_view, kCommands.size() + kReserved.size()> result{};
  for (std::size_t i = 0; i < kCommands.size(); ++i)
    result[i] = kCommands[i].spelling;
  std::ranges::copy(kReserved, result.begin() + static_cast<std::ptrdiff_t>(
                                                    kCommands.size()));
  return result;
}();
constexpr auto kSpelling = [](std::string_view name) { return name; };
constexpr auto kReservedBuckets =
    SpellingBuckets(kReservedSpellings, kSpelling);
const CommandSpec* GetCommand(SpellingId id) noexcept {
  return id && id <= kCommands.size() ? &kCommands[id - 1] : nullptr;
}

constexpr auto kOptions = std::to_array<Option>({
    {":print-success", OptionId::kPrintSuccess, OptionKind::kBoolean, "false"},
    {":produce-models", OptionId::kProduceModels, OptionKind::kBoolean,
     "false"},
    {":global-declarations", OptionId::kGlobalDeclarations,
     OptionKind::kBoolean, "false"},
    {":interactive-mode", OptionId::kFixedFalse, OptionKind::kBoolean, "false"},
    {":produce-assertions", OptionId::kFixedFalse, OptionKind::kBoolean,
     "false"},
    {":produce-assignments", OptionId::kFixedFalse, OptionKind::kBoolean,
     "false"},
    {":produce-proofs", OptionId::kFixedFalse, OptionKind::kBoolean, "false"},
    {":produce-unsat-assumptions", OptionId::kFixedFalse, OptionKind::kBoolean,
     "false"},
    {":produce-unsat-cores", OptionId::kFixedFalse, OptionKind::kBoolean,
     "false"},
    {":random-seed", OptionId::kUnsupported, OptionKind::kNumeral, "0"},
    {":reproducible-resource-limit", OptionId::kUnsupported,
     OptionKind::kNumeral, "0"},
    {":regular-output-channel", OptionId::kUnsupported, OptionKind::kString,
     "\"stdout\""},
    {":diagnostic-output-channel", OptionId::kUnsupported, OptionKind::kString,
     "\"stderr\""},
    {":verbosity", OptionId::kUnsupported, OptionKind::kNumeral, "0"},
});

enum class Role { kTerm, kSort };
enum class Projection { kDirect, kSecond, kDatatype, kConstructor, kRecursive };
struct Cursor {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  ExprId single;
  Role role;
  ChildRange entries;
  std::size_t next = 0;
  Projection projection = Projection::kDirect;
  // NOLINTEND(readability-identifier-naming)
};
constexpr auto kAny = std::numeric_limits<std::size_t>::max();

class Grammar {
 public:
  explicit Grammar(Syntax& syntax) : syntax_(syntax) {}
  void Check();

 private:
  void Need(bool condition, ExprId id, std::string_view message) const;
  void Type(ExprId id, TokenKind kind) const;
  void Symbol(ExprId id) const;
  ChildRange List(ExprId id, std::size_t min, std::size_t max = kAny) const;
  bool AttributeValue(ExprId id) const noexcept;
  void Attributes(ChildRange entries) const;
  void Identifier(ExprId id, bool qualified);
  void Queue(ExprId id, Role role) { pending_.push_back({id, role, {}}); }
  void QueueRange(ChildRange entries, Role role,
                  Projection projection = Projection::kDirect);
  void CheckNext();
  void Bindings(ExprId id, Role role, std::size_t min);
  void Term(ExprId id);
  void Sort(ExprId id);
  void Pattern(ExprId id) const;
  ChildRange Constructors(ExprId id) const;
  void CheckDatatype(ExprId id) const;
  void Excluded(ChildRange args, std::string_view head);
  void OptionSetting(ChildRange args);
  void Command(CommandKind kind, ChildRange args);

  Syntax& syntax_;
  std::vector<Cursor> pending_;
};

void Grammar::Need(bool condition, ExprId id, std::string_view message) const {
  if (!condition) Fail(DiagnosticCode::kSyntax, syntax_.Span(id), message);
}
void Grammar::Type(ExprId id, TokenKind kind) const {
  Need(syntax_.Type(id) == kind, id, "Unexpected token in this position");
}
void Grammar::Symbol(ExprId id) const {
  Need(syntax_.IsSymbol(id), id, "Expected a symbol");
}
ChildRange Grammar::List(ExprId id, std::size_t min, std::size_t max) const {
  Need(syntax_.List(id), id, "Expected a list");
  const auto children = syntax_.Children(id);
  Need(children.size() >= min && children.size() <= max, id,
       "Wrong number of elements");
  return children;
}
bool Grammar::AttributeValue(ExprId id) const noexcept {
  return syntax_.Type(id) != TokenKind::kKeyword &&
         syntax_.Type(id) != TokenKind::kReserved;
}
void Grammar::Attributes(ChildRange entries) const {
  for (std::size_t i = 0; i < entries.size(); ++i) {
    Type(entries[i], TokenKind::kKeyword);
    if (i + 1 < entries.size() && AttributeValue(entries[i + 1])) ++i;
  }
}
void Grammar::Identifier(ExprId id, bool qualified) {
  if (syntax_.IsSymbol(id)) return;
  auto children = List(id, 1);
  if (qualified && syntax_.Is(children.front(), "as")) {
    Need(children.size() == 3, id, "Expected an identifier and sort after as");
    Queue(children[2], Role::kSort);
    id = children[1];
    if (syntax_.IsSymbol(id)) return;
    children = List(id, 1);
  }
  Need(children.size() >= 3 && syntax_.Is(children.front(), "_"), id,
       "Expected an indexed identifier");
  Symbol(children[1]);
  for (const auto index : children.subspan(2)) {
    Need(syntax_.Type(index) == TokenKind::kNumeral || syntax_.IsSymbol(index),
         index, "Expected a numeral or symbolic index");
  }
}
void Grammar::QueueRange(ChildRange entries, Role role, Projection projection) {
  if (!entries.empty())
    pending_.push_back({kNoExpr, role, entries, 0, projection});
}
void Grammar::CheckNext() {
  auto& cursor = pending_.back();
  const auto role = cursor.role;
  const auto projection = cursor.projection;
  auto id = cursor.single;
  if (id == kNoExpr) {
    id = cursor.entries[cursor.next++];
    if (cursor.next == cursor.entries.size()) pending_.pop_back();
  } else {
    pending_.pop_back();
  }
  switch (projection) {
    case Projection::kDirect:
      break;
    case Projection::kSecond:
      id = syntax_.Children(id)[1];
      break;
    case Projection::kDatatype:
      QueueRange(Constructors(id), role, Projection::kConstructor);
      return;
    case Projection::kConstructor:
      QueueRange(syntax_.Children(id).subspan(1), role, Projection::kSecond);
      return;
    case Projection::kRecursive: {
      const auto parts = syntax_.Children(id);
      Queue(parts[2], role);
      QueueRange(syntax_.Children(parts[1]), role, Projection::kSecond);
      return;
    }
  }
  if (role == Role::kSort)
    Sort(id);
  else
    Term(id);
}
void Grammar::Bindings(ExprId id, Role role, std::size_t min) {
  const auto bindings = List(id, min);
  for (auto entry : bindings | std::views::reverse) {
    const auto fields = List(entry, 2, 2);
    Symbol(fields[0]);
  }
  QueueRange(bindings, role, Projection::kSecond);
}
void Grammar::Sort(ExprId id) {
  if (!syntax_.List(id)) {
    Identifier(id, false);
    return;
  }
  const auto children = List(id, 1);
  if (syntax_.Is(children.front(), "_")) {
    Identifier(id, false);
  } else {
    Need(children.size() >= 2, id, "A sort application needs an argument");
    QueueRange(children.subspan(1), Role::kSort);
    Identifier(children.front(), false);
  }
}
void Grammar::Pattern(ExprId id) const {
  if (syntax_.Is(id, "_") || syntax_.IsSymbol(id)) return;
  const auto fields = List(id, 2);
  Symbol(fields.front());
  for (auto field : fields.subspan(1))
    if (!syntax_.Is(field, "_")) Symbol(field);
}
void Grammar::Term(ExprId id) {
  if (!syntax_.List(id)) {
    const auto token = syntax_.Type(id);
    if (token < TokenKind::kNumeral) Symbol(id);
    return;
  }
  const auto children = List(id, 1);
  const auto head = children.front();
  const auto reserved = [&](std::string_view name) {
    return syntax_.Is(head, name);
  };
  if (reserved("_") || reserved("as")) {
    Identifier(id, true);
  } else if (reserved("let")) {
    Need(children.size() == 3, id, "Expected bindings and a body after let");
    Queue(children[2], Role::kTerm);
    Bindings(children[1], Role::kTerm, 1);
  } else if (reserved("match")) {
    Need(children.size() == 3, id,
         "Expected a scrutinee and cases after match");
    const auto cases = List(children[2], 1);
    for (auto entry : cases | std::views::reverse) {
      const auto fields = List(entry, 2, 2);
      Pattern(fields[0]);
    }
    QueueRange(cases, Role::kTerm, Projection::kSecond);
    Queue(children[1], Role::kTerm);
  } else if (reserved("!")) {
    Need(children.size() >= 3, id, "Expected a term and attributes after !");
    Attributes(children.subspan(2));
    Queue(children[1], Role::kTerm);
  } else if (reserved("forall") || reserved("exists") || reserved("lambda")) {
    Need(children.size() == 3, id, "Expected typed bindings and a body");
    Queue(children[2], Role::kTerm);
    Bindings(children[1], Role::kSort, 1);
  } else {
    // Zero-argument application admission depends on the resolved head.
    QueueRange(children.subspan(1), Role::kTerm);
    Identifier(head, true);
  }
}
ChildRange Grammar::Constructors(ExprId id) const {
  const auto body = syntax_.Children(id);
  return !body.empty() && syntax_.Is(body.front(), "par")
             ? syntax_.Children(body[2])
             : body;
}
void Grammar::CheckDatatype(ExprId id) const {
  auto ctors = List(id, 0);
  if (!ctors.empty() && syntax_.Is(ctors.front(), "par")) {
    Need(ctors.size() == 3, id,
         "Expected sort parameters and constructors after par");
    for (auto name : List(ctors[1], 1)) Symbol(name);
    ctors = List(ctors[2], 0);
  }
  for (auto ctor : ctors | std::views::reverse) {
    const auto fields = List(ctor, 1);
    Symbol(fields.front());
    for (auto field : fields.subspan(1) | std::views::reverse) {
      const auto pair = List(field, 2, 2);
      Symbol(pair[0]);
    }
  }
}
void Grammar::OptionSetting(ChildRange args) {
  Need(!args.empty() && args.size() <= 2, 0,
       "Expected an option and its setting");
  Type(args[0], TokenKind::kKeyword);
  const auto* option = FindOption(syntax_.Raw(args[0]));
  if (!option) {
    Attributes(args);
    return;
  }
  Need(args.size() == 2, 0, "Missing option setting");
  switch (option->kind) {
    case OptionKind::kBoolean:
      Need(syntax_.Is(args[1], "true") || syntax_.Is(args[1], "false"), args[1],
           "Expected true or false");
      break;
    case OptionKind::kNumeral:
      Type(args[1], TokenKind::kNumeral);
      break;
    case OptionKind::kString:
      Type(args[1], TokenKind::kString);
      break;
  }
}
void Grammar::Excluded(ChildRange args, std::string_view head) {
  if (syntax_.Type(syntax_.Children(0).front()) != TokenKind::kReserved) return;
  if (head == "define-fun-rec") {
    Command(CommandKind::kDefineFun, args);
    return;
  }
  if (head == "define-funs-rec") {
    Need(args.size() == 2, 0,
         "Expected recursive declarations and their bodies");
    const auto declarations = List(args[0], 1);
    const auto bodies = List(args[1], declarations.size(), declarations.size());
    QueueRange(bodies, Role::kTerm);
    for (auto declaration : declarations | std::views::reverse) {
      const auto parts = List(declaration, 3, 3);
      Symbol(parts[0]);
      for (auto entry : List(parts[1], 0) | std::views::reverse)
        Symbol(List(entry, 2, 2)[0]);
    }
    QueueRange(declarations, Role::kSort, Projection::kRecursive);
    return;
  }
  if (head == "push" || head == "pop" || head == "declare-sort-parameter" ||
      head == "check-sat-assuming") {
    Need(args.size() == 1, 0, "Expected one command argument");
    if (head == "declare-sort-parameter")
      Symbol(args[0]);
    else if (head == "check-sat-assuming")
      QueueRange(List(args[0], 0), Role::kTerm);
    else
      Type(args[0], TokenKind::kNumeral);
    return;
  }
  Need(args.empty(), 0, "This command takes no arguments");
}
void Grammar::Command(CommandKind kind, ChildRange args) {
  const auto arity = [&](std::size_t count) {
    Need(args.size() == count, 0, "Wrong number of command arguments");
  };
  switch (kind) {
    case CommandKind::kSetLogic:
      arity(1);
      Symbol(args[0]);
      break;
    case CommandKind::kSetInfo:
      Need(!args.empty() && args.size() <= 2, 0, "Expected one attribute");
      Type(args[0], TokenKind::kKeyword);
      if (args.size() == 2)
        Need(AttributeValue(args[1]), args[1], "Expected an attribute value");
      break;
    case CommandKind::kSetOption:
      OptionSetting(args);
      break;
    case CommandKind::kGetInfo:
    case CommandKind::kGetOption:
      arity(1);
      Type(args[0], TokenKind::kKeyword);
      break;
    case CommandKind::kEcho:
      arity(1);
      Type(args[0], TokenKind::kString);
      break;
    case CommandKind::kDeclareSort:
      arity(2);
      Symbol(args[0]);
      Type(args[1], TokenKind::kNumeral);
      break;
    case CommandKind::kDeclareFun:
      arity(3);
      Symbol(args[0]);
      Queue(args[2], Role::kSort);
      QueueRange(List(args[1], 0), Role::kSort);
      break;
    case CommandKind::kDeclareConst:
      arity(2);
      Symbol(args[0]);
      Queue(args[1], Role::kSort);
      break;
    case CommandKind::kDefineSort:
      arity(3);
      Symbol(args[0]);
      for (auto param : List(args[1], 0)) Symbol(param);
      Queue(args[2], Role::kSort);
      break;
    case CommandKind::kDefineFun:
      arity(4);
      Symbol(args[0]);
      Queue(args[3], Role::kTerm);
      Queue(args[2], Role::kSort);
      Bindings(args[1], Role::kSort, 0);
      break;
    case CommandKind::kDefineConst:
      arity(3);
      Symbol(args[0]);
      Queue(args[2], Role::kTerm);
      Queue(args[1], Role::kSort);
      break;
    case CommandKind::kDeclareDatatype:
      arity(2);
      Symbol(args[0]);
      CheckDatatype(args[1]);
      QueueRange(Constructors(args[1]), Role::kSort, Projection::kConstructor);
      break;
    case CommandKind::kDeclareDatatypes: {
      arity(2);
      for (auto header : List(args[0], 0)) {
        const auto parts = List(header, 2, 2);
        Symbol(parts[0]);
        Type(parts[1], TokenKind::kNumeral);
      }
      const auto bodies = List(args[1], 0);
      for (auto datatype : bodies | std::views::reverse)
        CheckDatatype(datatype);
      QueueRange(bodies, Role::kSort, Projection::kDatatype);
      break;
    }
    case CommandKind::kAssert:
      arity(1);
      Queue(args[0], Role::kTerm);
      break;
    case CommandKind::kGetValue:
      arity(1);
      QueueRange(List(args[0], 1), Role::kTerm);
      break;
    case CommandKind::kCheckSat:
    case CommandKind::kGetModel:
    case CommandKind::kExit:
      arity(0);
      break;
    case CommandKind::kUnsupported:
      Excluded(args, syntax_.Raw(syntax_.Children(0).front()));
      break;
  }
}
void Grammar::Check() {
  const auto children = List(0, 1);
  const auto head = children.front();
  Need(syntax_.Type(head) == TokenKind::kSymbol ||
           syntax_.Type(head) == TokenKind::kReserved,
       head, "Expected a command name");
  const auto* spec = syntax_.Type(head) == TokenKind::kReserved
                         ? GetCommand(syntax_.Spelling(head))
                         : nullptr;
  Need(spec || syntax_.Type(head) == TokenKind::kSymbol, head,
       "Expected a command name");
  const auto kind = spec ? spec->kind : CommandKind::kUnsupported;
  syntax_.header = {kind, syntax_.Span(0)};
  Command(kind, children.subspan(1));
  while (!pending_.empty()) CheckNext();
}
}  // namespace

SpellingId ClassifyReserved(std::string_view word) noexcept {
  return LookupSpelling(kReservedSpellings, kSpelling, kReservedBuckets, word);
}
bool IsReservedWord(std::string_view word) noexcept {
  return ClassifyReserved(word) != kUnknownSpelling;
}
const Option* FindOption(std::string_view name) noexcept {
  const auto found = std::ranges::find(kOptions, name, &Option::name);
  return found == kOptions.end() ? nullptr : &*found;
}
void ValidateSyntax(Syntax& syntax) { Grammar(syntax).Check(); }

}  // namespace dandelion::frontend::detail
