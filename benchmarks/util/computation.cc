#include "harness.h"

namespace dandelion::util_bench {
namespace {

template <class Generator, class Operation>
void RegisterIndependent(std::string name, Generator generator,
                         Operation operation) {
  Register("util/compute/" + name, [=](benchmark::State& state) {
    RunIndependent(state, Generate(generator), operation);
  });
}

template <auto Operation, class Generator>
void RegisterPair(std::string name, Generator generator) {
  RegisterIndependent(name, generator, [](const auto& values) {
    return Operation(values[0], values[1]);
  });
}

template <class T, class Operation>
T RunChain(const T& seed, const std::array<T, kBatchSize>& operands,
           Operation operation) {
  T current;
  auto previous = seed.Borrow();
  for (const auto& rhs : operands) {
    benchmark::DoNotOptimize(&rhs);
    current = operation(previous, rhs.Borrow()).value();
    previous = current.Borrow();
  }
  return current;
}

constexpr auto kIntegerAdd = [](IntegerView a, IntegerView b) { return a + b; };
constexpr auto kIntegerSub = [](IntegerView a, IntegerView b) { return a - b; };
constexpr auto kIntegerMul = [](IntegerView a, IntegerView b) { return a * b; };

template <auto Operation>
void RegisterIntegerOperation(std::string operation,
                              bool half_width_rhs = false,
                              bool word_product = false) {
  const auto prefix = "integer/" + operation + "/";
  const auto evaluate = [](const std::array<Integer, 2>& inputs) {
    return Operation(inputs[0].Borrow(), inputs[1].Borrow());
  };
  RegisterIndependent(
      prefix + "word",
      [word_product](auto& random, std::size_t) {
        return word_product ? SmallProductIntegers(random)
                            : WordIntegers(random);
      },
      evaluate);
  for (const auto bits : {128u, 256u, 512u, 1024u, 2048u, 4096u}) {
    RegisterIndependent(
        prefix + std::to_string(bits),
        [=](auto& random, std::size_t index) {
          const auto rhs_bits = half_width_rhs ? bits / 2 + 1 : bits;
          return std::array{ExactInteger(random, bits, index % 2 != 0),
                            ExactInteger(random, rhs_bits)};
        },
        evaluate);
  }
  RegisterIndependent(prefix + "mix90_10", MixedIntegers, evaluate);
}

template <auto Operation, class Generator>
void RegisterIntegerOverflow(std::string operation, Generator generator) {
  RegisterIndependent("integer/" + operation + "/overflow", generator,
                      [](const std::array<Integer, 2>& inputs) {
                        return Operation(inputs[0].Borrow(),
                                         inputs[1].Borrow());
                      });
}

template <auto Operation, class Generator>
void RegisterIntegerChain(std::string operation, std::uint32_t seed_bits,
                          Generator generator) {
  const auto profile = seed_bits < 64 ? "word" : std::to_string(seed_bits);
  Register("util/compute/integer/" + operation + "_chain/" + profile,
           [=](benchmark::State& state) {
             std::mt19937_64 random(kSeed);
             const auto seed = ExactInteger(random, seed_bits);
             const auto operands = Generate(generator);
             benchmark::DoNotOptimize(&seed);
             RunBatch(state, [&] {
               auto last = RunChain(seed, operands, Operation);
               benchmark::DoNotOptimize(last);
             });
           });
}

void RegisterIntegers() {
  RegisterIntegerOperation<kIntegerAdd>("add");
  RegisterIntegerOperation<kIntegerSub>("sub");
  RegisterIntegerOperation<kIntegerMul>("mul", false, true);
  RegisterIntegerOperation<Integer::DivMod>("divmod", true);
  RegisterIntegerOperation<Integer::Gcd>("gcd", true);

  RegisterIntegerOverflow<kIntegerAdd>("add", [](auto& random, std::size_t) {
    return AddOverflowIntegers(random);
  });
  RegisterIntegerOverflow<kIntegerSub>("sub", [](auto& random, std::size_t) {
    return std::array{
        Integer(std::numeric_limits<std::int64_t>::min() +
                static_cast<std::int64_t>(random() % 1024)),
        ExactInteger(random, 24),
    };
  });
  RegisterIntegerOverflow<kIntegerMul>("mul", [](auto& random, std::size_t) {
    return std::array{ExactInteger(random, 63), ExactInteger(random, 2)};
  });

  for (const auto bits : {8u, 128u, 256u, 1024u, 4096u}) {
    RegisterIndependent(
        "integer/power/" +
            std::string(bits == 8 ? "word" : std::to_string(bits)),
        [bits](auto& random, std::size_t index) {
          return std::array{
              ExactInteger(random, bits, index % 2 != 0),
              Integer(static_cast<std::int64_t>(3 + random() % 4))};
        },
        [](const auto& inputs) {
          return Integer::Power(inputs[0].Borrow(), inputs[1].Borrow());
        });
  }

  for (const auto bits : {256u, 4096u}) {
    RegisterIntegerChain<kIntegerAdd>(
        "add", bits, [](auto& random, std::size_t i) {
          return ExactInteger(random, 20, i % 2 == 0);
        });
    RegisterIntegerChain<kIntegerMul>(
        "mul", bits, [](auto& random, std::size_t) {
          return Integer(static_cast<std::int64_t>(2 + random() % 2));
        });
  }
  RegisterIntegerChain<kIntegerAdd>("add", 60, [](auto& random, std::size_t i) {
    return ExactInteger(random, 20, i % 2 == 0);
  });
  RegisterIntegerChain<kIntegerMul>("mul", 8, [](auto& random, std::size_t) {
    return Integer(random() % 2 == 0 ? -2 : 2);
  });
}

template <class Operation>
void RegisterRationalOperation(std::string operation, Operation evaluate) {
  const auto prefix = "rational/" + operation + "/";
  const auto calculate = [evaluate](const std::array<Rational, 2>& inputs) {
    return evaluate(inputs[0].Borrow(), inputs[1].Borrow());
  };
  RegisterIndependent(
      prefix + "word",
      [](auto& random, std::size_t) {
        return std::array{WordRational(random), WordRational(random)};
      },
      calculate);
  RegisterIndependent(
      prefix + "mixed",
      [](auto& random, std::size_t) {
        return std::array{WideRational(random, 256), WordRational(random)};
      },
      calculate);
  RegisterIndependent(
      prefix + "wide",
      [](auto& random, std::size_t) {
        return std::array{WideRational(random, 256), WideRational(random, 192)};
      },
      calculate);
}

template <class Generator>
void RegisterSimplexRows(std::string profile, Generator generate) {
  for (const std::size_t columns : {8u, 32u}) {
    Register(
        "util/compute/rational/simplex_row/" + profile + "/" +
            std::to_string(columns),
        [=](benchmark::State& state) {
          std::mt19937_64 pivot_random(kSeed + 1);
          std::vector<Rational> pivot(columns);
          for (auto& value : pivot) value = generate(pivot_random);
          const auto rows = Generate([&](auto& random, std::size_t) {
            std::vector<Rational> row(columns);
            for (auto& value : row) value = generate(random);
            return std::pair{generate(random), std::move(row)};
          });
          std::vector<Rational> updated(columns);
          RunBatch(
              state,
              [&] {
                for (const auto& [coefficient, row] : rows) {
                  benchmark::DoNotOptimize(&row);
                  benchmark::DoNotOptimize(&coefficient);
                  for (std::size_t column = 0; column < columns; ++column) {
                    const auto product =
                        (coefficient.Borrow() * pivot[column].Borrow()).value();
                    updated[column] =
                        (row[column].Borrow() - product.Borrow()).value();
                  }
                  benchmark::DoNotOptimize(updated.data());
                }
              },
              "ns/row");
        });
  }
}

void RegisterRationals() {
  constexpr auto multiply = [](auto a, auto b) { return a * b; };
  RegisterRationalOperation("add", [](auto a, auto b) { return a + b; });
  RegisterRationalOperation("sub", [](auto a, auto b) { return a - b; });
  RegisterRationalOperation("mul", multiply);
  RegisterRationalOperation("div", [](auto a, auto b) { return a / b; });
  for (const auto bits : {31u, 192u}) {
    Register("util/compute/rational/cross_cancel_chain/" +
                 std::string(bits == 31 ? "word" : "wide"),
             [=](benchmark::State& state) {
               const auto chain = MakeRationalChain(bits);
               const auto& seed = chain.values.front();
               const auto last = RunChain(seed, chain.factors, multiply);
               Require(last.Borrow() == chain.values.back().Borrow(),
                       "Rational cancellation chain did not telescope");
               benchmark::DoNotOptimize(&seed);
               RunBatch(state, [&] {
                 auto result = RunChain(seed, chain.factors, multiply);
                 benchmark::DoNotOptimize(result);
               });
             });
  }
  RegisterSimplexRows("word", WordRational);
  RegisterSimplexRows("wide",
                      [](auto& random) { return WideRational(random, 192); });
}

std::string BvProfile(std::uint32_t width) {
  return std::to_string(width) + (width == 64 ? "_high" : "");
}

template <auto Operation>
void RegisterBvPair(std::string operation, std::uint32_t width,
                    std::uint32_t rhs_bits) {
  RegisterPair<Operation>("bv/" + operation + "/" + BvProfile(width),
                          [=](auto& random, std::size_t) {
                            return std::array{DenseBv(random, width, width),
                                              DenseBv(random, width, rhs_bits)};
                          });
}

template <auto Operation>
void RegisterShift(std::string operation, std::uint32_t width) {
  RegisterPair<Operation>(
      "bv/" + operation + "/" + BvProfile(width),
      [width](auto& random, std::size_t) {
        return std::array{
            DenseBv(random, width, width),
            BitVector::FromUint64(IntegerView(width),
                                  1 + random() % (width - 1))
                .value(),
        };
      });
}

template <auto Operation>
void RegisterRotate(std::string operation, std::uint32_t width) {
  RegisterIndependent(
      "bv/" + operation + "/" + BvProfile(width),
      [width](auto& random, std::size_t) {
        const auto count =
            static_cast<std::int64_t>(width + 1 + random() % (width - 1));
        return std::pair{DenseBv(random, width, width), IntegerView(count)};
      },
      [](const auto& input) { return Operation(input.first, input.second); });
}

void RegisterBitVectors() {
  for (const auto width : kBvWidths) {
    const auto division_bits = width / 2 + 1;
    RegisterBvPair<BitVector::Add>("add", width, width);
    RegisterBvPair<BitVector::Mul>("mul", width, width);
    RegisterBvPair<BitVector::Udiv>("udiv", width, division_bits);
    RegisterBvPair<BitVector::Sdiv>("sdiv", width, division_bits);
    RegisterBvPair<BitVector::Smod>("smod", width, division_bits);
    RegisterShift<BitVector::Shl>("shl", width);
    RegisterShift<BitVector::Ashr>("ashr", width);
    RegisterBvPair<BitVector::Concat>("concat", width, width);
    RegisterRotate<BitVector::RotateLeft>("rotate_left", width);
    RegisterRotate<BitVector::RotateRight>("rotate_right", width);
    RegisterBvPair<BitVector::Uaddo>("uadd_overflow", width, width);
    RegisterBvPair<BitVector::Saddo>("sadd_overflow", width, width);
    RegisterBvPair<BitVector::Umulo>("umul_overflow", width, width);
    RegisterBvPair<BitVector::Smulo>("smul_overflow", width, width);
    RegisterBvPair<BitVector::Usubo>("usub_overflow", width, width);
    RegisterBvPair<BitVector::Ssubo>("ssub_overflow", width, width);
    RegisterBvPair<BitVector::Sdivo>("sdiv_overflow", width, width);
    RegisterIndependent(
        "bv/neg_overflow/" + BvProfile(width),
        [width](auto& random, std::size_t) {
          return DenseBv(random, width, width);
        },
        [](const BitVector& input) {
          return NumericResult<bool>(BitVector::Nego(input));
        });
    RegisterIndependent(
        "bv/extract/" + BvProfile(width),
        [width](auto& random, std::size_t) {
          const auto low =
              static_cast<std::int64_t>(1 + random() % (width / 4));
          const auto high =
              static_cast<std::int64_t>(3 * width / 4 + random() % (width / 4));
          return std::tuple{DenseBv(random, width, width), IntegerView(high),
                            IntegerView(low)};
        },
        [](const auto& input) {
          return BitVector::Extract(std::get<0>(input), std::get<1>(input),
                                    std::get<2>(input));
        });
  }
  for (const auto width : {8u, 63u, 65u}) {
    RegisterBvPair<BitVector::Add>("add", width, width);
    RegisterBvPair<BitVector::Udiv>("udiv", width, width / 2 + 1);
    RegisterBvPair<BitVector::Sdiv>("sdiv", width, width / 2 + 1);
    RegisterShift<BitVector::Ashr>("ashr", width);
    RegisterShift<BitVector::Lshr>("lshr", width);
  }
}

template <auto Operation>
constexpr auto kRoundedPair =
    [](const auto& a, const auto& b) { return Operation(kRounding, a, b); };

using Fp = FloatingPointValue;

void RegisterFloatingPointFormat(std::string profile, FpFormat format) {
  const std::string prefix = "fp/";
  const auto suffix = "/" + profile;
  const auto bias = (std::uint32_t{1} << (format.ExponentWidth() - 1)) - 1;
  const auto positive = [=](auto& random, std::size_t) {
    const auto exponent = bias - 2 + static_cast<std::uint32_t>(random() % 5);
    return NormalFp(random, format, exponent, false);
  };
  const auto pair = [=](auto& random, std::size_t index) {
    auto lhs = positive(random, index);
    const auto exponent = bias - 2 + static_cast<std::uint32_t>(random() % 5);
    return std::array{std::move(lhs),
                      NormalFp(random, format, exponent, index % 2 != 0)};
  };
  RegisterPair<kRoundedPair<Fp::Add>>(prefix + "add" + suffix, pair);
  RegisterPair<kRoundedPair<Fp::Sub>>(prefix + "sub" + suffix, pair);
  RegisterPair<kRoundedPair<Fp::Mul>>(prefix + "mul" + suffix, pair);
  RegisterPair<kRoundedPair<Fp::Div>>(prefix + "div" + suffix, pair);
  RegisterPair<Fp::Rem>(prefix + "rem" + suffix, pair);
  RegisterPair<Fp::Min>(prefix + "min" + suffix, pair);
  RegisterPair<Fp::Max>(prefix + "max" + suffix, pair);
  RegisterPair<Fp::Eq>(prefix + "eq" + suffix, pair);
  RegisterPair<Fp::Lt>(prefix + "lt" + suffix, pair);
  RegisterIndependent(
      prefix + "fma" + suffix,
      [=](auto& random, std::size_t index) {
        auto values = pair(random, index);
        auto addend = NormalFp(random, format, bias - 1, index % 3 != 0);
        return std::array{std::move(values[0]), std::move(values[1]),
                          std::move(addend)};
      },
      [](const auto& inputs) {
        return Fp::Fma(kRounding, inputs[0], inputs[1], inputs[2]);
      });
  RegisterIndependent(prefix + "sqrt" + suffix, positive, [](const Fp& input) {
    return Fp::Sqrt(kRounding, input);
  });
  RegisterIndependent(
      prefix + "round_to_integral" + suffix, positive,
      [](const Fp& input) { return Fp::RoundToIntegral(kRounding, input); });

  const auto round = [format](const Rational& input) {
    return RoundRationalToFp(format, kRounding, input.Borrow());
  };
  RegisterIndependent(
      prefix + "round_rational" + suffix + "/word",
      [](auto& random, std::size_t) {
        return Fraction(static_cast<std::int64_t>(ExactWord(random, 22) | 1),
                        std::uint64_t{1} << 22);
      },
      round);
  RegisterIndependent(
      prefix + "round_rational" + suffix + "/wide",
      [](auto& random, std::size_t) { return WideRational(random, 192); },
      round);
  RegisterIndependent(
      prefix + "from_signed_bv" + suffix,
      [format](auto& random, std::size_t index) {
        const auto magnitude = format.ExponentWidth() == 3
                                   ? random() % 12 + 1
                                   : ExactWord(random, 32);
        const auto value =
            index % 2 == 0 ? magnitude : std::uint64_t{0} - magnitude;
        return BitVector::FromUint64(IntegerView(64), value).value();
      },
      [format](const BitVector& input) {
        return Fp::FromSignedBv(format, kRounding, input);
      });
  RegisterIndependent(
      prefix + "to_signed_bv" + suffix, positive, [](const Fp& input) {
        return Fp::ToSignedBv(IntegerView(64), kRounding, input);
      });

  const auto gap = [format](auto& random, std::size_t) {
    return FpWithExponentGap(random, format);
  };
  RegisterPair<kRoundedPair<Fp::Add>>(prefix + "add_gap" + suffix, gap);
  RegisterPair<kRoundedPair<Fp::Sub>>(prefix + "sub_gap" + suffix, gap);
  RegisterPair<Fp::Rem>(prefix + "rem_gap" + suffix, gap);
  RegisterPair<kRoundedPair<Fp::Div>>(
      prefix + "div_zero" + suffix, [=](auto& random, std::size_t index) {
        const auto category = index % 2 == 0 ? FpCategory::kNegativeZero
                                             : FpCategory::kPositiveZero;
        return std::array{positive(random, index),
                          Fp::Special(format, category).value()};
      });
}

void RegisterFloatingPoints() {
  for (const auto& [name, format] : std::span(kFpFormats).subspan(1)) {
    RegisterFloatingPointFormat(std::string(name), format);
  }
  for (const auto& [name, rounding] :
       {std::pair{"rne", RoundingMode::kRne}, {"rtp", RoundingMode::kRtp}}) {
    RegisterIndependent(
        "fp/add_tie/binary32/" + std::string(name),
        [](auto& random, std::size_t) {
          const auto format = FpFormat::FromCanonicalWidths(8, 24);
          const auto even_significand = 0x3f800000 | (random() & 0x7ffffe);
          return std::array{
              Fp::FromBits(format, IntegerView::FromUnsigned(even_significand))
                  .value(),
              Fp::FromBits(format, IntegerView(0x33800000)).value(),
          };
        },
        [rounding](const auto& inputs) {
          return Fp::Add(rounding, inputs[0], inputs[1]);
        });
  }
}

}  // namespace

void RegisterComputation() {
  RegisterIntegers();
  RegisterRationals();
  RegisterBitVectors();
  RegisterFloatingPoints();
}

}  // namespace dandelion::util_bench
