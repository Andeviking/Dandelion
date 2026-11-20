module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {

void SessionState::ApplySemantic(const Syntax& syntax, TypedCommand& command,
                                 Node term, PendingTerms& pending,
                                 DefinitionInputs* definition,
                                 PendingDatatypeGroup* datatypes,
                                 PendingSortAlias* alias) {
  const auto [kind, where] = command.header;
  Transaction txn(*this);
  pending.Publish(txn);
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
      txn.AddUserSort(std::move(constructor), syntax.Raw(args[0]), where);
      break;
    }
    case CommandKind::kDefineSort: {
      CheckSortName(*this, syntax, args[0], true);
      if (alias) {
        alias->Publish(txn, args[0], args[2]);
      } else {
        const auto sort = ResolveSort(*this, syntax, args[2]);
        txn.AddSort(syntax.Name(args[0]),
                    TakeAt(manager.RetainSort(sort), {syntax, args[2]}));
      }
      break;
    }
    case CommandKind::kDeclareDatatype:
    case CommandKind::kDeclareDatatypes:
      datatypes->Publish(txn, where);
      break;
    case CommandKind::kDeclareFun:
    case CommandKind::kDeclareConst:
    case CommandKind::kDefineFun:
    case CommandKind::kDefineConst:
    case CommandKind::kAssert:
      ApplyTerms(*this, syntax, command, txn, std::move(term), definition);
      break;
    default:
      std::unreachable();
  }
  txn.Commit();
}

}  // namespace dandelion::frontend::detail
