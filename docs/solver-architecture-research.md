# SMT Solver Architecture Research

Status: research for discussion; not an implementation specification.

Research date: 2026-11-16. This survey uses published system descriptions,
official documentation, and selected implementation interfaces. Descriptions are
associated with their cited versions or publication dates; they should not be
read as an exhaustive or current feature matrix for every release. No Dandelion
implementation or benchmark comparison exists yet.

The companion [architecture proposal](architecture.md) derives tentative design
choices from this evidence. It is not a commitment to reproduce any solver's
implementation.

## 1. Systems examined

| System | Architecture evidence | Relevance to Dandelion |
| --- | --- | --- |
| Bitwuzla | The 2023 design uses a BV-centered abstraction and refinement loop. FP, arrays, and UF check candidates and contribute constraints; FP operations are reduced to BV using SymFPU. Its assertion-dependent state is backtrackable. [System paper](https://bitwuzla.github.io/data/NiemetzP-CAV23.pdf) | A strong reference for bit-precise solving, incremental preprocessing, and model-based refinement. Its BV-centered architecture does not by itself specify how to solve unbounded integer arithmetic. |
| cvc5 | The 2022 architecture has a propositional engine, theory engine, combination engine, model manager, node manager, and context-dependent data structures. It describes configurable central or distributed equality engines. [System paper](https://cvc5.github.io/papers/2022/BarbosaBBKLMMMN-TACAS22.pdf) | A reference for a broad set of theories and explicit coordination of equality, shared terms, and models. |
| Z3 | The author-maintained [Programming Z3](https://z3prover.github.io/papers/programmingz3) describes an SMT core with CDCL(T), equality and theory reasoning, as well as separate SAT-oriented engines and configurable tactics. Its architecture overview is explicitly a snapshot of Z3 4.8. | A reference for theory propagation, multiple solving strategies, and APIs that expose models, scopes, and assumptions. Do not assume every tactic uses the same internal engine. |
| Yices 2 | The 2014 paper describes shared, hash-consed terms and types with configurable solver contexts. The 2023 competition description reports CDCL(T) and MCSAT, with MCSAT used by default for nonlinear arithmetic in that configuration. [2014 paper](https://yices.csl.sri.com/papers/cav2014.pdf), [2023 description](https://smt-comp.github.io/2023/system-descriptions/Yices2.pdf) | A counterexample to committing all arithmetic and BV problems to a single solving algorithm. Its MCSAT capabilities and limitations depend on configuration and version. |
| MathSAT5 | The 2013 paper describes a SAT engine, a theory manager, specialized theory solvers, multiple BV/FP approaches, and model/proof support. It discusses third-party SAT integration requirements. [System paper](https://es-static.fbk.eu/people/griggio/papers/tacas13.pdf) | A useful reference for theory orchestration and the distinction between an abstract SAT interface and the callbacks required for deeper integration. |
| SMTInterpol | Its 2020 description presents CDCL(T), congruence closure, linear arithmetic, array reasoning, models, and interpolation. Its 2024 description also reports an experimental BV-to-integer solver. [2020 description](https://ultimate.informatik.uni-freiburg.de/smtinterpol/sysdesc-2020.pdf), [2024 description](https://ultimate.informatik.uni-freiburg.de/smtinterpol/sysdesc2024.pdf) | Particularly relevant to program-verification clients, interpolation, and a different BV/Int design point. The 2024 description explicitly notes limits in proof checking for BV rewrites. |
| STP | The 2022 competition description presents word-level BV simplification, eager CNF encoding, and array refinement. The current code guide documents node factories and SAT adapters, as well as separate extensionality machinery. [2022 description](https://smt-comp.github.io/2022/system-descriptions/STP.pdf), [current code guide](https://stp.github.io/stp/code-guide.html) | A focused reference for the BV-to-SAT pipeline. Array support changed over time: the 2022 description's non-extensional scope must not be assumed to describe every later version. |
| OpenSMT | Its README snapshot describes a quantifier-free solver focused on UF, arrays, linear arithmetic and difference logic, incrementality, and interpolation, with explicit restrictions on supported combinations. [README at `e9748d3`](https://github.com/usi-verification-and-security/opensmt/blob/e9748d3b9026cb1926f4ae20d343aee46b02d9f5/README.md) | A reference for an arithmetic and verification-oriented architecture and for stating combination support precisely. |

These systems expose complementary design points, not a ranking. This survey
does not establish that any one architecture will be fastest for Dandelion.

## 2. The main architectural alternatives

### 2.1 Eager bit-blasting

For BV-heavy problems, an effective baseline is to rewrite terms at word level,
encode their bit-level behavior as Boolean gates, convert gates to CNF, and
call a SAT solver. This preserves exact BV semantics if the encoding is correct.
An AIG is a useful candidate intermediate representation, not a requirement:
direct CNF, different gate representations, and structural hashing have
different construction and memory costs. The Bitwuzla paper and [STP 2022
description](https://smt-comp.github.io/2022/system-descriptions/STP.pdf)
describe variants of this approach.

Advantages include reuse of strong SAT engines and predictable support for
finite-width operations. Costs can be substantial for wide multiplication,
division, or repeated encodings. Word-level rewriting, circuit sharing, and
selective abstraction are important candidates to measure rather than assume.
[Scalable Bit-Blasting with Abstractions](https://link.springer.com/chapter/10.1007/978-3-031-65627-9_9)
describes a refinement approach for expensive BV terms.

### 2.2 CDCL(T) and model-based refinement

A general SMT engine may abstract theory atoms into Boolean variables, ask SAT
for an assignment, and use theory reasoning to detect conflicts, derive lemmas,
or build a compatible model. At one integration extreme, theory checks occur
after SAT has found a Boolean candidate. At another, theories receive partial
assignments during search and produce propagations and explanations. These are
architectural choices with different integration costs and performance
characteristics, not interchangeable descriptions of an identical algorithm.
[Programming Z3](https://z3prover.github.io/papers/programmingz3) discusses
theory propagation, while the Bitwuzla and cvc5 papers illustrate different
forms of coordination.

An externally supplied SAT solver is not automatically a full CDCL(T) engine.
A minimal incremental SAT API supports a candidate-and-refinement loop, but
does not necessarily expose decisions, backtracks, and propagation during SAT
search. The [IPASIR-UP paper](https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.SAT.2023.8)
reports integrating user propagation with CaDiCaL in cvc5. This supports an
experiment with tight integration; it does not establish that this interface
or one backend is optimal for our workloads.

### 2.3 Alternative arithmetic and BV engines

Yices' 2023 description uses a CDCL(T) architecture alongside MCSAT, choosing
the latter for nonlinear arithmetic in the documented configuration. MCSAT
works with theory-level model construction and explanations; it is not merely
another name for bit-blasting. Its features differ from those of the CDCL(T)
implementation, as the [Yices context documentation](https://yices.csl.sri.com/doc/context-operations.html)
explains.

Integer and BV representations can also be translated between each other.
[Int-blasting](https://cvc5.github.io/papers/2022/ZoharIMNNPRBT-VMCAI22.pdf)
is a family of BV-to-integer reductions. A [2026 lazy, modular translation](https://link.springer.com/article/10.1007/s00236-026-00529-y)
illustrates a different point in this design space. Benefits and costs depend
on the operations and benchmarks: integer translations can retain arithmetic
structure, but bitwise operations and modulo constraints may become expensive.
Neither translation should be selected as the universal engine without data.

These alternatives justify preserving high-level BV and integer structure until
the selected strategy lowers it. They do not make int-blasting the sole purpose
of the project.

### 2.4 Shared semantics and specialized solving

These choices describe different dimensions, not four mutually exclusive
solver architectures. Theory checks may be interleaved with SAT search or
performed on full candidates, and an engine may use both. MCSAT changes the
search and explanation mechanism by admitting theory-value decisions. Choosing
among specialized and general procedures is an organizational decision, not
another theory-combination calculus.

The [cvc5 2022 paper](https://cvc5.github.io/papers/2022/BarbosaBBKLMMMN-TACAS22.pdf)
describes both lazy BV handling within CDCL(T) and eager bit-blasting when the
problem can be reduced to BV. The [Yices 2023 description](https://smt-comp.github.io/2023/system-descriptions/Yices2.pdf)
describes CDCL(T) and MCSAT in the same solver, with MCSAT selected for
nonlinear arithmetic in that configuration. The [MCSAT BV study](https://arxiv.org/abs/2004.07940)
uses word-level explanations for some fragments and local bit-blasting for
other cases. These examples support considering specialized procedures; they
do not prove that a dispatcher or MCSAT covers Dandelion's whole scope, nor
that any route will be faster on its workloads.

For Dandelion, a CDCL(T)-style mixed-theory core is a reasonable general
candidate alongside a direct BV/FP path and other specialized procedures.
Final checks and model reconstruction must still cover the entire input.
Path applicability depends on actual sorts, operators, and combinations, not
just the `set-logic` name. A complete procedure also needs a progress argument:
repeated sound candidate checks or an indefinitely postponed fallback do not by
themselves establish termination for IDL or LIA. The relative benefit of
search-time propagation, separate paths, and their scheduling remains open.

## 3. Term representation and ownership

cvc5's 2022 paper describes immutable, hash-consed term DAGs managed by a node
manager, with reference counting and separate attributes. The Yices 2.2 paper
also describes hash-consed terms and types. This representation can make
structural identity checks cheap and avoid repeated storage, but its unique
tables, memory reclamation, synchronization, and per-node metadata have costs.
The relevant tradeoff is total runtime and memory on real workloads, not just
the number of allocations.

Structural identity is not semantic equality. Two different terms may become
equal under assertions, while two textually identical symbol names may refer
to different declarations or scopes. An equality engine therefore has a
different responsibility from hash-consing. Sorting, indexed operators,
overloading, symbol identity, and ownership all matter to a correct term key.

cvc5 and Bitwuzla both document scoped or backtrackable data structures, but
node lifetime and search-state lifetime are distinct problems. A useful
Dandelion prototype should compare manager-owned arenas or stable handles with
reference-counted ownership instead of assuming one is universally best.

Sources: [cvc5 system paper](https://cvc5.github.io/papers/2022/BarbosaBBKLMMMN-TACAS22.pdf),
[Yices 2.2 paper](https://yices.csl.sri.com/papers/cav2014.pdf), and
[Bitwuzla 2023 paper](https://bitwuzla.github.io/data/NiemetzP-CAV23.pdf).

## 4. Theory combination is a separate problem

It is not sufficient for each individual theory to report that its own
constraints are satisfiable. The results must extend to a single interpretation
of shared terms. Relevant information can include equalities, disequalities,
cardinality constraints, interpreted conversions, and actual model values.
cvc5's 2022 paper describes a combination engine, equality arrangements on
shared terms, and a model manager; Bitwuzla's 2023 paper describes coordination
using candidate values and refinement.

The standard Nelson–Oppen result has hypotheses, including conditions on the
component signatures and stable infiniteness. Fixed-width BV sorts are finite;
the conditions cannot be silently assumed for BV combined with other theories.
This does not make combination impossible: it means a suitable method must
account for finite domains and establish its own correctness conditions.
[Combination properties and politeness](https://theory.stanford.edu/~barrett/pubs/TZN25.pdf)
discusses this issue.

For example, three pairwise-distinct 1-bit terms are inconsistent because the
sort has only two values. A coordinator that treated those terms as elements of
an unrestricted infinite domain could accept a spurious candidate.

The [SMT-LIB 2.7 datatype declaration rules](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)
limit where a newly declared recursive datatype may occur inside a constructor
field sort. This is distinct from ordinary combinations such as arrays of
datatype values. The [nested-datatypes study](https://arxiv.org/abs/2606.30888)
defines and studies an extension that permits recursion through arrays and
addresses cycles hidden through array elements. Its results have their own
hypotheses and are not a completeness proof for every target combination.

Mixed Int/BV conversion is more than shared equality. The current
[SMT-LIB BV theory](https://smt-lib.org/theories-FixedSizeBitVectors.shtml)
specifies `ubv_to_int`, `sbv_to_int`, and indexed `int_to_bv`. In particular,
unsigned BV-to-Int values have a width-dependent range, and converting an
integer to `w` bits uses its residue modulo `2^w`. Signed interpretation has a
different range and conversion. These operators require explicit, correct
bridging or an equivalent verified translation.

## 5. Theory-specific observations

- **BV:** preserve widths and modular semantics; division by zero, signed
  operations, shifts, overflow predicates, and indexed operators follow the
  selected SMT-LIB version, not C++ machine arithmetic.
- **FP:** Bitwuzla's 2023 paper uses SymFPU to reduce FP operations to BV.
  The [FP theory](https://smt-lib.org/theories-FloatingPoint.shtml) distinguishes
  `fp.eq` from core equality and specifies NaN, signed zero, rounding, and
  underspecified conversions. Host floating-point arithmetic is not a general
  substitute for these semantics.
- **Integers:** exact linear reasoning can use rational arithmetic internally
  even when the solver does not expose the SMT-LIB `Real` sort. NIA is
  undecidable in general, so a practical engine must support an honest
  `unknown` or resource-limited outcome. [Yices' NIA paper](https://yices.csl.sri.com/papers/vmcai2017.pdf)
  discusses direct integer reasoning. An unsatisfiable result for an
  artificially bounded search space does not establish unbounded UNSAT.
- **Arrays:** read-over-write and extensionality are distinct obligations.
  Extensional disequality may require a fresh witness index; generated terms
  and clauses must have correct model-extension and scope semantics. See
  [SMT-LIB ArraysEx](https://smt-lib.org/theories-ArraysEx.shtml). Constant
  arrays are an additional input-profile decision; a [2026 decision procedure](https://arxiv.org/abs/2605.16820)
  studies their combination with extensional arrays over arbitrary index
  domains, including finite ones.
- **UF:** congruence requires equal arguments to produce equal results. A
  model must assign a consistent function interpretation, including at
  argument values that become equal only after theory combination.
- **DT:** constructors are disjoint and injective, and inductive datatypes
  require acyclicity. Selector behavior on the wrong constructor must follow
  the standard rather than an invented default. Finite datatypes also raise
  cardinality issues. See the [CVC4 datatype procedure](https://cvc4.github.io/publications/2015/RB15.pdf)
  and [cvc5 datatype reference](https://cvc5.github.io/docs/cvc5-1.3.0/theories/datatypes.html).

## 6. State, models, and SAT backends

The surveyed solvers manage several kinds of state, including terms, SAT search
decisions, and, where supported, user assertion scopes. cvc5 explicitly
discusses both user and SAT contexts; Bitwuzla describes backtrackable state for
incremental solving. These are observations about those systems. Dandelion's
full-scope proposal targets a single user query and does not require an
assertion-scope or multi-query framework.

Preprocessing must preserve enough information to recover values for the
original problem. Model combination is an additional responsibility: a SAT
assignment or separate per-theory values are not, by themselves, a complete
model for arrays, functions, datatypes, and shared terms.

For SAT integration, the [CaDiCaL 3.0.1 API](https://github.com/arminbiere/cadical/blob/rel-3.0.1/src/cadical.hpp)
documents incremental clauses, assumptions, model queries, and an external
propagator. Its integration has explicit state and observed-variable contracts.
The [cvc5 Kissat adapter at `79ce23a`](https://github.com/cvc5/cvc5/blob/79ce23a63d08f7ad43302f01fd27c41336bc9f5b/src/prop/kissat.cpp)
reports that incremental solving with assumptions is not implemented there;
the [Yices 2023 description](https://smt-comp.github.io/2023/system-descriptions/Yices2.pdf)
uses Kissat for single-query BV configurations. These observations help
compare candidate backends; Dandelion's adapter should provide the
capabilities used by its single-query theory-coordination algorithms.

The [Bitwuzla Kissat adapter at `06146aa`](https://github.com/bitwuzla/bitwuzla/blob/06146aa472d523b3da55f60b104c633d7c63a8ce/src/sat/kissat.cpp)
reinitializes Kissat on subsequent solves and replays accumulated clauses.
This particular integration does not reuse the previous solver instance's
learned state. The cost matters when a single user query triggers many SAT
calls, but the source alone does not establish a backend performance ranking.

## 7. Evaluation evidence and local inputs

Competition results are useful context but do not compare Dandelion, which has
no implementation yet. The [SMT-COMP 2025 QF_Equality_Bitvec results](https://smt-comp.github.io/2025/results/qf_equality_bitvec-single-query/)
list 8,489 selected queries, including 76 `QF_UFBVDT` queries. Bitwuzla solved
8,279 and cvc5 solved 8,009; the result table separately reports timeouts,
memory exhaustion, and abstentions. The [QF_UFBVDT results](https://smt-comp.github.io/2025/results/qf_ufbvdt-single-query/)
show the datatype-containing subset independently. This is one selected
division and track; it does not establish a solver-wide performance ordering
or say what an architecture change would do.

The local `/pub/netdisk1/zhangx/benchmark-2025/non-incremental/benchmarks.json`
index provides useful input families. `2025` is the local directory label;
the upstream snapshot revision has not been independently established.
Counting unique relative paths across its group records gives
46,191 `QF_BV`, 17,250 `QF_BVFP`, 15,148 `QF_ABV`, 13,306 `QF_LIA`, 25,452
`QF_NIA`, 8,700 `QF_DT`, and 76 `QF_UFBVDT` entries. These are indexed paths in
the local snapshot, not distinct source programs, the number selected for a
particular competition run, or evidence that a future solver will solve them.
The index can contain the same path in multiple group records; the figures above
deduplicate by relative path. The inspected index's SHA-256 is
`1970b0d6667d68eeb859ac5b917984228ebd723803be5e74d2224dc2514f250c`.
Benchmark directories must remain read-only.

A separate lexical inspection of all `.smt2` files in local directories
`UFBVLIA`, `UFBVDTLIA`, `UFBVDTNIA`, `AUFBVDTLIA`, and `AUFBVDTNIA` found
2,624 files; all contained explicit quantifier syntax. Conversion operator
names occurred in 544 files, all in the `20240618-LibMLKEM` family. The sampled
source metadata attributes that family to GNATprove and Why3. These are
quantified scripts, so this observation does **not** establish the frequency of
BV/Int conversion in the targeted quantifier-free workload. The inspection
ignores comments, strings, and quoted symbols; it does not type-check terms or
determine whether they are relevant to an assertion.

The following read-only Python script reproduces both counts on the local Linux
snapshot. It uses `O_NOATIME` for files and directories and writes no output
files. If the filesystem or permissions do not permit that flag, it must fail
rather than retry in a way that could change dataset metadata.

```python
import collections
import hashlib
import json
import os
import re

root = "/pub/netdisk1/zhangx/benchmark-2025/non-incremental"
directory_flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOATIME | os.O_NOFOLLOW
file_flags = os.O_RDONLY | os.O_NOATIME | os.O_NOFOLLOW
lexer = re.compile(rb';[^\r\n]*|"(?:[^"]|"")*"|\|[^|]*\||[^\s();]+|[()]')
conversions = {
    b"ubv_to_int", b"sbv_to_int", b"int_to_bv", b"bv2nat", b"bv2int",
    b"int2bv", b"nat2bv",
}

def files(directory, prefix=""):
    with os.scandir(directory) as entries:
        entries = sorted(entries, key=lambda entry: entry.name)
    for entry in entries:
        if entry.is_dir(follow_symlinks=False):
            child = os.open(entry.name, directory_flags, dir_fd=directory)
            try:
                yield from files(child, prefix + entry.name + "/")
            finally:
                os.close(child)
        elif entry.is_file(follow_symlinks=False) and entry.name.endswith(".smt2"):
            child = os.open(entry.name, file_flags, dir_fd=directory)
            with os.fdopen(child, "rb") as stream:
                yield prefix + entry.name, stream.read()

root_fd = os.open(root, directory_flags)
try:
    index_fd = os.open("benchmarks.json", file_flags, dir_fd=root_fd)
    with os.fdopen(index_fd, "rb") as stream:
        index_data = stream.read()
    groups = json.loads(index_data)
    print("index_sha256", hashlib.sha256(index_data).hexdigest())
    paths = {path for group in groups for path in group["benchmarks"]}
    logic_counts = collections.Counter(path.split("/", 1)[0] for path in paths)
    for logic in ("QF_BV", "QF_BVFP", "QF_ABV", "QF_LIA", "QF_NIA", "QF_DT", "QF_UFBVDT"):
        print(logic, logic_counts[logic])

    for name in ("UFBVLIA", "UFBVDTLIA", "UFBVDTNIA", "AUFBVDTLIA", "AUFBVDTNIA"):
        counts = collections.Counter()
        families = collections.Counter()
        digest = hashlib.sha256()
        directory = os.open(name, directory_flags, dir_fd=root_fd)
        try:
            for relative_path, data in files(directory):
                digest.update(relative_path.encode("utf-8") + b"\0")
                digest.update(hashlib.sha256(data).digest())
                tokens = [m.group() for m in lexer.finditer(data)
                          if m.group()[:1] not in (b";", b'"', b"|")]
                heads = {tokens[i + 1] for i, t in enumerate(tokens[:-1]) if t == b"("}
                indexed = {tokens[i + 2] for i, t in enumerate(tokens[:-2])
                           if t == b"(" and tokens[i + 1] == b"_"}
                counts["files"] += 1
                counts["quantified"] += bool(heads & {b"forall", b"exists"})
                has_conversion = bool((heads | indexed) & conversions)
                counts["conversions"] += has_conversion
                if has_conversion:
                    families[relative_path.split("/", 1)[0]] += 1
        finally:
            os.close(directory)
        print(name, counts["files"], counts["quantified"], counts["conversions"],
              "sha256_path_content", digest.hexdigest(), dict(families))
finally:
    os.close(root_fd)
```

## 8. Implications and unresolved questions

The following are design inferences for Dandelion, not claims made by the
surveyed systems:

1. A shared typed expression layer can represent all targeted theories;
   ownership, layout, and sharing costs require measurement.
2. A BV-to-SAT path and a general theory coordinator serve complementary
   roles in the proposed single-query architecture.
3. Shared equality, finite sorts, conversion semantics, and model construction
   are required considerations for the targeted theory combinations.
4. SAT and theory procedures need explicit communication and correctness
   contracts. Full-candidate checks and propagation during search have
   different backend requirements and performance costs.
5. Internal exact rational numbers may help integer algorithms and FP constant
   conversion without adding general Real formulas to the scope.
6. Performance choices require profiling and comparison on a working solver;
   the papers do not establish their performance for Dandelion.

No external datasets were retrieved for this research. Local benchmark
directories were inspected only for availability; no solver performance
measurements were made.
