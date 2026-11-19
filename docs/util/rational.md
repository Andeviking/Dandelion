# Rational

Status: **proposed extension of an existing implemented value**. `Make`,
normalization, borrowing, ownership, `ParseDecimalFraction`, `Negate`
and `Divide` are implemented. The wider arithmetic design follows below.

## Value and callers

Represent `n/d` with Integer numerator `n`, positive Integer denominator `d`,
`gcd(abs(n),d)=1`, and unique zero `0/1`. Equality compares the canonical pair;
hash both mathematical components and never addresses or GMP limbs. An
independently callable `Make(n,d)` rejects zero denominator and copies and
normalizes inputs; negative denominators transfer their sign to the numerator.
Comparison is mathematical, and output is an unambiguous canonical fraction
or integer; SMT-LIB lexical output is the frontend/model printer's job.

Exact Rational is needed for the selected closed concrete `to_fp` grammar and
is also useful to integer linear relaxations and exact FP rounding. None of
these creates user-level Real terms or makes a rational relaxation an integer
solution. The frontend grammar is `Q ::= numeral | decimal | (- Q) |
(/ Q Q)` in the Real-constant argument of `to_fp`. Decode decimals exactly as
an integer over a power of ten, reject a zero denominator at every nested `/`
as `kUnsupportedFeature` at that frontend profile boundary, normalize once
valid, and retain an arbitrary RM expression as the node's child. The numeric
`Make` itself reports its established `kInvalidParameter` for denominator zero.
Rational zero has no negative sign; conversion of an exact Real zero produces
positive FP zero. Symbolic Real and `fp.to_real` input remain outside scope.

## Operations and representation

| Operation | Contract |
| --- | --- |
| Construct, copy, move, borrow, inspect numerator/denominator | Independent owned value; existing no-throw move leaves source `0/1`, self-move is valid, failed copy assignment preserves destination. Views require a live owner. |
| Sign, equality, compare, hash, canonical output | Exact; compare via cross products or an equivalent cancellation algorithm, with actual representation and allocation failures handled. |
| Integer promotion, exact integer check, floor, ceiling, truncation | Promote as `n/1`; report nonintegral exact conversion separately; floor/ceiling/truncation have their mathematical meanings for negatives. |
| Negate, absolute, add, subtract, multiply, divide | Exact canonical result. Checked division rejects a zero divisor; no implicit interpretation of a source division by zero. Cross-cancellation is allowed but not semantically observable. |
| Compare with dyadics and scale by a power of two | Needed for correct FP conversion; check representational arithmetic for shifts and allocation sizes. A compressed exponent or early comparison can avoid unnecessary materialization; do not impose a discretionary intermediate-size quota. |

Division and general arithmetic support known conversion and relaxation
callers. Extra operations such as rational powers, continued fractions or
specialized fused updates are algorithm candidates, not required surface area
until a real caller establishes their contracts. The initial implementation
retains two `Integer`s, as the node API returns their borrowed views and their
small forms can avoid big-number allocation. GCD/cross-cancellation or a
temporary `mpq_t` can implement exact operations. A permanent `mpq_t` may
reduce conversions for large fractions but would need stable compatible
Integer views or a deliberate API migration, and it changes allocation/copy
costs. Both are unmeasured. Denominator sign, normalization and strong failure
guarantees apply irrespective of representation; GMP limb OOM remains fatal.
