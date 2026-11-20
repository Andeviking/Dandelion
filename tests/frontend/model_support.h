#ifndef DANDELION_TESTS_FRONTEND_MODEL_SUPPORT_H
#define DANDELION_TESTS_FRONTEND_MODEL_SUPPORT_H

#include "test_support.h"

namespace dandelion::frontend::testing {

inline Diagnostic ModelError() {
  return {DiagnosticCode::kModelFailure, {}, "Controlled provider failure"};
}

inline std::string DiagnosticResponse(const Diagnostic& diagnostic) {
  std::string output =
      "(error \"line " + std::to_string(diagnostic.where.begin.line) +
      ", column " + std::to_string(diagnostic.where.begin.column) + ": ";
  for (const auto ch : diagnostic.message) {
    output.push_back(ch);
    if (ch == '"') output.push_back(ch);
  }
  return output + "\")\n";
}

class ControlledModel : public ModelProvider {
 public:
  using Evaluation = std::function<Outcome<ValueRef>(ModelOwner&, NodeView)>;
  using Description =
      std::function<Outcome<ValueDescription>(ModelOwner&, const ValueRef&)>;
  using Export = std::function<Outcome<ModelExport>(ModelOwner&)>;

  explicit ControlledModel(Evaluation eval = {}, Description describe = {},
                           Export export_model = {})
      : eval_(std::move(eval)),
        describe_(std::move(describe)),
        export_(std::move(export_model)) {}

  Outcome<ValueRef> Evaluate(ModelOwner& model, NodeView term) override {
    if (eval_) return eval_(model, term);
    return std::unexpected(ModelError());
  }
  Outcome<ValueDescription> Describe(ModelOwner& model,
                                     const ValueRef& value) override {
    if (describe_) return describe_(model, value);
    if (!value.GetPayload()) return ValueDescription{};
    return *std::static_pointer_cast<const ValueDescription>(
        value.GetPayload());
  }
  Outcome<ModelExport> ExportModel(ModelOwner& model) override {
    if (export_) return export_(model);
    return std::unexpected(ModelError());
  }

 private:
  Evaluation eval_;
  Description describe_;
  Export export_;
};

inline Outcome<ValueRef> Scalar(ModelOwner& model, Node node) {
  const auto sort = node.Borrow().GetSort();
  const auto key = std::to_string(node.GetId());
  return model.MakeValue(sort, key,
                         std::make_shared<ValueDescription>(std::move(node)));
}
inline Outcome<ValueRef> IntegerScalar(ModelOwner& model, std::int64_t number) {
  auto manager = Require(NodeManager::Attach(model.GetQuery().GetManager()));
  return Scalar(model, Require(manager.MakeInt(numeric::IntegerView(number))));
}
inline Node ApplyDecl(NodeManager& nm, DeclView decl,
                      std::initializer_list<NodeView> children = {}) {
  const auto instance = Require(nm.InstantiateDeclaration(decl, {}));
  return Require(
      nm.MakeApply(instance.Borrow(), {children.begin(), children.size()}));
}
inline Node Builtin(NodeManager& nm, Kind kind,
                    std::initializer_list<NodeView> children) {
  return Require(
      nm.MakeBuiltin({kind, {}, {}, {}}, {children.begin(), children.size()}));
}

class CapturedSink {
 public:
  ResponseSink Get() {
    return {
        [this](std::string_view bytes) -> WriteResult {
          const auto count = std::min(
              bytes.size(),
              stop_after_ > output.size() ? stop_after_ - output.size() : 0);
          output.append(bytes.substr(0, count));
          return count;
        },
        [this]() -> std::expected<void, std::error_code> {
          ++flushes;
          if (broken_flush_ == flushes)
            return std::unexpected(std::make_error_code(std::errc::io_error));
          return {};
        },
    };
  }
  void StopAfter(std::size_t bytes) noexcept { stop_after_ = bytes; }
  void BreakFlush(std::size_t count) noexcept { broken_flush_ = count; }

  // NOLINTBEGIN(readability-identifier-naming): Fixture observations.
  std::string output;
  std::size_t flushes = 0;
  // NOLINTEND(readability-identifier-naming)
 private:
  std::size_t stop_after_ = std::numeric_limits<std::size_t>::max();
  std::size_t broken_flush_ = 0;
};

inline RunResult WithModel(std::string_view script,
                           std::unique_ptr<ModelProvider> provider,
                           CapturedSink& sink,
                           CheckAnswer answer = CheckAnswer::kSat) {
  auto session = Require(Session::FromText(script));
  return Run(
      session,
      [&](const QueryContext&, SessionOptions options) -> Outcome<CheckResult> {
        EXPECT_TRUE(options.produce_models);
        return CheckResult{answer, UnknownReason::kUnknown,
                           std::move(provider)};
      },
      sink.Get());
}

}  // namespace dandelion::frontend::testing
#endif
