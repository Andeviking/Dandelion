#include "model_support.h"

namespace {
thread_local std::optional<std::size_t> model_create_fail_after;
}

// The linker redirects scalar operator new for this test executable. When
// the countdown is disarmed, allocations use the original operator.
// NOLINTNEXTLINE(readability-identifier-naming): GNU linker ABI symbol.
extern "C" void* __real__Znwm(std::size_t bytes);
// NOLINTNEXTLINE(readability-identifier-naming): GNU linker ABI symbol.
extern "C" void* __wrap__Znwm(std::size_t bytes) {
  if (model_create_fail_after) {
    if (*model_create_fail_after == 0) {
      model_create_fail_after.reset();
      throw std::bad_alloc();
    }
    --*model_create_fail_after;
  }
  return __real__Znwm(bytes);
}

namespace dandelion::frontend::testing {
namespace {

constexpr std::string_view kModelOption = "(set-option :produce-models true)";
constexpr std::string_view kArrayModel =
    "(set-option :produce-models true)(set-logic ALL)(declare-sort U 0)"
    "(declare-const u U)(declare-const a (Array Bool Bool))"
    "(declare-const b (Array Bool Bool))(declare-const c (Array Bool Bool))"
    "(declare-datatype Wrap ((wrap (contents (Array Bool Bool)) (payload U))))"
    "(check-sat)";

// Independent two-entry table for the extensional interpretation used here.
std::array<bool, 2> ArrayValue(std::string_view name) {
  if (name == "c") return {false, true};
  if (name == "a") return {false, false};
  auto stored = std::array{true, true};
  stored[0] = false;
  stored[1] = false;
  return stored;
}
std::string ArrayKey(std::array<bool, 2> table) {
  return {table[0] ? '1' : '0', table[1] ? '1' : '0'};
}

Outcome<ValueRef> ArrayEvaluation(ModelOwner& model, NodeView term) {
  auto manager = Require(NodeManager::Attach(model.GetQuery().GetManager()));
  const auto kind = term.GetKind();
  if (kind == Kind::kArraySelect) {
    const auto args = term.GetChildren();
    const auto value = Require(ArrayEvaluation(model, args[0]));
    const auto index = args[1].GetValue()->GetBool().value() ? 1u : 0u;
    return Scalar(model,
                  Require(manager.MakeBool(value.GetKey()[index] == '1')));
  }
  if (kind == Kind::kApply) {
    const auto instance = *term.GetDeclaration();
    const auto decl = instance.GetDeclaration();
    if (decl.GetKind() == DeclKind::kInternalFunction)
      return model.LookupProxy(term);
    if (decl.GetKind() == DeclKind::kDatatypeConstructor) {
      const auto args = term.GetChildren();
      auto a = Require(ArrayEvaluation(model, args[0]));
      auto u = Require(ArrayEvaluation(model, args[1]));
      const auto id = std::string(a.GetKey()) + "-" + std::string(u.GetKey());
      auto type = Require(manager.RetainDeclInstance(instance));
      return model.MakeValue(
          term.GetSort(), id,
          std::make_shared<ValueDescription>(
              DatatypeValue{std::move(type), {std::move(a), std::move(u)}}));
    }
    if (term.GetSort().GetKind() == SortKind::kArray)
      return model.MakeValue(term.GetSort(),
                             ArrayKey(ArrayValue(decl.GetNameHint())));
    if (term.GetSort().GetKind() == SortKind::kUninterpreted)
      return model.MakeValue(term.GetSort(), "element");
  }
  if (term.GetValue()) return Scalar(model, Require(manager.RetainNode(term)));
  return std::unexpected(ModelError());
}

TEST(FrontendModel, CanonicalValuesArrayKeyCollisionsAndSameModelReadback) {
  const auto query = RequireQuery(Parse(kArrayModel));
  auto model = Require(ModelOwner::Create(
      query, std::make_unique<ControlledModel>(ArrayEvaluation),
      CheckAnswer::kSat));
  model->ForceKeyHashCollisions();
  EXPECT_EQ(ArrayKey(ArrayValue("a")), ArrayKey(ArrayValue("b")));
  EXPECT_NE(ArrayKey(ArrayValue("a")), ArrayKey(ArrayValue("c")));

  std::string input =
      "(get-value (a b c u (wrap b u)))"
      "(get-value ((select @value0 false)"
      " (select (as @value1 (Array Bool Bool)) true) @value2"
      " (as |@value2| U) (! true :source \"@value2\")))";
  auto session = Require(Session::ForModelText(model, input));
  CapturedSink sink;
  const auto run = dandelion::frontend::Run(session, {}, sink.Get());
  EXPECT_EQ(run.status, 0);
  EXPECT_EQ(run.model, model);
  EXPECT_EQ(
      sink.output,
      "((a (as @value0 (Array Bool Bool))) (b (as @value0 (Array Bool Bool)))"
      " (c (as @value1 (Array Bool Bool))) (u (as @value2 U))"
      " ((wrap b u) (wrap (as @value0 (Array Bool Bool)) (as @value2 U))))\n"
      "(((select (as @value0 (Array Bool Bool)) false) false)"
      " ((select (as @value1 (Array Bool Bool)) true) true)"
      " ((as @value2 U) (as @value2 U)) ((as @value2 U) (as @value2 U))"
      " ((! true :source \"@value2\") true))\n");

  for (const auto* term :
       {"@unknown", "(as @value0 U)", "(as @value2 (Array Bool Bool))"}) {
    const auto invalid = "(get-value (" + std::string(term) + "))";
    auto inspector = Require(Session::ForModelText(model, invalid));
    CapturedSink error;
    const auto stopped = dandelion::frontend::Run(inspector, {}, error.Get());
    EXPECT_EQ(stopped.status, 1);
    ASSERT_TRUE(stopped.diagnostic);
    EXPECT_EQ(stopped.diagnostic->code, DiagnosticCode::kInvalidInput);
  }
  ExpectError(Values("(as @value0 U)", "(set-logic ALL)(declare-sort U 0)"),
              DiagnosticCode::kModelDependent);
  ExpectError(
      Parse("(set-logic ALL)(declare-sort U 0)(assert (= @value0 @value0))"),
      DiagnosticCode::kInvalidInput);
  EXPECT_EQ(query->GetFunctions().size(), 4);
}

TEST(FrontendModel, ProxyAndValueHandlesRejectAnotherModelOnSameManager) {
  const auto query = RequireQuery(Parse(kArrayModel));
  const auto provider = [](std::string key) {
    return std::make_unique<ControlledModel>(
        [key = std::move(key)](ModelOwner& model,
                               NodeView term) -> Outcome<ValueRef> {
          if (Applied(term).GetKind() == DeclKind::kInternalFunction)
            return model.LookupProxy(term);
          return model.MakeValue(term.GetSort(), key);
        });
  };
  auto first =
      Require(ModelOwner::Create(query, provider("first"), CheckAnswer::kSat));
  auto second =
      Require(ModelOwner::Create(query, provider("second"), CheckAnswer::kSat));
  auto manager = Require(NodeManager::Attach(query->GetManager()));
  const auto term = ApplyDecl(manager, query->GetFunctions()[0].decl.Borrow());
  const auto foreign = Require(first->Evaluate(term.Borrow()));
  const auto proxy = Require(first->MakeProxy(foreign));
  EXPECT_EQ(Require(first->LookupProxy(proxy.GetTerm().Borrow())).GetKey(),
            "first");
  ExpectError(second->MakeProxy(foreign), DiagnosticCode::kModelFailure);
  ExpectError(second->LookupProxy(proxy.GetTerm().Borrow()),
              DiagnosticCode::kModelFailure);
  ExpectError(second->Evaluate(proxy.GetTerm().Borrow()),
              DiagnosticCode::kModelFailure);
  const auto wrapped =
      Builtin(manager, Kind::kEqual, {proxy.GetTerm().Borrow(), term.Borrow()});
  ExpectError(second->Evaluate(wrapped.Borrow()),
              DiagnosticCode::kModelFailure);

  for (const auto& model : {first, second}) {
    auto session =
        Require(Session::ForModelText(model, "(get-value (u @value0))"));
    CapturedSink sink;
    EXPECT_EQ(dandelion::frontend::Run(session, {}, sink.Get()).status, 1);
    EXPECT_TRUE(sink.output.starts_with("(error "));
    auto open = Require(Session::ForModelText(model, "(get-value (u))"));
    EXPECT_EQ(dandelion::frontend::Run(open, {}, sink.Get()).status, 0);
    auto again = Require(Session::ForModelText(model, "(get-value (@value0))"));
    const auto typed = RequireNext(again, CommandKind::kGetValue);
    ASSERT_EQ(typed.values.size(), 1);
    EXPECT_EQ(Require(model->Evaluate(typed.values[0].term.Borrow())).GetKey(),
              model == first ? "first" : "second");
  }
}

TEST(FrontendModel, ScalarCanonicalOutputAndFiniteness) {
  const auto input =
      std::string(kModelOption) +
      "(set-logic ALL)(check-sat)(get-value (true 18446744073709551616 #x01 RNE"
      " (_ NaN 3 3) (_ +oo 3 3) (_ -oo 3 3) (_ +zero 3 3) (_ -zero 3 3)"
      " (fp #b0 #b001 #b10)))";
  CapturedSink sink;
  const auto result = WithModel(
      input,
      std::make_unique<ControlledModel>([](ModelOwner& model,
                                           NodeView term) -> Outcome<ValueRef> {
        auto manager =
            Require(NodeManager::Attach(model.GetQuery().GetManager()));
        if (term.GetKind() == Kind::kFpConstruct) {
          const auto sort = term.GetSort();
          return Scalar(
              model, Require(manager.MakeFpBits(sort, IntegerView(0b000110))));
        }
        return Scalar(model, Require(manager.RetainNode(term)));
      }),
      sink);
  EXPECT_EQ(result.status, 0);
  EXPECT_EQ(
      sink.output,
      "sat\n((true true) (18446744073709551616 18446744073709551616)"
      " (#x01 #b00000001) (RNE RNE)"
      " ((_ NaN 3 3) (_ NaN 3 3)) ((_ +oo 3 3) (_ +oo 3 3))"
      " ((_ -oo 3 3) (_ -oo 3 3))"
      " ((_ +zero 3 3) (fp #b0 #b000 #b00)) ((_ -zero 3 3) (fp #b1 #b000 #b00))"
      " ((fp #b0 #b001 #b10) (fp #b0 #b001 #b10)))\n");
}

std::int64_t EvalSmallInt(
    const QueryContext& query, NodeView term,
    const std::unordered_map<DeclId, std::int64_t>& locals = {}) {
  const auto kind = term.GetKind();
  if (kind == Kind::kIntValue)
    return term.GetValue()->GetInteger()->TryInt64().value();
  const auto args = term.GetChildren();
  if (kind == Kind::kIntNeg) return -EvalSmallInt(query, args[0], locals);
  if (kind == Kind::kIntAdd || kind == Kind::kIntMul || kind == Kind::kIntSub) {
    const auto left = EvalSmallInt(query, args[0], locals);
    const auto right = EvalSmallInt(query, args[1], locals);
    if (kind == Kind::kIntAdd) return left + right;
    if (kind == Kind::kIntMul) return left * right;
    return left - right;
  }
  const auto decl = Applied(term);
  if (decl.GetKind() == DeclKind::kFormal) return locals.at(decl.GetId());
  const auto* definition = query.FindDefinition(decl);
  if (!definition)
    throw std::runtime_error("Unexpected free symbol in small Int oracle");
  std::unordered_map<DeclId, std::int64_t> bindings;
  for (std::size_t index = 0; index < args.size(); ++index)
    bindings.emplace(definition->formals[index].GetId(),
                     EvalSmallInt(query, args[index], locals));
  return EvalSmallInt(query, definition->body.Borrow(), bindings);
}

TEST(FrontendModel, RetainedQueryAndDefinitionsSurviveInputAndDriver) {
  std::shared_ptr<ModelOwner> model;
  CapturedSink sink;
  {
    const auto input =
        std::string(kModelOption) +
        "(set-logic ALL)(define-fun f ((a Int) (b Int)) Int (- a b))"
        "(define-fun g ((t Int)) Int (f t (f t 1)))(check-sat)(exit)";
    const auto result = WithModel(
        input,
        std::make_unique<ControlledModel>([](ModelOwner& owner, NodeView term) {
          return IntegerScalar(owner, EvalSmallInt(owner.GetQuery(), term));
        }),
        sink);
    ASSERT_EQ(result.status, 0);
    model = result.model;
  }
  ASSERT_TRUE(model);
  constexpr std::string_view request = "(get-value ((g 5) (f 6 3)))";
  auto reopen = Require(Session::ForModelReader(
      model,
      [remaining = request](std::span<char> bytes) mutable -> ReadResult {
        const auto count = std::min(remaining.size(), bytes.size());
        std::copy_n(remaining.data(), count, bytes.data());
        remaining.remove_prefix(count);
        return count;
      }));
  EXPECT_EQ(dandelion::frontend::Run(reopen, {}, sink.Get()).status, 0);
  EXPECT_EQ(sink.output, "sat\n(((g 5) 1) ((f 6 3) 3))\n");
}

TEST(FrontendModel,
     ExportBeforeValueSharesTotalInterpretationAndOriginalRanks) {
  const std::string input =
      std::string(kModelOption) +
      "(set-logic ALL)(declare-sort U 0)(define-sort Alias () U)"
      "(declare-fun f (Alias) Alias)(declare-const unused Bool)"
      "(define-const helper Int 0)(assert (! true :named yes))(check-sat)"
      "(get-model)(get-value ((f @value0) (f @value1) unused))";
  auto provider = std::make_unique<ControlledModel>(
      [](ModelOwner& model, NodeView term) -> Outcome<ValueRef> {
        const auto decl = Applied(term);
        auto nm = Require(NodeManager::Attach(model.GetQuery().GetManager()));
        if (decl.GetNameHint() == "unused")
          return Scalar(model, Require(nm.MakeBool(false)));
        const auto argument = Require(model.LookupProxy(term.GetChildren()[0]));
        return model.MakeValue(term.GetSort(),
                               argument.GetKey() == "zero" ? "one" : "zero");
      },
      ControlledModel::Description{},
      [](ModelOwner& model) -> Outcome<ModelExport> {
        const auto functions = model.GetQuery().GetFunctions();
        auto nm = Require(NodeManager::Attach(model.GetQuery().GetManager()));
        const auto sort = *functions[0].decl.Borrow().GetRange();
        const auto zero = Require(model.MakeValue(sort, "zero"));
        const auto one = Require(model.MakeValue(sort, "one"));
        auto z = Require(model.MakeProxy(zero));
        auto o = Require(model.MakeProxy(one));
        auto var = Require(nm.MakeFormal("unused", sort));
        const auto arg = ApplyDecl(nm, var.Borrow());
        const auto cond =
            Builtin(nm, Kind::kEqual, {arg.Borrow(), z.GetTerm().Borrow()});
        auto body = Builtin(
            nm, Kind::kIte,
            {cond.Borrow(), o.GetTerm().Borrow(), z.GetTerm().Borrow()});
        return ModelExport{
            {{functions[0].decl, {std::move(var)}, std::move(body)},
             {functions[1].decl, {}, Require(nm.MakeBool(false))}},
            {std::move(z), std::move(o)}};
      });
  CapturedSink sink;
  const auto result = WithModel(input, std::move(provider), sink);
  ASSERT_EQ(result.status, 0) << sink.output;
  EXPECT_TRUE(sink.output.starts_with("sat\n((define-fun f (("));
  EXPECT_TRUE(sink.output.contains(" Alias)) Alias (ite (= "));
  EXPECT_TRUE(
      sink.output.contains("(as @value0 U)) (as @value1 U) (as @value0 U))"));
  EXPECT_TRUE(sink.output.contains("(define-fun unused () Bool false)"));
  EXPECT_FALSE(sink.output.contains("define-fun helper"));
  EXPECT_FALSE(sink.output.contains("define-fun yes"));
  EXPECT_TRUE(
      sink.output.ends_with("(((f (as @value0 U)) (as @value1 U)) ((f (as "
                            "@value1 U)) (as @value0 U)) (unused false))\n"));
  // Substitute ordinary U constants for the abstract values solely to inspect
  // the exported binder and tuple-test identities in a fresh parser.
  auto records = sink.output.substr(5, sink.output.find('\n', 4) - 6);
  for (const auto& [value, symbol] :
       {std::pair{std::string_view("(as @value0 U)"), "zero"},
        {std::string_view("(as @value1 U)"), "one"}}) {
    for (auto offset = records.find(value); offset != std::string::npos;
         offset = records.find(value, offset))
      records.replace(offset, value.size(), symbol);
  }
  const auto recovered = RequireQuery(
      Parse("(set-logic ALL)(declare-sort U 0)(define-sort Alias () U)"
            "(declare-const zero U)(declare-const one U)" +
            records + "(check-sat)"));
  ASSERT_EQ(recovered->GetDefinitions().size(), 2);
  ASSERT_EQ(recovered->GetFunctions().size(), 2);
  const auto& f = recovered->GetDefinitions()[0];
  ASSERT_EQ(f.formals.size(), 1);
  ASSERT_EQ(f.body.Borrow().GetKind(), Kind::kIte);
  ASSERT_EQ(f.body.Borrow().GetChildren().size(), 3);
  const auto condition = Argument(f.body, 0);
  ASSERT_EQ(condition.GetKind(), Kind::kEqual);
  ASSERT_EQ(condition.GetChildren().size(), 2);
  EXPECT_EQ(Applied(condition.GetChildren()[0]), f.formals[0].Borrow());
  EXPECT_EQ(Applied(condition.GetChildren()[1]),
            recovered->GetFunctions()[0].decl.Borrow());
  EXPECT_EQ(Applied(Argument(f.body, 1)),
            recovered->GetFunctions()[1].decl.Borrow());
  EXPECT_EQ(Applied(Argument(f.body, 2)),
            recovered->GetFunctions()[0].decl.Borrow());
}

TEST(FrontendModel, SharedExportUsesCaptureSafeDependencyOrderedLetBindings) {
  const auto input =
      std::string(kModelOption) +
      "(set-logic ALL)(define-sort Alias () Int)(declare-const g Alias)"
      "(declare-const model_local0 Alias)"
      "(declare-fun f (Alias) Alias)(declare-fun id (Alias) Alias)(check-sat)"
      "(get-model)(get-value ((id 0) (id 6) (id (- 3))))";
  CapturedSink sink;
  const auto run = WithModel(
      input,
      std::make_unique<ControlledModel>(
          [](ModelOwner& model, NodeView term) {
            return IntegerScalar(
                model, EvalSmallInt(model.GetQuery(), term.GetChildren()[0]));
          },
          ControlledModel::Description{},
          [](ModelOwner& model) -> Outcome<ModelExport> {
            const auto functions = model.GetQuery().GetFunctions();
            auto nm =
                Require(NodeManager::Attach(model.GetQuery().GetManager()));
            const auto sort = Require(nm.IntSort());
            auto f_formal = Require(nm.MakeFormal("g", sort.Borrow()));
            auto id_formal = Require(nm.MakeFormal("g", sort.Borrow()));
            auto shared = ApplyDecl(nm, f_formal.Borrow());
            for (int i = 0; i < 12; ++i)
              shared = Builtin(nm, Kind::kIntAdd,
                               {shared.Borrow(), shared.Borrow()});
            const auto g = ApplyDecl(nm, functions[1].decl.Borrow());
            auto body =
                Builtin(nm, Kind::kIntAdd, {g.Borrow(), shared.Borrow()});
            auto id_body = ApplyDecl(nm, id_formal.Borrow());
            return ModelExport{
                {{functions[0].decl, {}, Require(nm.MakeInt(IntegerView(7)))},
                 {functions[1].decl, {}, Require(nm.MakeInt(IntegerView(7)))},
                 {functions[2].decl, {std::move(f_formal)}, std::move(body)},
                 {functions[3].decl,
                  {std::move(id_formal)},
                  std::move(id_body)}},
                {}};
          }),
      sink);
  ASSERT_EQ(run.status, 0) << sink.output;
  const auto model_end = sink.output.find('\n', 4);
  ASSERT_NE(model_end, std::string::npos);
  EXPECT_LT(model_end, 1800u);
  EXPECT_TRUE(sink.output.contains("(let (("));
  EXPECT_TRUE(
      sink.output.ends_with("(((id 0) 0) ((id 6) 6) ((id (- 3)) (- 3)))\n"));
  const auto body = sink.output.substr(5, model_end - 6);
  const auto script = "(set-logic ALL)(define-sort Alias () Int)" + body +
                      "(assert (= (f 2) 8199))(check-sat)";
  const auto parsed = RequireQuery(Parse(script));
  ASSERT_EQ(parsed->GetDefinitions().size(), 4);
  EXPECT_EQ(EvalSmallInt(*parsed, Argument(parsed->GetAssertions()[0].term, 0)),
            8199);
  const auto& f = parsed->GetDefinitions()[2];
  ASSERT_EQ(f.formals.size(), 1);
  EXPECT_NE(f.formals[0].Borrow().GetNameHint(), "model_local0");
  EXPECT_EQ(Applied(Argument(f.body, 0)),
            parsed->GetDefinitions()[1].decl.Borrow());
  const auto& id = parsed->GetDefinitions()[3];
  ASSERT_EQ(id.formals.size(), 1);
  ASSERT_EQ(id.body.Borrow().GetKind(), Kind::kApply);
  EXPECT_EQ(Applied(id.body.Borrow()), id.formals[0].Borrow());
}

TEST(FrontendModel, UfidlNegativeValuesReadBackAndExportAsBinarySubtraction) {
  const auto input = std::string(kModelOption) +
                     "(set-logic QF_UFIDL)(declare-const x Int)(declare-const "
                     "low Int)(check-sat)"
                     "(get-model)(get-value (x low (- 1) (+ (- 1) 2) (|-| 1)))";
  CapturedSink sink;
  const auto run = WithModel(
      input,
      std::make_unique<ControlledModel>(
          [](ModelOwner& model, NodeView term) {
            if (term.GetKind() == Kind::kApply) {
              const auto name = Applied(term).GetNameHint();
              return IntegerScalar(
                  model,
                  name == "x" ? -1 : std::numeric_limits<std::int64_t>::min());
            }
            return IntegerScalar(model, EvalSmallInt(model.GetQuery(), term));
          },
          ControlledModel::Description{},
          [](ModelOwner& model) -> Outcome<ModelExport> {
            const auto fs = model.GetQuery().GetFunctions();
            auto nm =
                Require(NodeManager::Attach(model.GetQuery().GetManager()));
            return ModelExport{
                {{fs[0].decl, {}, Require(nm.MakeInt(IntegerView(-1)))},
                 {fs[1].decl,
                  {},
                  Require(nm.MakeInt(
                      IntegerView(std::numeric_limits<std::int64_t>::min())))}},
                {}};
          }),
      sink);
  ASSERT_EQ(run.status, 0) << sink.output;
  EXPECT_EQ(sink.output,
            "sat\n((define-fun x () Int (- 0 1)) (define-fun low () Int (- 0 "
            "9223372036854775808)))\n"
            "((x (- 1)) (low (- 9223372036854775808)) ((- 1) (- 1))"
            " ((+ (- 1) 2) 1) ((|-| 1) (- 1)))\n");
  for (auto term : {"(- x)", "(- 0)", "((as - Int) 1)"}) {
    SCOPED_TRACE(term);
    ExpectError(Values(term, "(set-logic QF_UFIDL)(declare-const x Int)"),
                DiagnosticCode::kUnsupported);
  }
}

TEST(FrontendModel, RejectsIncompleteOrOutOfOrderExportsAndForeignValues) {
  constexpr std::string_view input =
      "(set-option :produce-models true)(set-logic ALL)(declare-const a Int)"
      "(declare-const b Int)(check-sat)(get-model)";
  for (int mode = 0; mode < 4; ++mode) {
    SCOPED_TRACE(mode);
    CapturedSink sink;
    const auto result = WithModel(
        input,
        std::make_unique<ControlledModel>(
            ControlledModel::Evaluation{}, ControlledModel::Description{},
            [mode](ModelOwner& model) -> Outcome<ModelExport> {
              const auto fs = model.GetQuery().GetFunctions();
              auto nm =
                  Require(NodeManager::Attach(model.GetQuery().GetManager()));
              const auto two = Require(nm.MakeInt(IntegerView(2)));
              if (mode == 0) return ModelExport{{{fs[0].decl, {}, two}}, {}};
              if (mode == 1)
                return ModelExport{
                    {{fs[0].decl, {}, ApplyDecl(nm, fs[1].decl.Borrow())},
                     {fs[1].decl, {}, two}},
                    {}};
              if (mode == 2)
                return ModelExport{
                    {{fs[0].decl, {}, two}, {fs[0].decl, {}, two}}, {}};
              return ModelExport{{{fs[0].decl, {}, Require(nm.MakeBool(false))},
                                  {fs[1].decl, {}, two}},
                                 {}};
            }),
        sink);
    EXPECT_EQ(result.status, 1);
    ASSERT_TRUE(result.diagnostic);
    EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kModelFailure);
    EXPECT_TRUE(sink.output.starts_with("sat\n"));
  }

  const auto foreign_query = RequireQuery(Parse(kArrayModel));
  auto foreign = Require(ModelOwner::Create(
      foreign_query, std::make_unique<ControlledModel>(ArrayEvaluation),
      CheckAnswer::kSat));
  auto nm = Require(NodeManager::Attach(foreign_query->GetManager()));
  const auto u = ApplyDecl(nm, foreign_query->GetFunctions()[0].decl.Borrow());
  const auto value = Require(foreign->Evaluate(u.Borrow()));
  CapturedSink sink;
  const auto result = WithModel(
      std::string(kArrayModel) + "(get-value (u))",
      std::make_unique<ControlledModel>(
          [&](ModelOwner&, NodeView) -> Outcome<ValueRef> { return value; }),
      sink);
  EXPECT_EQ(result.status, 1);
  EXPECT_TRUE(sink.output.starts_with("sat\n(error "));

  CapturedSink empty;
  const auto none = WithModel(
      "(set-option :produce-models true)(set-logic "
      "QF_SAT)(check-sat)(get-model)",
      std::make_unique<ControlledModel>(
          ControlledModel::Evaluation{}, ControlledModel::Description{},
          [](ModelOwner&) -> Outcome<ModelExport> { return ModelExport{}; }),
      empty);
  EXPECT_EQ(none.status, 0);
  EXPECT_EQ(empty.output, "sat\n()\n");
}

NodeView SelectRounding(NodeView term, DeclView formal, RoundingMode mode) {
  while (term.GetKind() == Kind::kIte) {
    const auto args = term.GetChildren();
    if (args.size() != 3 || args[0].GetKind() != Kind::kEqual ||
        args[0].GetChildren().size() != 2) {
      ADD_FAILURE() << "Expected an equality test on the rounding-mode formal";
      return {};
    }
    const auto equal = args[0].GetChildren();
    EXPECT_EQ(Applied(equal[0]), formal);
    const auto expected = equal[1].GetValue()->GetRoundingMode();
    term = args[expected == mode ? 1u : 2u];
  }
  return term;
}

TEST(FrontendModel, RationalConversionExportSurvivesLaterSlashDeclaration) {
  const auto input = std::string(kModelOption) +
                     "(set-logic ALL)(define-fun rounded ((r RoundingMode)) (_ "
                     "FloatingPoint 3 3)"
                     " ((_ to_fp 3 3) r (/ 1 3)))"
                     "(declare-fun / (Int Int) (_ BitVec 1))(declare-fun f "
                     "(RoundingMode) (_ FloatingPoint 3 3))"
                     "(check-sat)(get-model)";
  CapturedSink sink;
  const auto run = WithModel(
      input,
      std::make_unique<ControlledModel>(
          ControlledModel::Evaluation{}, ControlledModel::Description{},
          [](ModelOwner& model) -> Outcome<ModelExport> {
            auto nm =
                Require(NodeManager::Attach(model.GetQuery().GetManager()));
            const auto functions = model.GetQuery().GetFunctions();
            auto int_sort = Require(nm.IntSort());
            const auto& round = model.GetQuery().GetDefinitions()[0];
            const auto bv = *functions[0].decl.Borrow().GetRange();
            return ModelExport{
                {{functions[0].decl,
                  {Require(nm.MakeFormal("a", int_sort.Borrow())),
                   Require(nm.MakeFormal("b", int_sort.Borrow()))},
                  Require(nm.MakeBvValue(bv, IntegerView(0)))},
                 {functions[1].decl, round.formals, round.body}},
                {}};
          }),
      sink);
  ASSERT_EQ(run.status, 0) << sink.output;
  EXPECT_FALSE(sink.output.contains("(/ 1 3)"));
  EXPECT_TRUE(sink.output.contains("(ite "));
  const auto body = sink.output.substr(5, sink.output.size() - 7);
  const auto recovered =
      RequireQuery(Parse("(set-logic ALL)" + body + "(check-sat)"));
  ASSERT_EQ(recovered->GetDefinitions().size(), 2);
  const auto& function = recovered->GetDefinitions()[1];
  ASSERT_EQ(function.formals.size(), 1);
  for (const auto mode :
       {RoundingMode::kRne, RoundingMode::kRna, RoundingMode::kRtp,
        RoundingMode::kRtn, RoundingMode::kRtz}) {
    const auto value = SelectRounding(function.body.Borrow(),
                                      function.formals[0].Borrow(), mode);
    ASSERT_FALSE(value.IsNull());
    ASSERT_EQ(value.GetKind(), Kind::kFpConstruct);
    const auto args = value.GetChildren();
    EXPECT_EQ(IntegerValue(args[0]), "0");
    EXPECT_EQ(IntegerValue(args[1]), "1");
    EXPECT_EQ(IntegerValue(args[2]), mode == RoundingMode::kRtp ? "2" : "1");
  }
}

TEST(FrontendModel, LaterInspectionCanReachNewConcreteSorts) {
  const auto input =
      std::string(kModelOption) +
      "(set-logic ALL)(declare-sort U 1)(declare-const b Bool)(check-sat)"
      "(get-value (b))(get-value (((as const (Array (U Bool) Int)) "
      "1)))(get-value (b))";
  CapturedSink sink;
  const auto run = WithModel(
      input,
      std::make_unique<ControlledModel>([](ModelOwner& model,
                                           NodeView term) -> Outcome<ValueRef> {
        auto nm = Require(NodeManager::Attach(model.GetQuery().GetManager()));
        if (term.GetKind() == Kind::kArrayConst)
          return model.MakeValue(term.GetSort(), "all-ones");
        return Scalar(model, Require(nm.MakeBool(true)));
      }),
      sink);
  EXPECT_EQ(run.status, 0);
  EXPECT_EQ(sink.output,
            "sat\n((b true))\n"
            "((((as const (Array (U Bool) Int)) 1) (as @value0 (Array (U Bool) "
            "Int))))\n((b true))\n");
}

TEST(FrontendModel, StoreOnExposedArrayPreservesExtensionalKey) {
  CapturedSink sink;
  const auto input = std::string(kArrayModel) +
                     "(get-value (a c))(get-value ((store @value1 true false)))"
                     "(get-value ((select @value0 true)))";
  const auto run = WithModel(
      input,
      std::make_unique<ControlledModel>(
          [](ModelOwner& model, NodeView term) -> Outcome<ValueRef> {
            if (term.GetKind() != Kind::kArrayStore)
              return ArrayEvaluation(model, term);
            const auto children = term.GetChildren();
            const auto prior = Require(model.LookupProxy(children[0]));
            auto key = std::string(prior.GetKey());
            const auto pos = *children[1].GetValue()->GetBool() ? 1u : 0u;
            key[pos] = *children[2].GetValue()->GetBool() ? '1' : '0';
            return model.MakeValue(term.GetSort(), std::move(key));
          }),
      sink);
  EXPECT_EQ(run.status, 0) << sink.output;
  EXPECT_TRUE(sink.output.ends_with(
      "(((store (as @value1 (Array Bool Bool)) true false) (as @value0 (Array "
      "Bool Bool))))\n"
      "(((select (as @value0 (Array Bool Bool)) true) false))\n"));
}

TEST(FrontendModel, FpRoundingFailureRespectsModelResponseBoundary) {
  constexpr std::string_view input =
      "(set-option :produce-models true)(set-logic ALL)"
      "(define-fun rounded ((r RoundingMode)) (_ FloatingPoint 65 3)"
      " ((_ to_fp 65 3) r (/ 1 3)))"
      "(declare-const / Bool)(declare-fun f (RoundingMode) (_ FloatingPoint 65 "
      "3))"
      "(check-sat)(get-model)(echo \"tail\")";
  for (const bool after_first : {false, true}) {
    SCOPED_TRACE(after_first);
    CapturedSink sink;
    const auto run = WithModel(
        input,
        std::make_unique<ControlledModel>(
            ControlledModel::Evaluation{}, ControlledModel::Description{},
            [after_first](ModelOwner& model) -> Outcome<ModelExport> {
              const auto functions = model.GetQuery().GetFunctions();
              const auto& round = model.GetQuery().GetDefinitions()[0];
              auto manager =
                  Require(NodeManager::Attach(model.GetQuery().GetManager()));
              auto export_model = ModelExport{
                  {{functions[0].decl, {}, Require(manager.MakeBool(false))},
                   {functions[1].decl, round.formals, round.body}},
                  {}};
              if (!after_first) std::ranges::reverse(export_model.functions);
              NumericTestPeer::FailAllocationAfter(0);
              return export_model;
            }),
        sink);
    NumericTestPeer::ClearFailures();
    EXPECT_EQ(run.status, 1);
    ASSERT_TRUE(run.diagnostic);
    EXPECT_EQ(run.diagnostic->code, DiagnosticCode::kResource);
    ASSERT_TRUE(run.diagnostic->node_error);
    EXPECT_EQ(run.diagnostic->node_error->resource,
              ResourceKind::kCppAllocation);
    if (after_first) {
      EXPECT_EQ(sink.output, "sat\n((define-fun / () Bool false)");
    } else {
      EXPECT_TRUE(sink.output.starts_with("sat\n(error "));
      EXPECT_TRUE(sink.output.ends_with("\")\n"));
    }
  }
}

TEST(FrontendModel, ParameterizedDatatypeValuesUseNecessaryQualifiers) {
  const auto input = std::string(kModelOption) +
                     "(set-logic ALL)(declare-datatype List (par (T)"
                     " ((nil)(cons (head T)(tail (List T))))))"
                     "(check-sat)(get-value ((as nil (List Int)) (cons 5 (as "
                     "nil (List Int)))))";
  CapturedSink sink;
  auto eval = [](this auto&& recurse, ModelOwner& model,
                 NodeView term) -> Outcome<ValueRef> {
    auto nm = Require(NodeManager::Attach(model.GetQuery().GetManager()));
    if (term.GetValue()) return Scalar(model, Require(nm.RetainNode(term)));
    const auto instance = *term.GetDeclaration();
    std::vector<ValueRef> fields;
    for (auto field : term.GetChildren())
      fields.push_back(Require(recurse(model, field)));
    const auto ctor = Require(nm.RetainDeclInstance(instance));
    return model.MakeValue(term.GetSort(), std::to_string(term.GetId()),
                           std::make_shared<ValueDescription>(
                               DatatypeValue{ctor, std::move(fields)}));
  };
  const auto result =
      WithModel(input, std::make_unique<ControlledModel>(eval), sink);
  EXPECT_EQ(result.status, 0) << sink.output;
  EXPECT_EQ(sink.output,
            "sat\n(((as nil (List Int)) (as nil (List Int)))"
            " ((cons 5 (as nil (List Int))) (cons 5 (as nil (List Int)))))\n");
}

TEST(FrontendModel, ValidNodeRanksCannotSmuggleSortsOutsideTheSignature) {
  constexpr std::string_view input =
      "(set-option :produce-models true)(set-logic ALL)(declare-sort U 0)"
      "(declare-const f Bool)(check-sat)(get-model)";
  CapturedSink sink;
  const auto result = WithModel(
      input,
      std::make_unique<ControlledModel>(
          ControlledModel::Evaluation{}, ControlledModel::Description{},
          [](ModelOwner& model) -> Outcome<ModelExport> {
            const auto function = model.GetQuery().GetFunctions()[0].decl;
            auto nm =
                Require(NodeManager::Attach(model.GetQuery().GetManager()));
            const auto alias = Require(nm.DeclareUninterpretedSort("U", 0));
            const auto impostor =
                Require(nm.InstantiateSort(alias.Borrow(), {}));
            const auto value =
                Require(model.MakeValue(impostor.Borrow(), "foreign-sort"));
            auto proxy = Require(model.MakeProxy(value));
            auto body =
                Builtin(nm, Kind::kEqual,
                        {proxy.GetTerm().Borrow(), proxy.GetTerm().Borrow()});
            return ModelExport{{{function, {}, std::move(body)}},
                               {std::move(proxy)}};
          }),
      sink);
  EXPECT_EQ(result.status, 1);
  ASSERT_TRUE(result.diagnostic);
  EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kModelFailure);
  EXPECT_TRUE(sink.output.starts_with("sat\n(error "));
  EXPECT_FALSE(sink.output.contains("@value0"));

  const auto query = RequireQuery(
      Parse("(set-option :produce-models true)(set-logic QF_FP)(check-sat)"));
  auto nm = Require(NodeManager::Attach(query->GetManager()));
  const auto sort = Require(nm.MakeBvSort(IntegerView(1)));
  const auto value = Require(nm.MakeBvValue(sort.Borrow(), IntegerView(0)));
  const auto unsupported = Builtin(nm, Kind::kBvNot, {value.Borrow()});
  int calls = 0;
  auto model = Require(ModelOwner::Create(
      query,
      std::make_unique<ControlledModel>([&](ModelOwner& owner, NodeView term) {
        ++calls;
        return Scalar(owner,
                      Require(nm.MakeBvValue(term.GetSort(), IntegerView(1))));
      }),
      CheckAnswer::kSat));
  ExpectError(model->Evaluate(unsupported.Borrow()),
              DiagnosticCode::kModelFailure);
  EXPECT_EQ(calls, 0);
  EXPECT_TRUE(Require(model->Evaluate(value.Borrow())).GetSort().GetWidth() ==
              1u);
  EXPECT_EQ(calls, 1);
}

class QueryLifetimeProvider : public ControlledModel {
 public:
  QueryLifetimeProvider(std::weak_ptr<const QueryContext> query, int& destroyed,
                        bool& live_during_destruction)
      : query_(std::move(query)),
        destroyed_(destroyed),
        live_during_destruction_(live_during_destruction) {}

  ~QueryLifetimeProvider() override {
    ++destroyed_;
    const auto live = query_.lock();
    live_during_destruction_ = live && live->GetLogic() == "ALL";
  }

 private:
  std::weak_ptr<const QueryContext> query_;
  int& destroyed_;
  bool& live_during_destruction_;
};

TEST(FrontendModel, CreateDestroysProviderBeforeSoleQueryOnEveryExit) {
  const auto exercise = [](bool enabled, CheckAnswer answer,
                           std::optional<std::size_t> checkpoint) {
    const auto input =
        std::string(enabled ? kModelOption : "") + "(set-logic ALL)(check-sat)";
    SCOPED_TRACE(::testing::Message()
                 << "FE-MODEL-CREATE-02 " << input << " answer "
                 << std::to_underlying(answer));
    auto query = RequireQuery(Parse(input));
    const std::weak_ptr<const QueryContext> sentinel = query;
    int destroyed = 0;
    bool live = false;
    std::unique_ptr<ModelProvider> provider =
        std::make_unique<QueryLifetimeProvider>(sentinel, destroyed, live);
    EXPECT_EQ(query.use_count(), 1);
    model_create_fail_after = checkpoint;
    auto owner =
        ModelOwner::Create(std::move(query), std::move(provider), answer);
    model_create_fail_after.reset();
    EXPECT_FALSE(query);
    EXPECT_FALSE(provider);
    const bool success = owner.has_value();
    if (!enabled || answer == CheckAnswer::kUnsat) {
      EXPECT_FALSE(success);
      ExpectError(owner, DiagnosticCode::kModelFailure);
    } else if (success) {
      EXPECT_FALSE(sentinel.expired());
      EXPECT_EQ(destroyed, 0);
      owner->reset();
    } else {
      EXPECT_TRUE(checkpoint.has_value());
      EXPECT_EQ(owner.error().code, DiagnosticCode::kResource);
      EXPECT_TRUE(owner.error().node_error);
      if (owner.error().node_error) {
        EXPECT_EQ(owner.error().node_error->resource,
                  ResourceKind::kCppAllocation);
      }
    }
    EXPECT_EQ(destroyed, 1);
    EXPECT_TRUE(live);
    EXPECT_TRUE(sentinel.expired());
    return success;
  };
  EXPECT_FALSE(exercise(true, CheckAnswer::kUnsat, std::nullopt));
  EXPECT_FALSE(exercise(false, CheckAnswer::kSat, std::nullopt));
  EXPECT_TRUE(exercise(true, CheckAnswer::kSat, std::nullopt));
  std::size_t failures = 0;
  for (std::size_t checkpoint = 0; checkpoint < 32; ++checkpoint) {
    SCOPED_TRACE(checkpoint);
    if (exercise(true, CheckAnswer::kSat, checkpoint)) break;
    ++failures;
  }
  EXPECT_GT(failures, 0u);
  EXPECT_LT(failures, 32u);
}

Outcome<ValueRef> NamedAbstractValue(ModelOwner& owner, NodeView term) {
  const auto decl = Applied(term);
  if (decl.GetKind() == DeclKind::kInternalFunction)
    return owner.LookupProxy(term);
  return owner.MakeValue(term.GetSort(), std::string(decl.GetNameHint()));
}

RunResult Inspect(std::shared_ptr<ModelOwner> owner, std::string_view request,
                  CapturedSink& sink) {
  auto session = Require(Session::ForModelText(std::move(owner), request));
  const auto result = dandelion::frontend::Run(session, {}, sink.Get());
  EXPECT_EQ(session.GetPhase(), SessionPhase::kTerminated);
  return result;
}

void CheckPublishedNames(const std::shared_ptr<ModelOwner>& owner) {
  constexpr std::string_view input =
      "(get-value (@value0))(get-value (@value1))(get-value (@value2))";
  auto session = Require(Session::ForModelText(owner, input));
  const auto prior = RequireNext(session, CommandKind::kGetValue);
  ASSERT_EQ(prior.values.size(), 1);
  EXPECT_EQ(Require(owner->LookupProxy(prior.values[0].term.Borrow())).GetKey(),
            "prior");
  ExpectError(Next(session), DiagnosticCode::kInvalidInput);
  ExpectError(Next(session), DiagnosticCode::kInvalidInput);
  EXPECT_FALSE(Require(session.NextSyntax()));
}

TEST(FrontendModel, ValueResponsePublishesStagedNamesOnlyAfterFlush) {
  constexpr std::string_view setup =
      "(set-option :produce-models true)(set-logic ALL)(declare-sort U 0)"
      "(declare-const prior U)(declare-const fresh U)(declare-const scalar Int)"
      "(declare-const distinct_value U)(check-sat)";
  enum class Fault {
    kEvaluate,
    kDescribe,
    kDescribeThrow,
    kShortWrite,
    kFlush
  };
  for (const auto fault :
       {Fault::kEvaluate, Fault::kDescribe, Fault::kDescribeThrow,
        Fault::kShortWrite, Fault::kFlush}) {
    const bool describe_fault =
        fault == Fault::kDescribe || fault == Fault::kDescribeThrow;
    const bool provider_fault = fault == Fault::kEvaluate || describe_fault;
    const auto request = provider_fault
                             ? "(get-value (fresh scalar))"
                             : "(get-value (fresh fresh distinct_value))";
    const auto script =
        std::string("(get-value (prior))") + request + "(echo \"unread\")";
    SCOPED_TRACE(::testing::Message()
                 << "FE-MODEL-COMMIT-02 " << setup << script << " fault "
                 << std::to_underlying(fault));
    int evaluations = 0;
    int descriptions = 0;
    CapturedSink sink;
    const auto checked = WithModel(
        setup,
        std::make_unique<ControlledModel>(
            [&](ModelOwner& model, NodeView term) -> Outcome<ValueRef> {
              ++evaluations;
              if (term.GetSort().GetKind() != SortKind::kInt)
                return NamedAbstractValue(model, term);
              if (fault == Fault::kEvaluate)
                return std::unexpected(ModelError());
              return IntegerScalar(model, 7);
            },
            [&](ModelOwner&, const ValueRef&) -> Outcome<ValueDescription> {
              ++descriptions;
              if (fault == Fault::kDescribeThrow)
                throw std::runtime_error("Mid-response description fault");
              return std::unexpected(ModelError());
            }),
        sink);
    ASSERT_EQ(checked.status, 0);
    ASSERT_TRUE(checked.model);
    const auto& owner = checked.model;
    constexpr std::string_view earlier = "sat\n((prior (as @value0 U)))\n";
    constexpr std::string_view partial = "((fresh (as @value1 U))";
    constexpr std::string_view complete =
        "((fresh (as @value1 U)) (fresh (as @value1 U))"
        " (distinct_value (as @value2 U)))\n";
    constexpr std::string_view retry =
        "((fresh (as @value1 U)) (fresh (as @value1 U)))\n";
    if (fault == Fault::kShortWrite)
      sink.StopAfter(earlier.size() + partial.size() + 3);
    if (fault == Fault::kFlush) sink.BreakFlush(3);
    const auto result = Inspect(owner, script, sink);
    ASSERT_EQ(result.status, 1);
    ASSERT_TRUE(result.diagnostic);
    EXPECT_EQ(result.diagnostic->code, provider_fault
                                           ? DiagnosticCode::kModelFailure
                                           : DiagnosticCode::kOutputIo);
    EXPECT_EQ(evaluations, fault == Fault::kFlush ? 4 : 3);
    EXPECT_EQ(descriptions, describe_fault ? 1 : 0);
    EXPECT_EQ(sink.flushes, fault == Fault::kFlush ? 3u : 2u);
    EXPECT_EQ(sink.output,
              std::string(earlier) +
                  std::string(provider_fault ? partial
                              : fault == Fault::kShortWrite
                                  ? complete.substr(0, partial.size() + 3)
                                  : complete));
    CheckPublishedNames(owner);
    CapturedSink success;
    EXPECT_EQ(Inspect(owner, "(get-value (fresh fresh))", success).status, 0);
    EXPECT_EQ(success.output, retry);
    EXPECT_EQ(success.flushes, 1);
    CapturedSink readback;
    EXPECT_EQ(Inspect(owner, "(get-value (@value1))", readback).status, 0);
    EXPECT_EQ(readback.output, "(((as @value1 U) (as @value1 U)))\n");
  }
}

TEST(FrontendModel, ModelResponseCommitsAllProxyNamesTogether) {
  constexpr std::string_view setup =
      "(set-option :produce-models true)(set-logic ALL)(declare-sort U 0)"
      "(declare-const prior U)(declare-const fresh U)(declare-const second U)"
      "(declare-const different U)(check-sat)";
  enum class Fault { kRecord, kShortWrite, kFlush };
  for (auto fault : {Fault::kRecord, Fault::kShortWrite, Fault::kFlush}) {
    constexpr std::string_view script =
        "(get-value (prior))(get-model)(echo \"unread\")";
    SCOPED_TRACE(::testing::Message()
                 << "FE-MODEL-COMMIT-02 " << setup << script << " fault "
                 << std::to_underlying(fault));
    bool valid = fault != Fault::kRecord;
    int exports = 0;
    CapturedSink sink;
    const auto checked = WithModel(
        setup,
        std::make_unique<ControlledModel>(
            [](ModelOwner& model, NodeView term) {
              if (Applied(term).GetNameHint() == "second")
                return model.MakeValue(term.GetSort(), "fresh");
              return NamedAbstractValue(model, term);
            },
            ControlledModel::Description{},
            [&](ModelOwner& model) -> Outcome<ModelExport> {
              ++exports;
              const auto functions = model.GetQuery().GetFunctions();
              const auto sort = *functions[0].decl.Borrow().GetRange();
              auto prior = Require(
                  model.MakeProxy(Require(model.MakeValue(sort, "prior"))));
              auto fresh = Require(
                  model.MakeProxy(Require(model.MakeValue(sort, "fresh"))));
              auto duplicate = Require(model.MakeProxy(fresh.GetValue()));
              auto different = Require(
                  model.MakeProxy(Require(model.MakeValue(sort, "different"))));
              return ModelExport{{{functions[1].decl, {}, fresh.GetTerm()},
                                  {functions[2].decl,
                                   {},
                                   valid ? duplicate.GetTerm() : Node{}},
                                  {functions[0].decl, {}, prior.GetTerm()},
                                  {functions[3].decl, {}, different.GetTerm()}},
                                 {fresh, duplicate, prior, different}};
            }),
        sink);
    ASSERT_EQ(checked.status, 0);
    ASSERT_TRUE(checked.model);
    const auto& owner = checked.model;
    constexpr std::string_view earlier = "sat\n((prior (as @value0 U)))\n";
    constexpr std::string_view partial =
        "((define-fun fresh () U (as @value1 U))";
    constexpr std::string_view complete =
        "((define-fun fresh () U (as @value1 U))"
        " (define-fun second () U (as @value1 U))"
        " (define-fun prior () U (as @value0 U))"
        " (define-fun different () U (as @value2 U)))\n";
    if (fault == Fault::kShortWrite)
      sink.StopAfter(earlier.size() + partial.size() + 3);
    if (fault == Fault::kFlush) sink.BreakFlush(3);
    const auto result = Inspect(owner, script, sink);
    ASSERT_EQ(result.status, 1);
    ASSERT_TRUE(result.diagnostic);
    EXPECT_EQ(result.diagnostic->code, fault == Fault::kRecord
                                           ? DiagnosticCode::kModelFailure
                                           : DiagnosticCode::kOutputIo);
    EXPECT_EQ(exports, 1);
    EXPECT_EQ(sink.flushes, fault == Fault::kFlush ? 3u : 2u);
    EXPECT_EQ(sink.output,
              std::string(earlier) +
                  std::string(fault == Fault::kRecord ? partial
                              : fault == Fault::kShortWrite
                                  ? complete.substr(0, partial.size() + 3)
                                  : complete));
    CheckPublishedNames(owner);
    valid = true;
    CapturedSink retry;
    EXPECT_EQ(Inspect(owner, "(get-model)", retry).status, 0);
    EXPECT_EQ(exports, 2);
    EXPECT_EQ(retry.output, complete);
    EXPECT_EQ(retry.flushes, 1);
    CapturedSink readback;
    EXPECT_EQ(Inspect(owner, "(get-value (@value1 @value2))", readback).status,
              0);
    EXPECT_EQ(
        readback.output,
        "(((as @value1 U) (as @value1 U)) ((as @value2 U) (as @value2 U)))\n");
  }
}

TEST(FrontendModel, ExposureReservationFailurePrecedesTheClosingWrite) {
  const auto query = RequireQuery(
      Parse("(set-option :produce-models true)(set-logic ALL)(declare-sort U 0)"
            "(declare-const prior U)(declare-const fresh U)"
            "(declare-const different U)(check-sat)"));
  for (bool model_request : {false, true}) {
    const auto request =
        model_request ? "(get-model)" : "(get-value (fresh different))";
    const std::string_view body =
        model_request ? "((define-fun prior () U (as @value0 U))"
                        " (define-fun fresh () U (as @value1 U))"
                        " (define-fun different () U (as @value2 U))"
                      : "((fresh (as @value1 U)) (different (as @value2 U))";
    for (bool with_flush : {false, true}) {
      for (std::size_t checkpoint : {0u, 1u}) {
        SCOPED_TRACE(::testing::Message()
                     << "FE-MODEL-RESERVE-02 " << request << " flush "
                     << with_flush << " allocation " << checkpoint);
        auto owner = Require(ModelOwner::Create(
            query,
            std::make_unique<ControlledModel>(
                NamedAbstractValue, ControlledModel::Description{},
                [](ModelOwner& model) -> Outcome<ModelExport> {
                  ModelExport result;
                  for (const auto& function : model.GetQuery().GetFunctions()) {
                    const auto declaration = function.decl.Borrow();
                    auto value = Require(model.MakeValue(
                        *declaration.GetRange(),
                        std::string(declaration.GetNameHint())));
                    auto proxy = Require(model.MakeProxy(value));
                    result.functions.push_back(
                        {function.decl, {}, proxy.GetTerm()});
                    result.proxies.push_back(std::move(proxy));
                  }
                  return result;
                }),
            CheckAnswer::kSat));
        CapturedSink prior;
        ASSERT_EQ(Inspect(owner, "(get-value (prior))", prior).status, 0);
        ASSERT_EQ(prior.output, "((prior (as @value0 U)))\n");

        auto session = Require(Session::ForModelText(owner, request));
        std::string output;
        output.reserve(body.size() + 2);
        std::size_t flushes = 0;
        ResponseSink sink{[&](std::string_view bytes) -> WriteResult {
                            output.append(bytes);
                            if (output.size() == body.size())
                              model_create_fail_after = checkpoint;
                            return bytes.size();
                          },
                          {}};
        if (with_flush) {
          sink.flush = [&]() -> std::expected<void, std::error_code> {
            ++flushes;
            return {};
          };
        }
        const auto failed = dandelion::frontend::Run(session, {}, sink);
        model_create_fail_after.reset();
        EXPECT_EQ(failed.status, 1);
        EXPECT_EQ(output, body);
        EXPECT_EQ(flushes, 0u);
        EXPECT_EQ(session.GetPhase(), SessionPhase::kTerminated);
        ASSERT_TRUE(failed.diagnostic);
        EXPECT_EQ(failed.diagnostic->code, DiagnosticCode::kResource);
        ASSERT_TRUE(failed.diagnostic->node_error);
        EXPECT_EQ(failed.diagnostic->node_error->resource,
                  ResourceKind::kCppAllocation);
        CheckPublishedNames(owner);

        CapturedSink retry;
        ASSERT_EQ(Inspect(owner, request, retry).status, 0);
        EXPECT_EQ(retry.output, std::string(body) + ")\n");
        EXPECT_EQ(retry.flushes, 1u);
        CapturedSink readback;
        EXPECT_EQ(
            Inspect(owner, "(get-value (@value1 @value2))", readback).status,
            0);
        EXPECT_EQ(readback.output,
                  "(((as @value1 U) (as @value1 U))"
                  " ((as @value2 U) (as @value2 U)))\n");
      }
    }
  }
}

TEST(FrontendModel, InspectionApplyIsBoundToTheOriginalOwner) {
  const auto query = RequireQuery(Parse(kArrayModel));
  int first_calls = 0;
  int second_calls = 0;
  const auto provider = [](std::string_view key, int& calls) {
    return std::make_unique<ControlledModel>(
        [key, &calls](ModelOwner& owner, NodeView term) -> Outcome<ValueRef> {
          ++calls;
          if (Applied(term).GetKind() == DeclKind::kInternalFunction)
            return owner.LookupProxy(term);
          return owner.MakeValue(term.GetSort(), std::string(key));
        });
  };
  auto first = Require(ModelOwner::Create(query, provider("first", first_calls),
                                          CheckAnswer::kSat));
  auto second = Require(ModelOwner::Create(
      query, provider("second", second_calls), CheckAnswer::kSat));
  ASSERT_EQ(&first->GetQuery(), &second->GetQuery());
  for (const auto& owner : {first, second}) {
    CapturedSink sink;
    EXPECT_EQ(Inspect(owner, "(get-value (u))", sink).status, 0);
    EXPECT_EQ(sink.output, "((u (as @value0 U)))\n");
  }
  constexpr std::string_view script =
      "(get-value (@value0))(get-value (@value0))(get-value (@value0))";
  for (const bool use_reader : {false, true}) {
    SCOPED_TRACE(::testing::Message()
                 << "FE-MODEL-BOUND-02 " << script << " reader " << use_reader);
    auto result =
        use_reader
            ? Session::ForModelReader(
                  first,
                  [remaining =
                       script](std::span<char> bytes) mutable -> ReadResult {
                    const auto count = std::min(
                        {remaining.size(), bytes.size(), std::size_t{3}});
                    std::copy_n(remaining.data(), count, bytes.data());
                    remaining.remove_prefix(count);
                    return count;
                  })
            : Session::ForModelText(first, script);
    auto session = Require(std::move(result));
    auto header = Require(session.NextSyntax());
    ASSERT_TRUE(header);
    EXPECT_EQ(header->kind, CommandKind::kGetValue);
    ExpectError(session.Apply(second.get()), DiagnosticCode::kModelFailure);
    ExpectError(session.Apply(), DiagnosticCode::kInvalidState);
    EXPECT_EQ(second_calls, 1);
    const auto use = [&](const TypedCommand& command) {
      ASSERT_EQ(command.values.size(), 1);
      EXPECT_EQ(command.values[0].input, "(as @value0 U)");
      EXPECT_EQ(
          Require(first->Evaluate(command.values[0].term.Borrow())).GetKey(),
          "first");
    };
    use(RequireNext(session, CommandKind::kGetValue));
    header = Require(session.NextSyntax());
    ASSERT_TRUE(header);
    EXPECT_EQ(header->kind, CommandKind::kGetValue);
    use(Require(session.Apply(first.get())));
    EXPECT_FALSE(Require(session.NextSyntax()));
    EXPECT_EQ(second_calls, 1);
  }
  EXPECT_EQ(first_calls, 5);
  auto unrelated = Require(Session::FromText(std::string_view(
      "(set-option :produce-models true)(set-logic ALL)(declare-sort U 0)"
      "(check-sat)(get-value (@value0))")));
  RequireNext(unrelated, CommandKind::kSetOption);
  RequireNext(unrelated, CommandKind::kSetLogic);
  RequireNext(unrelated, CommandKind::kDeclareSort);
  const auto other = RequireCheck(unrelated);
  EXPECT_NE(other.get(), query.get());
  ASSERT_TRUE(Require(unrelated.NextSyntax()));
  ExpectError(unrelated.Apply(first.get()), DiagnosticCode::kModelFailure);
  EXPECT_FALSE(Require(unrelated.NextSyntax()));
  auto malformed =
      Require(Session::ForModelText(first, "(get-value (|@value01|))"));
  ExpectError(Next(malformed), DiagnosticCode::kInvalidInput);
  EXPECT_EQ(first_calls, 5);
}

TEST(FrontendModel, IdenticalKeyBytesInDistinctSortsDoNotMerge) {
  const auto query = RequireQuery(Parse(kArrayModel));
  auto owner = Require(ModelOwner::Create(
      query,
      std::make_unique<ControlledModel>([](ModelOwner& model,
                                           NodeView term) -> Outcome<ValueRef> {
        if (Applied(term).GetKind() == DeclKind::kInternalFunction)
          return model.LookupProxy(term);
        return model.MakeValue(term.GetSort(), std::string("key\0bytes", 9));
      }),
      CheckAnswer::kSat));
  CapturedSink sink;
  EXPECT_EQ(Inspect(owner, "(get-value (u a u b))", sink).status, 0);
  EXPECT_EQ(sink.output,
            "((u (as @value0 U)) (a (as @value1 (Array Bool Bool)))"
            " (u (as @value0 U)) (b (as @value1 (Array Bool Bool))))\n");
  auto session =
      Require(Session::ForModelText(owner, "(get-value (@value0 @value1))"));
  const auto typed = RequireNext(session, CommandKind::kGetValue);
  ASSERT_EQ(typed.values.size(), 2);
  const auto u = Require(owner->LookupProxy(typed.values[0].term.Borrow()));
  const auto a = Require(owner->LookupProxy(typed.values[1].term.Borrow()));
  EXPECT_EQ(u.GetKey(), a.GetKey());
  EXPECT_EQ(u.GetSort(), query->GetFunctions()[0].decl.Borrow().GetRange());
  EXPECT_EQ(a.GetSort(), query->GetFunctions()[1].decl.Borrow().GetRange());
  EXPECT_NE(u.GetSort(), a.GetSort());
}

TEST(FrontendModel, SharedSortAliasesBoundGeneratedModelText) {
  std::string aliases = "(define-sort A0 (T) T)";
  for (int depth = 1; depth <= 12; ++depth) {
    const auto name = "A" + std::to_string(depth);
    const auto child = "A" + std::to_string(depth - 1);
    aliases += "(define-sort " + name + " (T) (Array (" + child + " T) (" +
               child + " T)))";
  }
  aliases += "(define-sort R (X Y) (Array Y X))";
  constexpr std::string_view signature =
      "(declare-datatype Box (par (T) ((nil) (box (contents T)))))"
      "(declare-const deep (A12 Bool))(declare-const ctor (Box (A12 Bool)))"
      "(declare-const skew (R Int Bool))"
      "(declare-const small (Array Bool (_ BitVec 1)))"
      "(define-const constructor (Box (A12 Bool)) (as nil (Box (A12 Bool))))";
  std::shared_ptr<const QueryContext> query;
  {
    const auto input = std::string(kModelOption) + "(set-logic ALL)" + aliases +
                       std::string(signature) + "(check-sat)";
    SCOPED_TRACE(::testing::Message() << "FE-MODEL-SORT-TEXT-02 " << input);
    query = RequireQuery(Parse(input));
  }
  ASSERT_EQ(query->GetFunctions().size(), 4);
  auto owner = Require(ModelOwner::Create(
      query,
      std::make_unique<ControlledModel>(
          NamedAbstractValue, ControlledModel::Description{},
          [](ModelOwner& model) -> Outcome<ModelExport> {
            const auto functions = model.GetQuery().GetFunctions();
            auto nm =
                Require(NodeManager::Attach(model.GetQuery().GetManager()));
            const auto deep = *functions[0].decl.Borrow().GetRange();
            const auto inner = Require(model.MakeProxy(
                Require(model.MakeValue(*deep.GetArrayElement(), "inner"))));
            const auto array =
                Require(nm.MakeBuiltin({Kind::kArrayConst, {}, deep, {}},
                                       std::array{inner.GetTerm().Borrow()}));
            const auto skew = Require(model.MakeProxy(Require(model.MakeValue(
                *functions[2].decl.Borrow().GetRange(), "skew"))));
            const auto small = Require(model.MakeProxy(Require(model.MakeValue(
                *functions[3].decl.Borrow().GetRange(), "small"))));
            const auto ctor = model.GetQuery().GetDefinitions()[0].body;
            return ModelExport{{{functions[0].decl, {}, array},
                                {functions[1].decl, {}, ctor},
                                {functions[2].decl, {}, skew.GetTerm()},
                                {functions[3].decl, {}, small.GetTerm()}},
                               {inner, skew, small}};
          }),
      CheckAnswer::kSat));
  constexpr std::string_view input = "(get-value (deep skew small))(get-model)";
  SCOPED_TRACE(::testing::Message() << "FE-MODEL-SORT-TEXT-02 " << input);
  CapturedSink sink;
  ASSERT_EQ(Inspect(owner, input, sink).status, 0);
  EXPECT_EQ(sink.flushes, 2);
  ASSERT_LT(sink.output.size(), 500u);
  EXPECT_EQ(sink.output,
            "((deep (as @value0 (A12 Bool))) (skew (as @value1 (R Int Bool)))"
            " (small (as @value2 (Array Bool (_ BitVec 1)))))\n"
            "((define-fun deep () (A12 Bool)"
            " ((as const (A12 Bool)) (as @value3 (A11 Bool))))"
            " (define-fun ctor () (Box (A12 Bool)) (as nil (Box (A12 Bool))))"
            " (define-fun skew () (R Int Bool) (as @value1 (R Int Bool)))"
            " (define-fun small () (Array Bool (_ BitVec 1))"
            " (as @value2 (Array Bool (_ BitVec 1)))))\n");
  const auto inspect_input =
      "(get-value ((as @value0 (A12 Bool)) (as @value1 (R Int Bool))"
      " ((as const (A12 Bool)) (as @value3 (A11 Bool)))"
      " (as nil (Box (A12 Bool))) (as @value2 (Array Bool (_ BitVec 1)))))";
  auto session = Require(Session::ForModelText(owner, inspect_input));
  const auto typed = RequireNext(session, CommandKind::kGetValue);
  ASSERT_EQ(typed.values.size(), 5);
  constexpr std::array<std::size_t, 5> source{0, 2, 0, 1, 3};
  for (std::size_t i = 0; i < source.size(); ++i)
    EXPECT_EQ(typed.values[i].term.Borrow().GetSort(),
              query->GetFunctions()[source[i]].decl.Borrow().GetRange());
  const auto asymmetric = typed.values[1].term.Borrow().GetSort();
  EXPECT_EQ(asymmetric.GetArrayIndex()->GetKind(), SortKind::kBool);
  EXPECT_EQ(asymmetric.GetArrayElement()->GetKind(), SortKind::kInt);
  const auto constant = typed.values[2].term.Borrow();
  ASSERT_EQ(constant.GetKind(), Kind::kArrayConst);
  ASSERT_EQ(constant.GetChildren().size(), 1);
  EXPECT_EQ(constant.GetChildren()[0].GetSort(),
            constant.GetSort().GetArrayElement());
  EXPECT_EQ(typed.values[3].term, query->GetDefinitions()[0].body);
  EXPECT_FALSE(Require(session.NextSyntax()));
}

TEST(FrontendModel, DeepAliasChainBoundsRecipeMatchesAcrossSubsorts) {
  constexpr int kDepth = 64;
  std::string script =
      std::string(kModelOption) + "(set-logic ALL)(define-sort A0 (T) T)";
  for (int depth = 1; depth <= kDepth; ++depth) {
    const auto child = "A" + std::to_string(depth - 1);
    script += "(define-sort A" + std::to_string(depth) + " (T) (Array (" +
              child + " T) (" + child + " T)))";
  }
  script +=
      "(declare-const deep (Array (A63 Bool) (A63 Bool)))"
      "(declare-const inner (A63 Bool))(check-sat)";
  constexpr std::string_view request = "(get-value (deep inner))";
  SCOPED_TRACE(::testing::Message()
               << "FE-MODEL-SORT-TEXT-02 " << script << request);
  const auto query = RequireQuery(Parse(script));
  auto owner = Require(ModelOwner::Create(
      query, std::make_unique<ControlledModel>(NamedAbstractValue),
      CheckAnswer::kSat));
  auto session = Require(Session::ForModelText(owner, request));
  CapturedSink sink;
  ASSERT_EQ(dandelion::frontend::Run(session, {}, sink.Get()).status, 0);
  EXPECT_EQ(
      sink.output,
      "((deep (as @value0 (A64 Bool))) (inner (as @value1 (A63 Bool))))\n");
  const auto pairs = session.GetStats().sort_recipe_pairs;
  EXPECT_GT(pairs, 0u);
  EXPECT_LE(pairs, 4u * kDepth);
}

TEST(FrontendModel, SortAliasCanTakeAnAtomicSpellingOfACompoundArgument) {
  constexpr std::string_view script =
      "(set-option :produce-models true)(set-logic ALL)"
      "(define-sort A1 (T) (Array T T))"
      "(define-sort A2 (T) (A1 (A1 T)))"
      "(define-sort A3 (T) (A1 (A2 T)))"
      "(define-sort Z () (A2 Int))"
      "(declare-const deep (Array (A2 Int) (A2 Int)))(check-sat)";
  constexpr std::string_view request = "(get-value (deep))";
  SCOPED_TRACE(::testing::Message()
               << "FE-MODEL-SORT-TEXT-02 " << script << request);
  auto owner = Require(
      ModelOwner::Create(RequireQuery(Parse(script)),
                         std::make_unique<ControlledModel>(NamedAbstractValue),
                         CheckAnswer::kSat));
  CapturedSink sink;
  ASSERT_EQ(Inspect(owner, request, sink).status, 0);
  EXPECT_EQ(sink.output, "((deep (as @value0 (A1 Z))))\n");
}

TEST(FrontendModel, ScalarDescriptionFaultsRejectTheFirstPair) {
  constexpr std::string_view input =
      "(set-option :produce-models true)(set-logic ALL)(declare-const x Int)"
      "(check-sat)(get-value (x))(echo \"unread\")";
  enum class Fault {
    kNone,
    kMissing,
    kNull,
    kNonliteral,
    kWrongSort,
    kForeign
  };
  for (const auto fault :
       {Fault::kNone, Fault::kMissing, Fault::kNull, Fault::kNonliteral,
        Fault::kWrongSort, Fault::kForeign}) {
    SCOPED_TRACE(::testing::Message()
                 << "FE-MODEL-FAULTS-02 " << input << " scalar case "
                 << std::to_underlying(fault));
    auto foreign = Require(NodeManager::Create());
    int evaluations = 0;
    int descriptions = 0;
    CapturedSink sink;
    const auto result = WithModel(
        input,
        std::make_unique<ControlledModel>(
            [&](ModelOwner& owner, NodeView term) {
              ++evaluations;
              return owner.MakeValue(term.GetSort(), "seven");
            },
            [&](ModelOwner& owner,
                const ValueRef&) -> Outcome<ValueDescription> {
              ++descriptions;
              auto nm =
                  Require(NodeManager::Attach(owner.GetQuery().GetManager()));
              switch (fault) {
                case Fault::kNone:
                  return ValueDescription{Require(nm.MakeInt(IntegerView(7)))};
                case Fault::kMissing:
                  return ValueDescription{};
                case Fault::kNull:
                  return ValueDescription{Node{}};
                case Fault::kNonliteral:
                  return ValueDescription{ApplyDecl(
                      nm, owner.GetQuery().GetFunctions()[0].decl.Borrow())};
                case Fault::kWrongSort:
                  return ValueDescription{Require(nm.MakeBool(true))};
                case Fault::kForeign:
                  return ValueDescription{
                      Require(foreign.MakeInt(IntegerView(7)))};
              }
              throw std::logic_error("Unrecognized scalar test case");
            }),
        sink);
    EXPECT_EQ(evaluations, 1);
    EXPECT_EQ(descriptions, 1);
    if (fault == Fault::kNone) {
      EXPECT_EQ(result.status, 0);
      EXPECT_FALSE(result.diagnostic);
      EXPECT_EQ(sink.output, "sat\n((x 7))\n\"unread\"\n");
      EXPECT_EQ(sink.flushes, 3);
    } else {
      EXPECT_EQ(result.status, 1);
      ASSERT_TRUE(result.diagnostic);
      EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kModelFailure);
      EXPECT_EQ(sink.output, "sat\n" + DiagnosticResponse(*result.diagnostic));
      EXPECT_EQ(sink.flushes, 2);
    }
  }
}

TEST(FrontendModel, DatatypeDescriptionChecksConstructorFieldsAndActiveKeys) {
  constexpr std::string_view input =
      "(set-option :produce-models true)(set-logic ALL)"
      "(declare-datatype Tree ((leaf (data Int)) (fork (left Tree) (right "
      "Tree))))"
      "(declare-datatype Other ((alien)))"
      "(define-const sample Tree (fork (leaf 7) (leaf 7)))"
      "(define-const selection Int (data (leaf 7)))"
      "(define-const test Bool ((_ is leaf) (leaf 7)))"
      "(define-const alien_value Other alien)(check-sat)";
  constexpr std::string_view request = "(get-value (sample))(echo \"unread\")";
  const auto query = RequireQuery(Parse(input));
  auto nm = Require(NodeManager::Attach(query->GetManager()));
  const auto& sample = query->GetDefinitions()[0].body;
  const auto instance = [&](NodeView term) {
    return Require(nm.RetainDeclInstance(*term.GetDeclaration()));
  };
  const auto fork = instance(sample.Borrow());
  const auto leaf = instance(Argument(sample, 0));
  const auto int_sort = Require(nm.IntSort());
  auto other_owner = Require(ModelOwner::Create(
      query, std::make_unique<ControlledModel>(), CheckAnswer::kSat));
  const auto foreign =
      Require(other_owner->MakeValue(int_sort.Borrow(), "seven"));
  enum class Fault {
    kNone,
    kMissing,
    kNull,
    kSelector,
    kTester,
    kWrongConstructor,
    kFieldCount,
    kFieldSort,
    kForeignField,
    kCycle
  };
  for (const auto fault :
       {Fault::kNone, Fault::kMissing, Fault::kNull, Fault::kSelector,
        Fault::kTester, Fault::kWrongConstructor, Fault::kFieldCount,
        Fault::kFieldSort, Fault::kForeignField, Fault::kCycle}) {
    SCOPED_TRACE(::testing::Message()
                 << "FE-MODEL-FAULTS-02 " << input << request
                 << " datatype case " << std::to_underlying(fault));
    int descriptions = 0;
    auto owner = Require(ModelOwner::Create(
        query,
        std::make_unique<ControlledModel>(
            [](ModelOwner& model, NodeView term) {
              return model.MakeValue(term.GetSort(), "root");
            },
            [&](ModelOwner& model,
                const ValueRef& value) -> Outcome<ValueDescription> {
              ++descriptions;
              if (value.GetSort() == int_sort.Borrow())
                return ValueDescription{Require(nm.MakeInt(IntegerView(7)))};
              if (value.GetKey() == "leaf") {
                ValueRef field =
                    Require(model.MakeValue(int_sort.Borrow(), "seven"));
                if (fault == Fault::kForeignField) field = foreign;
                return ValueDescription{
                    DatatypeValue{leaf, {std::move(field)}}};
              }
              auto left = Require(model.MakeValue(value.GetSort(), "leaf"));
              auto right = left;
              auto ctor = fork;
              switch (fault) {
                case Fault::kMissing:
                  return ValueDescription{};
                case Fault::kNull:
                  ctor = {};
                  break;
                case Fault::kSelector:
                  ctor = instance(query->GetDefinitions()[1].body.Borrow());
                  break;
                case Fault::kTester:
                  ctor = instance(query->GetDefinitions()[2].body.Borrow());
                  break;
                case Fault::kWrongConstructor:
                  ctor = instance(query->GetDefinitions()[3].body.Borrow());
                  break;
                case Fault::kFieldCount:
                  return ValueDescription{DatatypeValue{ctor, {left}}};
                case Fault::kFieldSort:
                  right = Require(model.MakeValue(int_sort.Borrow(), "seven"));
                  break;
                case Fault::kCycle:
                  // A separate handle with the same active semantic key.
                  left = Require(model.MakeValue(value.GetSort(), "root"));
                  break;
                case Fault::kNone:
                case Fault::kForeignField:
                  break;
              }
              return ValueDescription{DatatypeValue{ctor, {left, right}}};
            }),
        CheckAnswer::kSat));
    CapturedSink sink;
    const auto result = Inspect(owner, request, sink);
    if (fault == Fault::kNone) {
      EXPECT_EQ(result.status, 0);
      EXPECT_EQ(sink.output,
                "((sample (fork (leaf 7) (leaf 7))))\n\"unread\"\n");
      EXPECT_EQ(sink.flushes, 2);
    } else {
      EXPECT_EQ(result.status, 1);
      ASSERT_TRUE(result.diagnostic);
      EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kModelFailure);
      EXPECT_EQ(sink.output, DiagnosticResponse(*result.diagnostic));
      EXPECT_EQ(sink.flushes, 1);
      if (fault == Fault::kForeignField) {
        EXPECT_EQ(descriptions, 2);
      }
    }
  }
}

TEST(FrontendModel, ExportChecksExactSignatureAndBodyBindings) {
  constexpr std::string_view input =
      "(set-option :produce-models true)(set-logic QF_UFIDL)"
      "(declare-const seed Int)(declare-fun f (Int Int) Int)"
      "(check-sat)(get-model)(echo \"unread\")";
  enum class Fault {
    kNone,
    kMissing,
    kDuplicate,
    kForeign,
    kExtra,
    kForward,
    kWrongResult,
    kFormalCount,
    kRepeatedFormal,
    kForeignFormal,
    kFormalSort,
    kFormalKind,
    kUnboundFormal,
    kInactiveOp,
    kOutsideSort
  };
  constexpr std::array faults{
      Fault::kNone,          Fault::kMissing,     Fault::kDuplicate,
      Fault::kForeign,       Fault::kExtra,       Fault::kForward,
      Fault::kWrongResult,   Fault::kFormalCount, Fault::kRepeatedFormal,
      Fault::kForeignFormal, Fault::kFormalSort,  Fault::kFormalKind,
      Fault::kUnboundFormal, Fault::kInactiveOp,  Fault::kOutsideSort};
  for (const auto fault : faults) {
    SCOPED_TRACE(::testing::Message()
                 << "FE-MODEL-FAULTS-02 " << input << " export case "
                 << std::to_underlying(fault));
    int exports = 0;
    int evaluations = 0;
    auto foreign = Require(NodeManager::Create());
    const auto foreign_int = Require(foreign.IntSort());
    CapturedSink sink;
    const auto result = WithModel(
        input,
        std::make_unique<ControlledModel>(
            [&](ModelOwner& owner, NodeView) {
              ++evaluations;
              return IntegerScalar(owner, 0);
            },
            ControlledModel::Description{},
            [&](ModelOwner& owner) -> Outcome<ModelExport> {
              ++exports;
              const auto functions = owner.GetQuery().GetFunctions();
              auto nm =
                  Require(NodeManager::Attach(owner.GetQuery().GetManager()));
              const auto int_sort = Require(nm.IntSort());
              const auto bool_sort = Require(nm.BoolSort());
              const auto seven = Require(nm.MakeInt(IntegerView(7)));
              const auto x = Require(nm.MakeFormal("x", int_sort.Borrow()));
              const auto y = Require(nm.MakeFormal("y", int_sort.Borrow()));
              const auto seed = ApplyDecl(nm, functions[0].decl.Borrow());
              const auto left = ApplyDecl(nm, x.Borrow());
              const auto right = ApplyDecl(nm, y.Borrow());
              const auto difference =
                  Builtin(nm, Kind::kIntSub, {left.Borrow(), right.Borrow()});
              ModelExport out{{{functions[0].decl, {}, seven},
                               {functions[1].decl,
                                {x, y},
                                Builtin(nm, Kind::kIntAdd,
                                        {seed.Borrow(), difference.Borrow()})}},
                              {}};
              auto& first = out.functions[0];
              auto& later = out.functions[1];
              switch (fault) {
                case Fault::kNone:
                  break;
                case Fault::kMissing:
                  out.functions.pop_back();
                  break;
                case Fault::kDuplicate:
                  later = first;
                  break;
                case Fault::kForeign:
                  later.declaration = Require(foreign.DeclareFunction(
                      "f",
                      {std::array{foreign_int.Borrow(), foreign_int.Borrow()}},
                      foreign_int.Borrow()));
                  break;
                case Fault::kExtra:
                  later.declaration = Require(nm.DeclareFunction(
                      "extra",
                      {std::array{int_sort.Borrow(), int_sort.Borrow()}},
                      int_sort.Borrow()));
                  break;
                case Fault::kForward:
                  first.body = ApplyDecl(nm, later.declaration.Borrow(),
                                         {seven.Borrow(), seven.Borrow()});
                  break;
                case Fault::kWrongResult:
                  later.body = Require(nm.MakeBool(false));
                  break;
                case Fault::kFormalCount:
                  later.formals.pop_back();
                  break;
                case Fault::kRepeatedFormal:
                  later.formals[1] = x;
                  break;
                case Fault::kForeignFormal:
                  later.formals[1] =
                      Require(foreign.MakeFormal("y", foreign_int.Borrow()));
                  break;
                case Fault::kFormalSort:
                  later.formals[1] =
                      Require(nm.MakeFormal("y", bool_sort.Borrow()));
                  break;
                case Fault::kFormalKind:
                  later.formals[1] = functions[0].decl;
                  break;
                case Fault::kUnboundFormal: {
                  const auto unbound =
                      Require(nm.MakeFormal("x", int_sort.Borrow()));
                  later.body = ApplyDecl(nm, unbound.Borrow());
                  break;
                }
                case Fault::kInactiveOp: {
                  later.body = Builtin(nm, Kind::kIntMod,
                                       {left.Borrow(), right.Borrow()});
                  break;
                }
                case Fault::kOutsideSort: {
                  const auto unknown =
                      Require(nm.DeclareUninterpretedSort("Hidden", 0));
                  const auto sort =
                      Require(nm.InstantiateSort(unknown.Borrow(), {}));
                  const auto value =
                      Require(nm.MakeFreshConstant("hidden", sort.Borrow()));
                  const auto condition = Builtin(
                      nm, Kind::kEqual, {value.Borrow(), value.Borrow()});
                  later.body = Builtin(
                      nm, Kind::kIte,
                      {condition.Borrow(), left.Borrow(), right.Borrow()});
                  break;
                }
              }
              return out;
            }),
        sink);
    EXPECT_EQ(exports, 1);
    EXPECT_EQ(evaluations, 0);
    if (fault == Fault::kNone) {
      ASSERT_EQ(result.status, 0);
      EXPECT_EQ(sink.flushes, 3);
      const auto model_end = sink.output.find('\n', 4);
      ASSERT_NE(model_end, std::string::npos);
      const auto recovered = RequireQuery(
          Parse("(set-logic QF_UFIDL)" + sink.output.substr(5, model_end - 6) +
                "(assert (= (f 9 2) 14))(check-sat)"));
      ASSERT_EQ(recovered->GetDefinitions().size(), 2);
      ASSERT_EQ(recovered->GetAssertions().size(), 1);
      const auto& f = recovered->GetDefinitions()[1];
      EXPECT_EQ(f.source_name, "f");
      ASSERT_EQ(f.formals.size(), 2);
      EXPECT_NE(f.formals[0], f.formals[1]);
      EXPECT_EQ(f.formals[0].Borrow().GetRange()->GetKind(), SortKind::kInt);
      EXPECT_EQ(f.formals[1].Borrow().GetRange()->GetKind(), SortKind::kInt);
      EXPECT_EQ(f.body.Borrow().GetSort().GetKind(), SortKind::kInt);
      EXPECT_EQ(EvalSmallInt(*recovered,
                             Argument(recovered->GetAssertions()[0].term, 0)),
                14);
    } else {
      EXPECT_EQ(result.status, 1);
      ASSERT_TRUE(result.diagnostic);
      EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kModelFailure);
      const bool before_first =
          fault == Fault::kMissing || fault == Fault::kForward;
      EXPECT_EQ(sink.flushes, before_first ? 2u : 1u);
      EXPECT_EQ(sink.output,
                before_first ? "sat\n" + DiagnosticResponse(*result.diagnostic)
                             : "sat\n((define-fun seed () Int 7)");
    }
  }
}

TEST(FrontendModel, ExportValidatesProxyMappingsBeforeNamingValues) {
  constexpr std::string_view input =
      "(set-option :produce-models true)(set-logic ALL)"
      "(declare-sort U 0)(declare-const a U)(declare-const b U)(check-sat)";
  constexpr std::string_view request = "(get-model)(echo \"unread\")";
  const auto query = RequireQuery(Parse(input));
  const auto functions = query->GetFunctions();
  ASSERT_EQ(functions.size(), 2);
  const auto sort = *functions[0].decl.Borrow().GetRange();
  auto external = Require(ModelOwner::Create(
      query, std::make_unique<ControlledModel>(), CheckAnswer::kSat));
  const auto foreign_value = Require(external->MakeValue(sort, "same"));
  const auto foreign_proxy = Require(external->MakeProxy(foreign_value));
  enum class Fault { kNone, kMissing, kDuplicate, kForeign };
  for (auto fault :
       {Fault::kNone, Fault::kMissing, Fault::kDuplicate, Fault::kForeign}) {
    SCOPED_TRACE(::testing::Message()
                 << "FE-MODEL-FAULTS-02 " << input << request << " proxy case "
                 << std::to_underlying(fault));
    int exports = 0;
    auto owner = Require(ModelOwner::Create(
        query,
        std::make_unique<ControlledModel>(
            [](ModelOwner& model, NodeView term) {
              if (Applied(term).GetKind() == DeclKind::kInternalFunction)
                return model.LookupProxy(term);
              return model.MakeValue(term.GetSort(), "same");
            },
            ControlledModel::Description{},
            [&](ModelOwner& model) -> Outcome<ModelExport> {
              ++exports;
              const auto value = Require(model.MakeValue(sort, "same"));
              const auto a = Require(model.MakeProxy(value));
              const auto b = Require(model.MakeProxy(value));
              EXPECT_NE(Applied(a.GetTerm().Borrow()),
                        Applied(b.GetTerm().Borrow()));
              ModelExport out{{{functions[0].decl, {}, a.GetTerm()},
                               {functions[1].decl, {}, b.GetTerm()}},
                              {a, b}};
              switch (fault) {
                case Fault::kNone:
                  break;
                case Fault::kMissing:
                  out.proxies.erase(out.proxies.begin());
                  break;
                case Fault::kDuplicate:
                  out.proxies.push_back(a);
                  break;
                case Fault::kForeign:
                  out.functions[0].body = foreign_proxy.GetTerm();
                  out.proxies[0] = foreign_proxy;
                  break;
              }
              return out;
            }),
        CheckAnswer::kSat));
    ExpectError(owner->MakeProxy(foreign_value), DiagnosticCode::kModelFailure);
    // MakeProxy is the public constructor. It derives the proxy sort from the
    // value, so a mismatched proxy cannot be constructed through this API.
    ExpectError(owner->MakeProxy(Require(IntegerScalar(*owner, 1))),
                DiagnosticCode::kModelFailure);
    CapturedSink sink;
    const auto result = Inspect(owner, request, sink);
    EXPECT_EQ(exports, 1);
    if (fault == Fault::kNone) {
      EXPECT_EQ(result.status, 0);
      EXPECT_EQ(sink.output,
                "((define-fun a () U (as @value0 U))"
                " (define-fun b () U (as @value0 U)))\n\"unread\"\n");
      EXPECT_EQ(sink.flushes, 2);
    } else {
      EXPECT_EQ(result.status, 1);
      ASSERT_TRUE(result.diagnostic);
      EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kModelFailure);
      EXPECT_EQ(sink.output, DiagnosticResponse(*result.diagnostic));
      EXPECT_EQ(sink.flushes, 1);
      auto session =
          Require(Session::ForModelText(owner, "(get-value (@value0))"));
      ExpectError(Next(session), DiagnosticCode::kInvalidInput);
    }
  }
}

TEST(FrontendModel, CallbackErrorsAndExceptionsPrecedeAnyModelResponseBytes) {
  enum class Boundary { kEvaluate, kDescribe, kExport };
  for (const auto boundary :
       {Boundary::kEvaluate, Boundary::kDescribe, Boundary::kExport}) {
    for (const bool throws : {false, true}) {
      const auto input =
          std::string(kModelOption) + "(set-logic ALL)(check-sat)" +
          (boundary == Boundary::kExport ? "(get-model)" : "(get-value (7))") +
          "(echo \"unread\")";
      SCOPED_TRACE(::testing::Message()
                   << "FE-MODEL-FAULTS-02 " << input << " callback "
                   << std::to_underlying(boundary) << " throws " << throws);
      const auto fail = [&]() -> std::unexpected<Diagnostic> {
        if (throws) throw std::runtime_error("Provider fault");
        return std::unexpected(ModelError());
      };
      int evaluations = 0;
      int descriptions = 0;
      int exports = 0;
      CapturedSink sink;
      const auto result = WithModel(
          input,
          std::make_unique<ControlledModel>(
              [&](ModelOwner& model, NodeView) -> Outcome<ValueRef> {
                ++evaluations;
                if (boundary == Boundary::kEvaluate) return fail();
                return IntegerScalar(model, 7);
              },
              [&](ModelOwner&, const ValueRef&) -> Outcome<ValueDescription> {
                ++descriptions;
                return fail();
              },
              [&](ModelOwner&) -> Outcome<ModelExport> {
                ++exports;
                return fail();
              }),
          sink);
      EXPECT_EQ(result.status, 1);
      ASSERT_TRUE(result.diagnostic);
      EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kModelFailure);
      EXPECT_EQ(sink.output, "sat\n" + DiagnosticResponse(*result.diagnostic));
      EXPECT_EQ(sink.flushes, 2);
      EXPECT_EQ(evaluations, boundary == Boundary::kExport ? 0 : 1);
      EXPECT_EQ(descriptions, boundary == Boundary::kDescribe ? 1 : 0);
      EXPECT_EQ(exports, boundary == Boundary::kExport ? 1 : 0);
    }
  }
}

TEST(FrontendModel, EvaluateRejectsOutOfSignatureTermsBeforeProviderCallback) {
  constexpr std::string_view input =
      "(set-option :produce-models true)(set-logic QF_UFIDL)"
      "(declare-sort U 0)(declare-const seed Int)(check-sat)";
  const auto query = RequireQuery(Parse(input));
  auto nm = Require(NodeManager::Attach(query->GetManager()));
  const auto int_sort = Require(nm.IntSort());
  const auto one = Require(nm.MakeInt(IntegerView(1)));
  const auto inactive =
      Builtin(nm, Kind::kIntMod, {one.Borrow(), one.Borrow()});
  const auto new_sort = Require(nm.DeclareUninterpretedSort("U", 0));
  const auto impostor = Require(nm.InstantiateSort(new_sort.Borrow(), {}));
  const auto local = Require(nm.MakeFreshConstant("local", impostor.Borrow()));
  const auto wrong_sort =
      Builtin(nm, Kind::kEqual, {local.Borrow(), local.Borrow()});
  const auto outside =
      Require(nm.DeclareFunction("seed", {}, int_sort.Borrow()));
  const auto unbound = ApplyDecl(nm, outside.Borrow());
  auto other = Require(NodeManager::Create());
  const auto foreign = Require(other.MakeInt(IntegerView(1)));
  int calls = 0;
  auto owner = Require(ModelOwner::Create(
      query,
      std::make_unique<ControlledModel>([&](ModelOwner& model, NodeView) {
        ++calls;
        return IntegerScalar(model, 7);
      }),
      CheckAnswer::kSat));
  for (const auto& rejected :
       {inactive, wrong_sort, unbound, foreign, Node{}}) {
    ExpectError(owner->Evaluate(rejected.Borrow()),
                DiagnosticCode::kModelFailure);
    EXPECT_EQ(calls, 0);
  }
  EXPECT_EQ(Require(owner->Evaluate(one.Borrow())).GetSort(),
            int_sort.Borrow());
  EXPECT_EQ(calls, 1);
}

TEST(FrontendModel, WideScalarFieldsAndNegativeWordNeighborRenderExactly) {
  constexpr std::string_view input =
      "(set-option :produce-models true)(set-logic ALL)"
      "(declare-const i Int)(declare-const b (_ BitVec 65))"
      "(declare-const f (_ FloatingPoint 65 66))(check-sat)(get-value (i b f))";
  const auto field = "1" + std::string(63, '0') + "1";
  CapturedSink sink;
  const auto result = WithModel(
      input,
      std::make_unique<ControlledModel>([&](ModelOwner& owner, NodeView term)
                                            -> Outcome<ValueRef> {
        auto nm = Require(NodeManager::Attach(owner.GetQuery().GetManager()));
        const auto sort = term.GetSort();
        switch (sort.GetKind()) {
          case SortKind::kInt:
            return IntegerScalar(owner,
                                 std::numeric_limits<std::int64_t>::min() + 1);
          case SortKind::kBitVector: {
            const auto bits =
                Require(Integer::ParseDecimal("18446744073709551617"));
            return Scalar(owner, Require(nm.MakeBvValue(sort, bits.Borrow())));
          }
          case SortKind::kFloatingPoint: {
            const auto bits =
                Require(Integer::ParseRadix("1" + field + field, 2));
            return Scalar(owner, Require(nm.MakeFpBits(sort, bits.Borrow())));
          }
          default:
            return std::unexpected(ModelError());
        }
      }),
      sink);
  EXPECT_EQ(result.status, 0);
  EXPECT_EQ(sink.flushes, 2);
  EXPECT_EQ(sink.output, "sat\n((i (- 9223372036854775807)) (b #b" + field +
                             ") (f (fp #b1 #b" + field + " #b" + field +
                             ")))\n");
}

TEST(FrontendModel, ExportKeepsDatatypeOperatorsAndUnshadowedRationalRank) {
  constexpr std::string_view input =
      "(set-option :produce-models true)(set-logic ALL)"
      "(declare-datatype D ((empty) (box (field Int))))"
      "(define-fun choose ((v D)) Int (ite ((_ is box) v) (field v) 0))"
      "(define-fun round ((r RoundingMode)) Float16 ((_ to_fp 5 11) r (/ 1 3)))"
      "(declare-fun choose_out (D) Int)"
      "(declare-fun round_out (RoundingMode) Float16)(check-sat)(get-model)";
  CapturedSink sink;
  const auto result = WithModel(
      input,
      std::make_unique<ControlledModel>(
          ControlledModel::Evaluation{}, ControlledModel::Description{},
          [](ModelOwner& owner) -> Outcome<ModelExport> {
            const auto defs = owner.GetQuery().GetDefinitions();
            const auto functions = owner.GetQuery().GetFunctions();
            return ModelExport{
                {{functions[0].decl, defs[0].formals, defs[0].body},
                 {functions[1].decl, defs[1].formals, defs[1].body}},
                {}};
          }),
      sink);
  ASSERT_EQ(result.status, 0) << sink.output;
  EXPECT_EQ(sink.flushes, 2);
  EXPECT_TRUE(sink.output.contains("(/ 1 3)"));
  const auto query = RequireQuery(
      Parse("(set-logic ALL)(declare-datatype D ((empty) (box (field Int))))" +
            sink.output.substr(5, sink.output.size() - 7) + "(check-sat)"));
  ASSERT_EQ(query->GetDefinitions().size(), 2);
  const auto& choose = query->GetDefinitions()[0];
  ASSERT_EQ(choose.formals.size(), 1);
  ASSERT_EQ(choose.body.Borrow().GetKind(), Kind::kIte);
  ASSERT_EQ(choose.body.Borrow().GetChildren().size(), 3);
  const auto tester = Argument(choose.body, 0);
  const auto selector = Argument(choose.body, 1);
  EXPECT_EQ(Applied(tester).GetKind(), DeclKind::kDatatypeTester);
  EXPECT_EQ(Applied(selector).GetKind(), DeclKind::kDatatypeSelector);
  EXPECT_EQ(Applied(tester).GetConstructor(),
            Applied(selector).GetConstructor());
  EXPECT_EQ(Applied(tester.GetChildren()[0]), choose.formals[0].Borrow());
  EXPECT_EQ(tester.GetChildren()[0], selector.GetChildren()[0]);
  const auto& round = query->GetDefinitions()[1];
  ASSERT_EQ(round.formals.size(), 1);
  ASSERT_EQ(round.body.Borrow().GetKind(), Kind::kFpFromRational);
  ASSERT_EQ(round.body.Borrow().GetChildren().size(), 1);
  EXPECT_EQ(Applied(Argument(round.body, 0)), round.formals[0].Borrow());
  EXPECT_EQ(round.body.Borrow().GetRationalParameter()->Numerator().ToDecimal(),
            "1");
  EXPECT_EQ(
      round.body.Borrow().GetRationalParameter()->Denominator().ToDecimal(),
      "3");
}

}  // namespace
}  // namespace dandelion::frontend::testing
