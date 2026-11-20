export module dandelion.util.numeric:bitvector;
import :error;
import :integer;
import std;

namespace dandelion::numeric::detail {
struct BitVectorAccess;
}

export namespace dandelion::numeric {

class BitVectorView;

class BitVector {
 public:
  inline BitVector() noexcept = default;
  BitVector(const BitVector&);
  BitVector& operator=(const BitVector&);
  inline BitVector(BitVector&& other) noexcept { MoveFrom(other); }
  BitVector& operator=(BitVector&&) noexcept;
  inline ~BitVector() noexcept {
    if (limb_count_ != 0) delete[] limbs_;
  }

  BitVectorView Borrow() const& noexcept;
  BitVectorView Borrow() const&& = delete;
  inline std::uint32_t Width() const noexcept { return width_; }
  std::uint32_t EffectiveBitLength() const noexcept;
  std::uint64_t Chunk(std::uint64_t index) const noexcept;
  bool TestBit(std::uint64_t index) const noexcept;
  std::optional<std::uint64_t> TryUint64() const noexcept;
  std::size_t BackingBytes() const noexcept;
  std::uint64_t Hash() const noexcept;
  bool operator==(const BitVector& other) const noexcept;
  NumericResult<std::string> ToBinary() const;

  static NumericResult<BitVector> Zero(IntegerView width);
  static NumericResult<BitVector> AllOnes(IntegerView width);
  static NumericResult<BitVector> FromUint64(IntegerView width,
                                             std::uint64_t value);
  static NumericResult<BitVector> FromIntegerExact(IntegerView width,
                                                   IntegerView value);
  static NumericResult<BitVector> FromIntegerModulo(IntegerView width,
                                                    IntegerView value);
  static NumericResult<BitVector> FromBinaryDigits(std::string_view digits);
  static NumericResult<BitVector> FromHexDigits(std::string_view digits);
  static NumericResult<BitVector> FromDecimalModulo(IntegerView width,
                                                    std::string_view digits);
  static NumericResult<Integer> ToUnsignedInteger(const BitVector& value);
  static NumericResult<Integer> ToSignedInteger(const BitVector& value);

  static NumericResult<BitVector> Not(const BitVector& value);
  static NumericResult<BitVector> Neg(const BitVector& value);
  static bool Nego(const BitVector& value) noexcept;
  static NumericResult<BitVector> And(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Or(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Xor(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Nand(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Nor(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Xnor(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Add(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Sub(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Mul(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Udiv(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Urem(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Sdiv(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Srem(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Smod(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Shl(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Lshr(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Ashr(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Comp(const BitVector& a, const BitVector& b);
  static NumericResult<BitVector> Concat(const BitVector& a,
                                         const BitVector& b);
  static NumericResult<bool> Ult(const BitVector& a, const BitVector& b);
  static NumericResult<bool> Ule(const BitVector& a, const BitVector& b);
  static NumericResult<bool> Ugt(const BitVector& a, const BitVector& b);
  static NumericResult<bool> Uge(const BitVector& a, const BitVector& b);
  static NumericResult<bool> Slt(const BitVector& a, const BitVector& b);
  static NumericResult<bool> Sle(const BitVector& a, const BitVector& b);
  static NumericResult<bool> Sgt(const BitVector& a, const BitVector& b);
  static NumericResult<bool> Sge(const BitVector& a, const BitVector& b);
  static NumericResult<bool> Uaddo(const BitVector& a, const BitVector& b);
  static NumericResult<bool> Saddo(const BitVector& a, const BitVector& b);
  static NumericResult<bool> Umulo(const BitVector& a, const BitVector& b);
  static NumericResult<bool> Smulo(const BitVector& a, const BitVector& b);
  static NumericResult<bool> Usubo(const BitVector& a, const BitVector& b);
  static NumericResult<bool> Ssubo(const BitVector& a, const BitVector& b);
  static NumericResult<bool> Sdivo(const BitVector& a, const BitVector& b);

  static NumericResult<BitVector> Extract(const BitVector& value,
                                          IntegerView high, IntegerView low);
  static NumericResult<BitVector> Repeat(const BitVector& value,
                                         IntegerView count);
  static NumericResult<BitVector> ZeroExtend(const BitVector& value,
                                             IntegerView count);
  static NumericResult<BitVector> SignExtend(const BitVector& value,
                                             IntegerView count);
  static NumericResult<BitVector> RotateLeft(const BitVector& value,
                                             IntegerView count);
  static NumericResult<BitVector> RotateRight(const BitVector& value,
                                              IntegerView count);

 private:
  friend struct detail::BitVectorAccess;
  inline void MoveFrom(BitVector& other) noexcept {
    limb_count_ = std::exchange(other.limb_count_, 0);
    width_ = std::exchange(other.width_, 1);
    if (limb_count_ != 0)
      limbs_ = other.limbs_;
    else
      word_ = other.word_;
    other.word_ = 0;
  }

  std::uint32_t width_ = 1;
  std::uint32_t limb_count_ = 0;
  union {
    std::uint64_t word_ = 0;
    std::uint64_t* limbs_;
  };
};

// Word views hold their bits by value. Keep the owner of packed bits alive
// and unchanged when using a wide view.
class BitVectorView {
 public:
  // The width must be positive, and the bits must fit within it.
  static BitVectorView FromCanonicalWord(std::uint32_t width,
                                         std::uint64_t word) noexcept;
  // The width must be positive. At least two trimmed limbs in increasing
  // order of significance must fit within the width.
  static BitVectorView FromCanonicalLimbs(
      std::uint32_t width, std::span<const std::uint64_t> limbs) noexcept;
  std::uint32_t Width() const noexcept { return width_; }
  std::uint32_t EffectiveBitLength() const noexcept;
  std::uint64_t Chunk(std::uint64_t index) const noexcept;
  std::optional<std::uint64_t> TryUint64() const noexcept;
  // Returns the borrowed limbs for a wide view and an empty span for a word.
  std::span<const std::uint64_t> Limbs() const noexcept;
  std::uint64_t Hash() const noexcept;
  bool operator==(BitVectorView other) const noexcept;
  BitVector Copy() const;

 private:
  explicit BitVectorView(std::uint32_t width) noexcept : width_(width) {}
  std::uint32_t width_;
  std::uint32_t limb_count_ = 0;
  union {
    std::uint64_t word_ = 0;
    const std::uint64_t* limbs_;
  };
};

}  // namespace dandelion::numeric
