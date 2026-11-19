module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {
namespace {
struct DatatypeSyntax {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  ExprId name;
  std::uint64_t arity;
  std::span<const ExprId> params;
  std::span<const ExprId> ctors;
  // NOLINTEND(readability-identifier-naming)
};
DatatypeSyntax InspectDatatype(const Syntax& syntax, ExprId name, ExprId body,
                               std::optional<std::uint64_t> arity) {
  const auto body_parts = syntax.Children(body);
  const bool is_parametric =
      !body_parts.empty() && syntax.Is(body_parts[0], "par");
  const auto params = is_parametric ? syntax.Children(body_parts[1])
                                    : std::span<const ExprId>{};
  const auto actual_arity = params.size();
  if (arity && *arity != actual_arity)
    Fail(DiagnosticCode::kInvalidInput, syntax.Span(body),
         "Datatype arity and par parameters disagree");
  return {name, actual_arity, params,
          is_parametric ? syntax.Children(body_parts[2]) : body_parts};
}
}  // namespace

void DeclareDatatypes(SessionState& session, Transaction& txn,
                      const Syntax& syntax) {
  const auto where = syntax.header.where;
  const auto args = syntax.Children(0).subspan(1);
  const bool plural = syntax.header.kind == CommandKind::kDeclareDatatypes;
  const auto written_headers =
      plural ? syntax.Children(args[0]) : std::span<const ExprId>{};
  const auto written_bodies =
      plural ? syntax.Children(args[1]) : std::span<const ExprId>{};
  if (written_headers.size() != written_bodies.size())
    Fail(DiagnosticCode::kInvalidInput, where,
         "Mismatched datatype headers and bodies");
  if (!session.GetSignature().profile->Has(kDatatypes))
    Fail(DiagnosticCode::kUnsupported, where,
         "Datatypes are outside the active signature");
  std::vector<DatatypeSyntax> datatypes;
  if (!plural) {
    datatypes.push_back(
        InspectDatatype(syntax, args[0], args[1], std::nullopt));
  } else {
    datatypes.reserve(written_headers.size());
    for (std::size_t i = 0; i < written_headers.size(); ++i) {
      const auto parts = syntax.Children(written_headers[i]);
      datatypes.push_back(InspectDatatype(syntax, parts[0], written_bodies[i],
                                          ParseArity(syntax, parts[1])));
    }
  }
  const auto group = txn.StageGroup();
  std::vector<DatatypeHeader> headers;
  headers.reserve(datatypes.size());
  for (std::size_t i = 0; i < datatypes.size(); ++i) {
    const auto& datatype = datatypes[i];
    CheckSortName(session, syntax, datatype.name, true);
    const auto name = syntax.Name(datatype.name);
    headers.push_back({name, datatype.arity});
    txn.AddSort(name, DatatypeSort{group, i, datatype.arity});
    for (std::size_t j = 0; j < datatype.ctors.size(); ++j) {
      const auto fields = syntax.Children(datatype.ctors[j]);
      CheckTermName(session, syntax, fields[0], true);
      txn.AddTerm(syntax.Name(fields[0]),
                  DatatypeMember{group, i, j, std::nullopt});
      for (std::size_t k = 1; k < fields.size(); ++k) {
        const auto selector = syntax.Children(fields[k])[0];
        CheckTermName(session, syntax, selector, true);
        txn.AddTerm(syntax.Name(selector), DatatypeMember{group, i, j, k - 1});
      }
    }
  }

  auto builder = Take(session.manager.BeginDatatypeGroup(headers), where);
  std::vector<DatatypeFieldSpec> field_specs;
  for (std::size_t i = 0; i < datatypes.size(); ++i) {
    const auto& datatype = datatypes[i];
    Parameters parameters;
    for (auto name : datatype.params) {
      CheckSortName(session, syntax, name, false);
      if (!parameters.emplace(syntax.Name(name), parameters.size()).second)
        Fail(DiagnosticCode::kInvalidInput, syntax.Span(name),
             "Duplicate datatype parameter");
    }
    for (auto ctor : datatype.ctors) {
      const auto fields = syntax.Children(ctor);
      field_specs.clear();
      for (auto field : fields.subspan(1)) {
        const auto pair = syntax.Children(field);
        const auto sort = ResolveField(session, syntax, pair[1], parameters,
                                       builder, group, i);
        field_specs.push_back({syntax.Name(pair[0]), sort});
      }
      Take(builder.AddConstructor(i, syntax.Name(fields[0]), field_specs),
           syntax.Span(ctor));
    }
  }
  session.Checkpoint();
  txn.FinishGroup(group, Take(builder.Commit(), where));
}
}  // namespace dandelion::frontend::detail
