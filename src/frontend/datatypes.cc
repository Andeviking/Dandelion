module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {

PendingDatatypeGroup::PendingDatatypeGroup(SessionState& session)
    : session_(session), group_(session.Prefix().GetDatatypeGroups().size()) {
#ifdef DANDELION_FRONTEND_TESTING
  session_.Checkpoint();
#endif
  session_.Count(&FrontendStats::staging_overlays);
}

void PendingDatatypeGroup::AddHeader(const Syntax& syntax, ExprId name,
                                     std::uint64_t arity) {
  CheckSortName(session_, syntax, name, true);
  const auto spelling = syntax.Name(name);
  const auto [entry, inserted] =
      sorts_.emplace(spelling, DatatypeSort{group_, sorts_.size(), arity});
  if (!inserted)
    Fail(DiagnosticCode::kInvalidInput, syntax.Span(name),
         "Sort name is already in use");
  headers_.push_back({entry->first, arity});
}

void PendingDatatypeGroup::Start(SourceSpan where) {
#ifdef DANDELION_FRONTEND_TESTING
  session_.Checkpoint();
#endif
  builder_.emplace(Take(session_.manager.BeginDatatypeGroup(headers_), where));
}

void PendingDatatypeGroup::ReadHeaders(const Syntax& syntax, ExprId list,
                                       SourceSpan where) {
  for (const auto header : syntax.Children(list)) {
    const auto parts = syntax.Children(header);
    AddHeader(syntax, parts[0], ParseArity(syntax, parts[1]));
  }
  Start(where);
}

void PendingDatatypeGroup::StartBody(const Syntax& syntax, ExprId body,
                                     ExprId name, ChildRange parameters) {
  if (in_body_) return;
  const auto where = syntax.Span(body);
  if (!builder_) {
    AddHeader(syntax, name, parameters.size());
    Start(where);
  }
  if (slot_ >= headers_.size())
    Fail(DiagnosticCode::kInvalidInput, where,
         "Mismatched datatype headers and bodies");
  if (parameters.size() != headers_[slot_].parameter_count)
    Fail(DiagnosticCode::kInvalidInput, where,
         "Datatype arity and par parameters disagree");
  for (const auto param : parameters) {
    CheckSortName(session_, syntax, param, false);
    if (!parameters_.emplace(syntax.Name(param), parameters_.size()).second)
      Fail(DiagnosticCode::kInvalidInput, syntax.Span(param),
           "Duplicate datatype parameter");
  }
  in_body_ = true;
}

std::string_view PendingDatatypeGroup::AddMember(
    const Syntax& syntax, ExprId name, std::optional<std::uint64_t> field) {
  CheckTermName(session_, syntax, name, true);
#ifdef DANDELION_FRONTEND_TESTING
  session_.Checkpoint();
#endif
  const auto [entry, inserted] = term_index_.emplace(syntax.Name(name));
  if (!inserted)
    Fail(DiagnosticCode::kInvalidInput, syntax.Span(name),
         "Term name is already in use");
  members_.emplace_back(*entry, DatatypeMember{group_, slot_, ctor_, field});
  return *entry;
}

void PendingDatatypeGroup::StartConstructor(const Syntax& syntax, ExprId name) {
  constructor_name_ = AddMember(syntax, name, std::nullopt);
}

SortExpr PendingDatatypeGroup::ReduceSort(const Syntax& syntax, ExprId expr) {
  return ResolveField(session_, syntax, expr, parameters_, *builder_, sorts_,
                      slot_);
}

void PendingDatatypeGroup::AddField(const Syntax& syntax, ExprId field) {
  const auto parts = syntax.Children(field);
  const auto name = AddMember(syntax, parts[0], fields_.size());
  const auto sort = *syntax.ReducedSort<SortExpr>(parts[1]);
  fields_.push_back({name, sort});
}

void PendingDatatypeGroup::EndConstructor(SourceSpan where) {
  Take(builder_->AddConstructor(slot_, constructor_name_, fields_), where);
  ++ctor_;
  fields_.clear();
}

void PendingDatatypeGroup::EndBody() noexcept {
  ++slot_;
  ctor_ = 0;
  parameters_.clear();
  in_body_ = false;
}

void PendingDatatypeGroup::Publish(Transaction& txn, SourceSpan where) {
  if (slot_ != headers_.size())
    Fail(DiagnosticCode::kInvalidInput, where,
         "Mismatched datatype headers and bodies");
  const auto group = txn.StageGroup();
  for (const auto& header : headers_)
    txn.AddSort(header.name_hint, sorts_.find(header.name_hint)->second);
  for (const auto& [name, member] : members_) txn.AddTerm(name, member);
#ifdef DANDELION_FRONTEND_TESTING
  session_.Checkpoint();
#endif
  txn.FinishGroup(group, Take(builder_->Commit(), where));
}

}  // namespace dandelion::frontend::detail
