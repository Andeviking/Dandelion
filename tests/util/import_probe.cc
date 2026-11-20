import dandelion.util.numeric;

// A standalone consumer needs only the primary import and dandelion_util.
int main() {
  using dandelion::numeric::BitVector;
  using dandelion::numeric::FloatingPointValue;
  using dandelion::numeric::FpFormat;
  using dandelion::numeric::FpOutcomeKind;
  using dandelion::numeric::Integer;
  using dandelion::numeric::IntegerView;
  using dandelion::numeric::NumericError;
  using dandelion::numeric::Rational;
  using dandelion::numeric::RoundingMode;
  using dandelion::numeric::RoundRationalToFp;

  const auto invalid = Integer::ParseDecimal("+1");
  if (invalid || invalid.error() != NumericError::kInvalidText) return 1;
  const auto value = Integer::ParseRadix("10000000000000001", 16);
  if (!value || value->Borrow().BitLength() != 65) return 2;
  const auto bits =
      BitVector::FromIntegerExact(IntegerView(65), value->Borrow());
  if (!bits || bits->Chunk(1) != 1 || bits->Chunk(0) != 1) return 3;
  const auto recovered = BitVector::ToUnsignedInteger(*bits);
  if (!recovered || recovered->Borrow() != value->Borrow()) return 4;

  const auto fraction = Rational::Make(IntegerView(6), IntegerView(4));
  if (!fraction || fraction->Borrow().Numerator().TryInt64().value_or(0) != 3 ||
      fraction->Borrow().Denominator().TryUint64().value_or(0) != 2) {
    return 5;
  }
  const auto format = FpFormat::Make(IntegerView(2), IntegerView(3));
  if (!format) return 6;
  const auto fp =
      RoundRationalToFp(*format, RoundingMode::kRne, fraction->Borrow());
  if (!fp || !fp->IsNormal()) return 7;
  const auto interchange = fp->ToBits();
  if (!interchange || interchange->TryUint64().value_or(0) != 6) return 8;
  const auto bv =
      FloatingPointValue::ToSignedBv(IntegerView(3), RoundingMode::kRne, *fp);
  if (!bv || bv->Kind() != FpOutcomeKind::kDefined ||
      bv->Value().TryUint64().value_or(0) != 2) {
    return 9;
  }
}
