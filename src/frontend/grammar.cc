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

constexpr std::array kReservedIds{
    SpellingId::kAnnotation,    SpellingId::kOtherReserved,
    SpellingId::kOtherReserved, SpellingId::kOtherReserved,
    SpellingId::kOtherReserved, SpellingId::kOtherReserved,
    SpellingId::kUnderscore,    SpellingId::kAs,
    SpellingId::kExists,        SpellingId::kForall,
    SpellingId::kLambda,        SpellingId::kLet,
    SpellingId::kMatch,         SpellingId::kPar};
static_assert(kReservedIds.size() == kReserved.size());

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
  const auto index = static_cast<std::size_t>(std::to_underlying(id)) -
                     std::to_underlying(SpellingId::kCommandBase);
  return index < kCommands.size() ? &kCommands[index] : nullptr;
}

constexpr auto kOptions = std::to_array<Option>({
    {":print-success", OptionId::kPrintSuccess, OptionKind::kBoolean,
     CommandResponse::kFalse},
    {":produce-models", OptionId::kProduceModels, OptionKind::kBoolean,
     CommandResponse::kFalse},
    {":global-declarations", OptionId::kGlobalDeclarations,
     OptionKind::kBoolean, CommandResponse::kFalse},
    {":interactive-mode", OptionId::kFixedFalse, OptionKind::kBoolean,
     CommandResponse::kFalse},
    {":produce-assertions", OptionId::kFixedFalse, OptionKind::kBoolean,
     CommandResponse::kFalse},
    {":produce-assignments", OptionId::kFixedFalse, OptionKind::kBoolean,
     CommandResponse::kFalse},
    {":produce-proofs", OptionId::kFixedFalse, OptionKind::kBoolean,
     CommandResponse::kFalse},
    {":produce-unsat-assumptions", OptionId::kFixedFalse, OptionKind::kBoolean,
     CommandResponse::kFalse},
    {":produce-unsat-cores", OptionId::kFixedFalse, OptionKind::kBoolean,
     CommandResponse::kFalse},
    {":random-seed", OptionId::kUnsupported, OptionKind::kNumeral,
     CommandResponse::kZero},
    {":reproducible-resource-limit", OptionId::kUnsupported,
     OptionKind::kNumeral, CommandResponse::kZero},
    {":regular-output-channel", OptionId::kUnsupported, OptionKind::kString,
     CommandResponse::kStdout},
    {":diagnostic-output-channel", OptionId::kUnsupported, OptionKind::kString,
     CommandResponse::kStderr},
    {":verbosity", OptionId::kUnsupported, OptionKind::kNumeral,
     CommandResponse::kZero},
});

using enum GrammarRole;

GrammarRole Slot(std::size_t index, std::initializer_list<GrammarRole> slots) {
  return index < slots.size() ? slots.begin()[index] : kInvalid;
}

GrammarRole CommandSlot(const GrammarCommand& command, std::size_t index) {
  if (index == 0) return kCommandName;
  --index;
  switch (command.shape) {
    case CommandKind::kSetLogic:
      return Slot(index, {kSymbol});
    case CommandKind::kSetInfo:
      return Slot(index, {kKeyword, kAttributeValue});
    case CommandKind::kSetOption: {
      if (index == 0) return kKeyword;
      if (index != 1) return kInvalid;
      if (!command.option) return kAttributeValue;
      switch (command.option->kind) {
        case OptionKind::kBoolean:
          return kBoolean;
        case OptionKind::kNumeral:
          return kNumeral;
        case OptionKind::kString:
          return kString;
      }
      std::unreachable();
    }
    case CommandKind::kGetOption:
    case CommandKind::kGetInfo:
      return Slot(index, {kKeyword});
    case CommandKind::kEcho:
      return Slot(index, {kString});
    case CommandKind::kDeclareSort:
      return Slot(index, {kSymbol, kNumeral});
    case CommandKind::kDeclareFun:
      return Slot(index, {kSymbol, kSorts, kSort});
    case CommandKind::kDeclareConst:
      return Slot(index, {kSymbol, kSort});
    case CommandKind::kDefineSort:
      return Slot(index, {kSymbol, kSymbols, kSort});
    case CommandKind::kDefineFun:
      return Slot(index, {kSymbol, kFormals, kSort, kTerm});
    case CommandKind::kDefineConst:
      return Slot(index, {kSymbol, kSort, kTerm});
    case CommandKind::kDeclareDatatype:
      return Slot(index, {kSymbol, kDatatype});
    case CommandKind::kDeclareDatatypes:
      return Slot(index, {kDatatypeHeaders, kDatatypes});
    case CommandKind::kAssert:
      return Slot(index, {kTerm});
    case CommandKind::kGetValue:
      return Slot(index, {kValues});
    case CommandKind::kCheckSat:
    case CommandKind::kGetModel:
    case CommandKind::kExit:
      return kInvalid;
    case CommandKind::kUnsupported:
      if (command.excluded.empty()) return kOpaque;
      if (command.excluded == "define-funs-rec")
        return Slot(index, {kRecDeclarations, kRecBodies});
      if (command.excluded == "declare-sort-parameter")
        return Slot(index, {kSymbol});
      if (command.excluded == "check-sat-assuming")
        return Slot(index, {kTerms});
      if (command.excluded == "push" || command.excluded == "pop")
        return Slot(index, {kNumeral});
      return kInvalid;
  }
  std::unreachable();
}

}  // namespace

GrammarRole SpecialChildRole(GrammarFrame& parent, const Token& token,
                             const GrammarCommand& command) {
  const auto index = parent.count++;
  const auto spelling =
      token.kind == TokenKind::kReserved ? token.spelling_id : kUnknownSpelling;
  if (index == 0) parent.head = spelling;
  switch (parent.role) {
    case kCommand:
      return CommandSlot(command, index);
    case kTerm: {
      switch (parent.head) {
        case SpellingId::kLet:
          return Slot(index, {kOpaque, kLetBindings, kTerm});
        case SpellingId::kMatch:
          return Slot(index, {kOpaque, kTerm, kCases});
        case SpellingId::kForall:
        case SpellingId::kExists:
        case SpellingId::kLambda:
          return Slot(index, {kOpaque, kNonemptyFormals, kTerm});
        case SpellingId::kAnnotation:
          if (index < 2) return Slot(index, {kOpaque, kTerm});
          if (parent.expect_attribute || token.kind == TokenKind::kKeyword ||
              token.kind == TokenKind::kReserved) {
            parent.expect_attribute = false;
            return kKeyword;
          }
          parent.expect_attribute = true;
          return kAttributeValue;
        case SpellingId::kUnderscore:
          return index == 0 ? kOpaque : index == 1 ? kSymbol : kIndex;
        case SpellingId::kAs:
          return Slot(index, {kOpaque, kPlainIdentifier, kSort});
        default:
          return index == 0 ? kHead : kTerm;
      }
    }
    case kSort:
      if (parent.head != SpellingId::kUnderscore)
        return index == 0 ? kPlainIdentifier : kSort;
      return index == 0 ? kOpaque : index == 1 ? kSymbol : kIndex;
    case kHead:
    case kIdentifier:
    case kPlainIdentifier:
      if (parent.head == SpellingId::kAs && parent.role != kPlainIdentifier)
        return Slot(index, {kOpaque, kPlainIdentifier, kSort});
      if (parent.head != SpellingId::kUnderscore) return kInvalid;
      return index == 0 ? kOpaque : index == 1 ? kSymbol : kIndex;
    case kLetBindings:
      return kLetBinding;
    case kLetBinding:
      return Slot(index, {kSymbol, kTerm});
    case kNonemptyFormals:
    case kFormals:
      return kFormal;
    case kFormal:
      return Slot(index, {kSymbol, kSort});
    case kCases:
      return kBranch;
    case kBranch:
      return Slot(index, {kPattern, kTerm});
    case kPattern:
      return index == 0 ? kSymbol : kPatternSymbol;
    case kSorts:
      return kSort;
    case kSymbols:
    case kParameters:
      return kSymbol;
    case kValues:
    case kTerms:
      return kTerm;
    case kDatatype:
      if (parent.head == SpellingId::kPar)
        return Slot(index, {kOpaque, kParameters, kConstructors});
      return kConstructor;
    case kConstructors:
      return kConstructor;
    case kConstructor:
      return index == 0 ? kSymbol : kFormal;
    case kDatatypeHeaders:
      return kDatatypeHeader;
    case kDatatypeHeader:
      return Slot(index, {kSymbol, kNumeral});
    case GrammarRole::kDatatypes:
      return kDatatype;
    case kRecDeclarations:
      return kRecDeclaration;
    case kRecDeclaration:
      return Slot(index, {kSymbol, kFormals, kSort});
    case kRecBodies:
      return index < command.recursive_count ? kTerm : kInvalid;
    case kAttributeValue:
    case kOpaque:
      return kOpaque;
    default:
      std::unreachable();
  }
}

bool AcceptAtom(GrammarRole role, const Token& token) {
  const auto kind = token.kind;
  switch (role) {
    case kTerm:
      return Syntax::Symbol(kind) || kind >= TokenKind::kNumeral;
    case kSort:
    case kHead:
    case kIdentifier:
    case kPlainIdentifier:
    case kSymbol:
      return Syntax::Symbol(kind);
    case kPattern:
    case kPatternSymbol:
      return Syntax::Symbol(kind) ||
             (kind == TokenKind::kReserved &&
              token.spelling_id == SpellingId::kUnderscore);
    case kNumeral:
      return kind == TokenKind::kNumeral;
    case kIndex:
      return kind == TokenKind::kNumeral || Syntax::Symbol(kind);
    case kKeyword:
      return kind == TokenKind::kKeyword;
    case kString:
      return kind == TokenKind::kString;
    case kBoolean:
      return kind == TokenKind::kSymbol &&
             (token.raw == "true" || token.raw == "false");
    case kCommandName:
      return kind == TokenKind::kSymbol ||
             (kind == TokenKind::kReserved && GetCommand(token.spelling_id));
    case kAttributeValue:
      return kind != TokenKind::kReserved && kind != TokenKind::kKeyword;
    case kOpaque:
      return true;
    default:
      return false;
  }
}

bool AcceptList(GrammarRole role) {
  switch (role) {
    case kSymbol:
    case kPatternSymbol:
    case kNumeral:
    case kIndex:
    case kKeyword:
    case kString:
    case kBoolean:
    case kCommandName:
    case kInvalid:
      return false;
    default:
      return true;
  }
}

std::size_t Minimum(const GrammarFrame& frame, const GrammarCommand& command) {
  switch (frame.role) {
    case kCommand: {
      if (frame.count == 0) return 1;
      if (command.shape == CommandKind::kSetInfo ||
          (command.shape == CommandKind::kSetOption && !command.option))
        return 2;
      if (command.shape == CommandKind::kUnsupported &&
          command.excluded.empty())
        return 1;
      std::size_t count = 1;
      while (CommandSlot(command, count) != kInvalid) ++count;
      return count;
    }
    case kTerm:
      switch (frame.head) {
        case SpellingId::kUnderscore:
        case SpellingId::kAs:
        case SpellingId::kLet:
        case SpellingId::kMatch:
        case SpellingId::kAnnotation:
        case SpellingId::kForall:
        case SpellingId::kExists:
        case SpellingId::kLambda:
          return 3;
        default:
          return 1;
      }
    case kSort:
      return frame.head == SpellingId::kUnderscore ? 3 : 2;
    case kHead:
    case kIdentifier:
    case kPlainIdentifier:
    case kRecDeclaration:
      return 3;
    case kLetBinding:
    case kFormal:
    case kPattern:
    case kBranch:
    case kDatatypeHeader:
      return 2;
    case kDatatype:
      return frame.head == SpellingId::kPar ? 3 : 0;
    case kLetBindings:
    case kNonemptyFormals:
    case kCases:
    case kValues:
    case kParameters:
    case kConstructor:
    case kRecDeclarations:
      return 1;
    case kRecBodies:
      return command.recursive_count;
    default:
      return 0;
  }
}

void ReadCommandName(GrammarCommand& command, SpellingId spelling) {
  if (const auto* spec = GetCommand(spelling)) {
    command.kind = spec->kind;
    command.shape = spec->kind;
    if (spec->kind == CommandKind::kUnsupported) {
      if (spec->spelling == "define-fun-rec")
        command.shape = CommandKind::kDefineFun;
      else
        command.excluded = spec->spelling;
    }
  }
}

std::optional<CommandHeader> ReadGrammar(Scanner& scanner) {
  Token first;
  if (!BeginCommand(scanner, first)) return std::nullopt;
  auto& frames = scanner.Frames();
  struct ResetFrames {
    // NOLINTNEXTLINE(readability-identifier-naming): Stack cleanup.
    std::vector<GrammarFrame>& frames;
    ~ResetFrames() { ClearScratch(frames); }
  } reset{frames};
  GrammarCommand command;
  PushFrame(scanner, frames, kCommand, first);
  for (;;) {
    const auto token = ScanCommandToken(scanner);
    auto& parent = frames.back();
    if (token.kind == TokenKind::kRight) {
      auto span = parent.where;
      span.end = token.where.end;
      if (!Complete(parent, command))
        Fail(DiagnosticCode::kSyntax, span, "Wrong number of elements");
      ClosedGrammarFrame(parent, command);
      frames.pop_back();
      if (frames.empty()) return CommandHeader{command.kind, span};
      continue;
    }
    const auto role = NextRole(parent, token, command);
    if (role == kInvalid && parent.role == kCommand)
      Fail(DiagnosticCode::kSyntax, parent.where,
           "Wrong number of command arguments");
    if (token.kind == TokenKind::kLeft) {
      if (!ListAllowed(role))
        Fail(DiagnosticCode::kSyntax, token.where, "Unexpected list");
      PushFrame(scanner, frames, role, token);
    } else {
      if (!AtomAllowed(role, token))
        Fail(DiagnosticCode::kSyntax, token.where,
             "Unexpected token in this position");
      ReadGrammarAtom(parent, role, token, command);
    }
  }
}

SpellingId ClassifyReserved(std::string_view word) noexcept {
  const auto ordinal =
      LookupSpelling(kReservedSpellings, kSpelling, kReservedBuckets, word);
  if (!ordinal) return kUnknownSpelling;
  const auto index = static_cast<std::size_t>(ordinal - 1);
  if (index < kCommands.size())
    return static_cast<SpellingId>(
        std::to_underlying(SpellingId::kCommandBase) + index);
  return kReservedIds[index - kCommands.size()];
}
bool IsReservedWord(std::string_view word) noexcept {
  return ClassifyReserved(word) != kUnknownSpelling;
}
const Option* FindOption(std::string_view name) noexcept {
  const auto found = std::ranges::find(kOptions, name, &Option::name);
  return found == kOptions.end() ? nullptr : &*found;
}

}  // namespace dandelion::frontend::detail
