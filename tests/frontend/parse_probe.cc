import std;
import dandelion.frontend;

namespace dandelion::frontend::testing {
namespace {

// This serializer is an independent oracle adapter for the bounded corpus
// subset. It fails explicitly on a node outside the comparison inventory.
std::string_view Operator(Kind kind) {
  switch (kind) {
    case Kind::kAnd:
      return "and";
    case Kind::kOr:
      return "or";
    case Kind::kNot:
      return "not";
    case Kind::kXor:
      return "xor";
    case Kind::kImplies:
      return "=>";
    case Kind::kEqual:
      return "=";
    case Kind::kDistinct:
      return "distinct";
    case Kind::kIte:
      return "ite";
    case Kind::kIntAdd:
      return "+";
    case Kind::kIntSub:
    case Kind::kIntNeg:
      return "-";
    case Kind::kIntMul:
      return "*";
    case Kind::kBvAnd:
      return "bvand";
    case Kind::kBvOr:
      return "bvor";
    case Kind::kBvMul:
      return "bvmul";
    case Kind::kBvNeg:
      return "bvneg";
    case Kind::kBvNot:
      return "bvnot";
    case Kind::kBvAdd:
      return "bvadd";
    case Kind::kBvSub:
      return "bvsub";
    case Kind::kBvSdiv:
      return "bvsdiv";
    case Kind::kBvSlt:
      return "bvslt";
    case Kind::kBvConcat:
      return "concat";
    case Kind::kBvExtract:
      return "extract";
    case Kind::kBvSignExtend:
      return "sign_extend";
    case Kind::kBvZeroExtend:
      return "zero_extend";
    case Kind::kArraySelect:
      return "select";
    case Kind::kArrayStore:
      return "store";
    default:
      throw std::runtime_error("Unmapped comparison Kind " +
                               std::to_string(std::to_underlying(kind)));
  }
}

void QuoteSymbol(std::ostream& out, std::string_view name) {
  out << '|' << name << '|';
}
using DefinitionNames = std::unordered_map<DeclId, std::string>;
void Serialize(std::ostream& out, const DefinitionNames& definitions,
               NodeView root) {
  using Task = std::variant<NodeView, char>;
  std::vector<Task> work{root};
  while (!work.empty()) {
    const auto task = work.back();
    work.pop_back();
    if (const auto* ch = std::get_if<char>(&task)) {
      out << *ch;
      continue;
    }
    const auto term = std::get<NodeView>(task);
    const auto kind = term.GetKind();
    if (kind == Kind::kBoolValue) {
      out << (*term.GetValue()->GetBool() ? "true" : "false");
      continue;
    }
    if (kind == Kind::kIntValue) {
      const auto number = term.GetValue()->GetInteger()->ToDecimal();
      if (number.starts_with('-'))
        out << "(- " << number.substr(1) << ')';
      else
        out << number;
      continue;
    }
    if (kind == Kind::kBvValue) {
      out << "(_ bv" << term.GetValue()->GetInteger()->ToDecimal() << ' '
          << *term.GetSort().GetWidth() << ')';
      continue;
    }
    const auto children = term.GetChildren();
    if (!children.empty()) out << '(';
    if (kind == Kind::kApply) {
      const auto decl = term.GetDeclaration()->GetDeclaration();
      if (decl.GetKind() == DeclKind::kDatatypeTester) {
        out << "(_ is ";
        QuoteSymbol(out, decl.GetConstructor()->GetNameHint());
        out << ')';
      } else if (const auto definition = definitions.find(decl.GetId());
                 definition != definitions.end()) {
        out << definition->second;
      } else {
        if (decl.GetParameterCount() != 0)
          throw std::runtime_error(
              "Parameterized datatype comparison is unavailable");
        QuoteSymbol(out, decl.GetNameHint());
      }
    } else {
      const auto indices = term.GetIndices();
      if (!indices.empty()) out << "(_ ";
      out << Operator(kind);
      for (std::size_t i = 0; i < indices.size(); ++i)
        out << ' ' << indices[i].ToDecimal();
      if (!indices.empty()) out << ')';
    }
    if (!children.empty()) {
      work.push_back(')');
      for (auto i = children.size(); i > 0; --i) {
        work.push_back(children[i - 1]);
        work.push_back(' ');
      }
    }
  }
}
void ComparisonSort(std::ostream& out, SortView sort) {
  switch (sort.GetKind()) {
    case SortKind::kBool:
      out << "Bool";
      break;
    case SortKind::kInt:
      out << "Int";
      break;
    case SortKind::kBitVector:
      out << "(_ BitVec " << *sort.GetWidth() << ')';
      break;
    default:
      throw std::runtime_error("Sort outside the comparison inventory");
  }
}
std::string_view At(std::string_view input, SourceSpan span) {
  return input.substr(
      static_cast<std::size_t>(span.begin.offset),
      static_cast<std::size_t>(span.end.offset - span.begin.offset));
}
int Report(const Diagnostic& diagnostic) {
  std::cerr << diagnostic.where.begin.line << ':'
            << diagnostic.where.begin.column << ' ' << diagnostic.message
            << '\n';
  if (diagnostic.code == DiagnosticCode::kUnsupported) return 2;
  if (diagnostic.code == DiagnosticCode::kModelDependent) return 3;
  return 1;
}
bool SignatureCommand(CommandKind kind) noexcept {
  switch (kind) {
    case CommandKind::kSetLogic:
    case CommandKind::kDeclareSort:
    case CommandKind::kDeclareFun:
    case CommandKind::kDeclareConst:
    case CommandKind::kDefineSort:
    case CommandKind::kDefineFun:
    case CommandKind::kDefineConst:
    case CommandKind::kDeclareDatatype:
    case CommandKind::kDeclareDatatypes:
      return true;
    default:
      return false;
  }
}

int Probe(bool compare) {
  const std::string input((std::istreambuf_iterator<char>(std::cin)),
                          std::istreambuf_iterator<char>());
  if (std::cin.bad()) throw std::runtime_error("Input read failed");
  auto session = Session::FromText(input);
  if (!session) return Report(session.error());
  std::ostringstream validation;
  std::shared_ptr<const QueryContext> query;
  std::string_view considered_input = input;
  std::size_t commands = 0;
  while (true) {
    const auto syntax = session->NextSyntax();
    if (!syntax) return Report(syntax.error());
    if (!*syntax) break;
    auto typed = session->Apply();
    if (!typed) return Report(typed.error());
    if (typed->query) query = std::move(typed->query);
    ++commands;
    const auto header = typed->header;
    if (header.kind == CommandKind::kExit)
      considered_input = considered_input.substr(
          0, static_cast<std::size_t>(header.where.end.offset));
    if (compare && SignatureCommand(header.kind))
      validation << At(input, header.where) << '\n';
  }
  if (!query) throw std::runtime_error("A complete test input needs check-sat");
  std::cerr << query->GetLogic() << " commands=" << commands
            << " assertions=" << query->GetAssertions().size()
            << " functions=" << query->GetFunctions().size()
            << " definitions=" << query->GetDefinitions().size()
            << " datatype_groups=" << query->GetDatatypeGroups().size() << '\n';
  if (!compare) return 0;
  if (!query->GetNamedTerms().empty()) {
    std::cerr << "Comparison with named assertions is unavailable\n";
    return 3;
  }
  try {
    DefinitionNames definitions;
    std::string prefix = "dandelion_recovered_";
    while (considered_input.contains(prefix)) prefix.push_back('_');
    for (const auto& definition : query->GetDefinitions()) {
      if (!definition.formals.empty())
        throw std::runtime_error(
            "Comparison with non-nullary definitions is unavailable");
      auto name = prefix + std::to_string(definitions.size());
      validation << "(define-fun " << name << " () ";
      ComparisonSort(validation, definition.body.Borrow().GetSort());
      validation << ' ';
      Serialize(validation, definitions, definition.body.Borrow());
      validation << ")\n";
      definitions.emplace(definition.decl.GetId(), std::move(name));
    }
    if (query->GetAssertions().empty()) {
      validation << "(assert false)\n";
    } else {
      validation << "(assert (or false\n";
      for (const auto& assertion : query->GetAssertions()) {
        validation << "(xor " << At(input, assertion.where) << '\n';
        Serialize(validation, definitions, assertion.term.Borrow());
        validation << ")\n";
      }
      validation << "))\n";
    }
    std::cout << validation.str() << "(check-sat)\n";
  } catch (const std::runtime_error& error) {
    std::cerr << error.what() << '\n';
    return 3;
  }
  return std::cout ? 0 : 1;
}
}  // namespace
}  // namespace dandelion::frontend::testing

int main(int argc, char** argv) {
  const auto compare = argc == 2 && std::string_view(argv[1]) == "--compare";
  if (argc != 1 && !compare) {
    std::cerr << "Usage: dandelion_frontend_probe [--compare] < input.smt2\n";
    return 1;
  }
  try {
    return dandelion::frontend::testing::Probe(compare);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
