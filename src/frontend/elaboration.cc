module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {

void SessionState::ApplySemantic(const Syntax& syntax, TypedCommand& command) {
  const auto [kind, where] = command.header;
  Transaction txn(*this);
  const auto args = syntax.Children(0).subspan(1);
  switch (kind) {
    case CommandKind::kDeclareSort: {
      CheckSortName(*this, syntax, args[0], true);
      if (GetSignature().profile->declarations ==
          DeclarationPermission::kConstants)
        Fail(DiagnosticCode::kUnsupported, where,
             "Free sorts are outside the active profile");
      const auto arity = ParseArity(syntax, args[1]);
      const auto name = syntax.Name(args[0]);
      auto constructor =
          Take(manager.DeclareUninterpretedSort(name, arity), where);
      txn.AddSort(name, constructor.Borrow());
      txn.AddUserSort(
          {std::move(constructor), std::string(syntax.Raw(args[0])), where});
      break;
    }
    case CommandKind::kDefineSort: {
      CheckSortName(*this, syntax, args[0], true);
      Parameters parameters;
      const auto names = syntax.Children(args[1]);
      for (auto parameter : names) {
        CheckSortName(*this, syntax, parameter, false);
        const auto index = parameters.size();
        if (!parameters.emplace(syntax.Name(parameter), index).second)
          Fail(DiagnosticCode::kInvalidInput, syntax.Span(parameter),
               "Duplicate sort parameter");
      }
      const auto alias = ResolveAlias(*this, txn, syntax, args[2], parameters);
      txn.AddSort(syntax.Name(args[0]), alias);
      break;
    }
    case CommandKind::kDeclareDatatype:
    case CommandKind::kDeclareDatatypes:
      DeclareDatatypes(*this, txn, syntax);
      break;
    case CommandKind::kDeclareFun:
    case CommandKind::kDeclareConst:
    case CommandKind::kDefineFun:
    case CommandKind::kDefineConst:
    case CommandKind::kAssert:
      ApplyTerms(*this, syntax, command, txn);
      break;
    default:
      std::unreachable();
  }
  txn.Commit();
}

}  // namespace dandelion::frontend::detail
