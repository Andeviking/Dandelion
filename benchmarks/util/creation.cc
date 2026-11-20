#include "harness.h"

namespace dandelion::util_bench {
namespace {

template <class Result, class Construct>
void RunRetained(benchmark::State& state, Construct construct) {
  std::array<std::optional<Result>, kBatchSize> slots;
  auto fill = [&] {
    for (std::size_t index = 0; index < slots.size(); ++index) {
      construct(index, slots[index]);
      benchmark::DoNotOptimize(*slots[index]);
    }
  };
  auto release = [&] {
    for (auto& slot : slots) slot.reset();
  };
  fill();
  release();
  for (auto iteration : state) {
    (void)iteration;
    fill();
    benchmark::ClobberMemory();
    state.PauseTiming();
    release();
    state.ResumeTiming();
  }
  ReportBatch(state);
}

template <class Inputs, class Factory>
void RunCreation(benchmark::State& state, const Inputs& inputs,
                 Factory factory) {
  using Result =
      std::invoke_result_t<Factory, const typename Inputs::value_type&>;
  RunRetained<Result>(state, [&](std::size_t index, auto& slot) {
    const auto& input = inputs[index];
    benchmark::DoNotOptimize(&input);
    slot.emplace(factory(input));
  });
}

template <bool Move, class T>
void RunConsume(benchmark::State& state,
                const std::array<T, kBatchSize>& seeds) {
  std::array<std::optional<T>, kBatchSize> sources;
  std::array<std::optional<T>, Move ? kBatchSize : 0> destinations;
  auto prepare = [&] {
    for (std::size_t index = 0; index < sources.size(); ++index) {
      sources[index].emplace(seeds[index]);
      if constexpr (Move) destinations[index].reset();
    }
  };
  auto consume = [&] {
    for (std::size_t index = 0; index < sources.size(); ++index) {
      auto& source = sources[index];
      benchmark::DoNotOptimize(*source);
      if constexpr (Move) {
        auto& destination = destinations[index].emplace(std::move(*source));
        benchmark::DoNotOptimize(destination);
      } else {
        source.reset();
      }
    }
    benchmark::ClobberMemory();
  };
  prepare();
  consume();
  if constexpr (Move) {
    const T empty;
    for (std::size_t index = 0; index < sources.size(); ++index) {
      Require(sources[index]->Borrow() == empty.Borrow(),
              "Move did not reset the source");
      Require(destinations[index]->Borrow() == seeds[index].Borrow(),
              "Move changed the value");
    }
  }
  for (auto iteration : state) {
    (void)iteration;
    state.PauseTiming();
    prepare();
    state.ResumeTiming();
    consume();
  }
  ReportBatch(state);
}

template <class Generator>
void RegisterLifecycle(std::string type, std::string profile,
                       Generator generate) {
  const auto prefix = "util/create/" + type + "/";
  Register(prefix + "copy/" + profile, [generate](benchmark::State& state) {
    const auto sources = Generate(generate);
    using T = typename decltype(sources)::value_type;
    {
      auto source = sources.front();
      const auto copy = source;
      source = T{};
      Require(copy.Borrow() == sources.front().Borrow(),
              "Copy did not retain the source value");
    }
    RunRetained<T>(state, [&](std::size_t index, auto& slot) {
      benchmark::DoNotOptimize(&sources[index]);
      slot.emplace(sources[index]);
    });
  });
  Register(prefix + "move/" + profile, [generate](benchmark::State& state) {
    RunConsume<true>(state, Generate(generate));
  });
  Register(prefix + "destroy/" + profile, [generate](benchmark::State& state) {
    RunConsume<false>(state, Generate(generate));
  });
}

template <class Generator, class Factory>
void RegisterFactory(std::string name, Generator generator, Factory factory) {
  Register("util/create/" + name, [=](benchmark::State& state) {
    RunCreation(state, Generate(generator), factory);
  });
}

std::string Digits(std::mt19937_64& random, std::size_t count, unsigned radix) {
  constexpr std::string_view kAlphabet = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::string digits(count, '0');
  for (auto& digit : digits) digit = kAlphabet[random() % radix];
  digits.front() = kAlphabet[radix / 2 + random() % (radix - radix / 2)];
  return digits;
}

void RegisterIntegers() {
  const auto small_input = [](std::mt19937_64& random, std::size_t index) {
    return index == 0 ? std::int64_t{42}
                      : static_cast<std::int64_t>(random() % 1'000'000);
  };
  const auto small = [](std::int64_t value) { return Integer(value); };
  RegisterFactory("integer/create/small", small_input, small);
  RegisterFactory(
      "integer/from_unsigned/64_high",
      [](auto& random, std::size_t) { return ExactWord(random, 64); },
      [](std::uint64_t value) { return Integer::FromUnsigned(value).value(); });
  Register("util/create/integer/create_release/small",
           [=](benchmark::State& state) {
             const auto inputs = Generate(small_input);
             RunBatch(state, [&] {
               for (auto input : inputs) {
                 benchmark::DoNotOptimize(input);
                 auto result = small(input);
                 benchmark::DoNotOptimize(result);
               }
             });
           });
  RegisterLifecycle("integer", "small", [=](auto& random, std::size_t index) {
    return small(small_input(random, index));
  });

  for (const std::uint32_t bits : {128u, 4096u}) {
    const auto profile = std::to_string(bits);
    const auto generate = [bits](auto& random, std::size_t) {
      return Magnitude(random, bits);
    };
    const auto create = [](const auto& limbs) {
      return IntegerView::FromCanonicalMagnitude(limbs, false).Copy();
    };
    RegisterFactory("integer/create/" + profile, generate, create);
    Register("util/create/integer/create_release/" + profile,
             [=](benchmark::State& state) {
               const auto inputs = Generate(generate);
               RunBatch(state, [&] {
                 for (const auto& limbs : inputs) {
                   benchmark::DoNotOptimize(&limbs);
                   auto result = create(limbs);
                   benchmark::DoNotOptimize(result);
                 }
               });
             });
    RegisterLifecycle("integer", profile, [bits](auto& random, std::size_t) {
      return ExactInteger(random, bits);
    });
  }

  for (const std::uint32_t bits : {31u, 64u, 128u, 4096u}) {
    const auto profile = std::to_string(bits);
    RegisterFactory(
        "integer/parse_decimal/" + profile,
        [bits](auto& random, std::size_t index) {
          if (bits == 64 && index == 0)
            return std::string("9223372036854775808");
          return ExactInteger(random, bits, index % 2 != 0).ToDecimal();
        },
        [](std::string_view text) {
          return Integer::ParseDecimal(text).value();
        });
    for (const unsigned radix : {2u, 16u}) {
      const auto digits = radix == 2 ? bits : (bits + 3) / 4;
      RegisterFactory(
          "integer/parse_radix/" + std::to_string(radix) + "/" + profile,
          [=](auto& random, std::size_t) {
            return Digits(random, digits, radix);
          },
          [radix](std::string_view text) {
            return Integer::ParseRadix(text, static_cast<int>(radix)).value();
          });
    }
  }
}

void RegisterRationals() {
  const auto make = [](const std::array<Integer, 2>& parts) {
    return Rational::Make(parts[0].Borrow(), parts[1].Borrow()).value();
  };
  RegisterFactory(
      "rational/make/reduced",
      [](auto& random, std::size_t) {
        return std::array{
            Integer(static_cast<std::int64_t>(random() % 512 | 1)),
            Integer(std::int64_t{1} << (2 + random() % 60))};
      },
      make);
  RegisterFactory(
      "rational/make/strong_reduction",
      [](auto& random, std::size_t index) {
        if (index == 0) return std::array{Integer(-2), Integer(-4)};
        const auto factor = ExactInteger(random, 256);
        return std::array{(factor.Borrow() * IntegerView(-2)).value(),
                          (factor.Borrow() * IntegerView(-4)).value()};
      },
      make);
  RegisterFactory(
      "rational/make/mixed",
      [](auto& random, std::size_t) {
        return std::array{ExactInteger(random, 256, true),
                          ExactInteger(random, 29)};
      },
      make);
  for (const auto size : {3u, 96u}) {
    RegisterFactory(
        "rational/parse_decimal_fraction/" + std::to_string(size),
        [size](auto& random, std::size_t index) {
          if (size == 3 && index == 0) return std::string("1.250");
          return std::to_string(random() % 1'000) + "." +
                 Digits(random, size, 10);
        },
        [](std::string_view text) {
          return Rational::ParseDecimalFraction(text).value();
        });
  }
  RegisterLifecycle("rational", "word", [](auto& random, std::size_t) {
    return WordRational(random);
  });
  RegisterLifecycle("rational", "wide", [](auto& random, std::size_t) {
    return WideRational(random, 256);
  });
}

void RegisterBitVectors() {
  constexpr std::array widths{
      32u,  64u,   65u,    128u,
      256u, 4096u, 65536u, std::numeric_limits<std::uint32_t>::max()};
  for (const auto width : widths) {
    const auto profile = std::to_string(width) + (width == 64  ? "_high"
                                                  : width > 64 ? "_sparse"
                                                               : "");
    RegisterFactory(
        "bv/from_uint64/" + profile,
        [width](auto& random, std::size_t) {
          return ExactWord(random, std::min(width, 64u));
        },
        [width](std::uint64_t value) {
          return BitVector::FromUint64(IntegerView(width), value).value();
        });
  }
  for (const auto width : std::span(widths).first(widths.size() - 1)) {
    const auto profile = std::to_string(width);
    RegisterFactory(
        "bv/from_binary/" + profile,
        [width](auto& random, std::size_t) { return Digits(random, width, 2); },
        [](std::string_view text) {
          return BitVector::FromBinaryDigits(text).value();
        });
    if (width % 4 == 0) {
      RegisterFactory(
          "bv/from_hex/" + profile,
          [width](auto& random, std::size_t) {
            return Digits(random, width / 4, 16);
          },
          [](std::string_view text) {
            return BitVector::FromHexDigits(text).value();
          });
    }
    RegisterFactory(
        "bv/from_decimal_modulo/" + profile,
        [width](auto& random, std::size_t) {
          return ExactInteger(random, width + 17).ToDecimal();
        },
        [width](std::string_view text) {
          return BitVector::FromDecimalModulo(IntegerView(width), text).value();
        });
    if (width == 64 || width == 256 || width == 65536) {
      RegisterLifecycle("bv", profile, [width](auto& random, std::size_t) {
        return DenseBv(random, width, width);
      });
    }
  }
}

void RegisterFloatingPoints() {
  constexpr std::array categories{
      FpCategory::kNaN, FpCategory::kPositiveInfinity,
      FpCategory::kNegativeInfinity, FpCategory::kPositiveZero,
      FpCategory::kNegativeZero};
  for (const auto& [name, format] : std::span(kFpFormats).first<4>()) {
    const std::string profile(name);
    const auto bits = [format](auto& random, std::size_t index) {
      const auto exponent =
          (std::uint32_t{1} << (format.ExponentWidth() - 1)) - 1;
      return FpBits(random, format, exponent, index % 2 != 0);
    };
    const auto from_bits = [format](const BitVector& input) {
      return FloatingPointValue::FromBits(format, input).value();
    };
    RegisterFactory("fp/from_bits/" + profile, bits, from_bits);
    RegisterFactory(
        "fp/special/" + profile,
        [categories](auto&, std::size_t index) {
          return categories[index % categories.size()];
        },
        [format](FpCategory category) {
          return FloatingPointValue::Special(format, category).value();
        });
    RegisterLifecycle("fp", profile, [=](auto& random, std::size_t index) {
      return from_bits(bits(random, index));
    });
  }
}

}  // namespace

void RegisterCreation() {
  RegisterIntegers();
  RegisterRationals();
  RegisterBitVectors();
  RegisterFloatingPoints();
}

}  // namespace dandelion::util_bench
