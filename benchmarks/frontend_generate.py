#!/usr/bin/env python3
"""Generate deterministic SMT-LIB frontend benchmark inputs."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
from itertools import repeat
from pathlib import Path
import random

from process import outside_dataset, write_json

SCALES = {"smoke": 8, "light": 128, "medium": 4096, "heavy": 65536}

BV = "(_ BitVec 32)"
FP = "(_ FloatingPoint 8 24)"
BV_OPERAND = "(bvadd ((_ zero_extend 16) ((_ extract 23 8) word)) (concat #x0000 #x0001))"
FP_RATIONAL = "((_ to_fp 8 24) RNE (/ (- {value}) 3))"
BOX = "(declare-datatypes ((Box 1)) ((par (T) ((empty) (box (item T))))))"
LIST = "(declare-datatype List ((nil) (cons (head Bool) (tail List))))"
DT_TERM = (
    "(ite (head (cons {symbol} nil)) ((_ is box) packet) "
    "(match packet (((box x) x) (empty false))))"
)

# A template can request a distinct declared operand for each wide argument.
THEORIES = {
    "QF_SAT": ("core", (), "Bool", "(let ((x (not {symbol}))) (= (ite x true false) {symbol}))"),
    "QF_BV": ("bv", (f"(declare-const word {BV})",), None,
              f"(= {BV_OPERAND} {{bv}})"),
    "QF_UFBV": ("bv", (f"(declare-const word {BV})", f"(declare-fun transform ({BV}) {BV})"),
                None, f"(= (transform {BV_OPERAND}) {{bv}})"),
    "QF_FP": ("fp", (f"(declare-const sample {FP})", "(declare-const narrow Float16)"),
              None, f"(fp.eq (fp.add RTP sample {FP_RATIONAL}) ((_ to_fp 8 24) RTN narrow) "
                    "(_ +zero 8 24))"),
    "QF_UFFP": ("fp", (f"(declare-fun transform ({FP}) {FP})",), None,
                f"(fp.lt (transform {FP_RATIONAL}) (_ +oo 8 24))"),
    "QF_BVFP": ("fp", (f"(declare-const word {BV})",), None,
                f"(fp.eq ((_ to_fp 8 24) {BV_OPERAND}) {FP_RATIONAL})"),
    "QF_IDL": ("int", ("(declare-const x Int)", "(declare-const y Int)"), None,
               "(<= (- x y) {value})"),
    "QF_LIA": ("int", ("(declare-const x Int)", "(declare-const y Int)"), None,
               "(>= (+ (* 2 x) (* 3 y)) {large})"),
    "QF_NIA": ("int", ("(declare-const x Int)", "(declare-const y Int)"), None,
               "(< (* x (+ y {value})) {large})"),
    "QF_EIA": ("int", ("(declare-const x Int)",), None, "(= (** x 3) {large})"),
    "QF_UFIDL": ("int", ("(declare-const x Int)", "(declare-fun step (Int) Int)"), None,
                 "(<= (- (step (+ x {value})) x) 10)"),
    "QF_AX": ("array", ("(declare-sort Axis 0)", "(declare-const origin Axis)",
                       "(declare-const grid (Array Axis (Array Axis Axis)))"), "Axis",
              "(= (select (select (store grid {symbol} ((as const (Array Axis Axis)) origin)) "
              "{symbol}) origin) origin)"),
    "QF_ABV": ("array", (f"(declare-const grid (Array {BV} (Array {BV} {BV})))",), None,
               f"(= (select (select (store grid {{bv}} ((as const (Array {BV} {BV})) {{bv}})) "
               "{bv}) #x00000000) {bv})"),
    "QF_AUFLIA": ("array", ("(declare-const grid (Array Int (Array Bool Int)))",
                           "(declare-fun index (Int) Int)"), None,
                  "(= (select (select (store grid (index {value}) ((as const (Array Bool Int)) "
                  "{value})) (index {value})) true) {value})"),
    "QF_UF": ("uf", ("(declare-sort Wrap 1)", "(declare-const origin (Wrap Bool))",
                    "(declare-fun merge ((Wrap Bool) (Wrap Bool)) (Wrap Bool))"), "(Wrap Bool)",
              "(= (merge {symbol} {symbol}) (merge {symbol} origin))"),
    "QF_DT": ("dt", (BOX, LIST, "(declare-const packet (Box Bool))"), "Bool", DT_TERM),
    "QF_UFDT": ("dt", (BOX, LIST, "(declare-const packet (Box Bool))",
                      "(declare-fun accepts ((Box Bool) Bool) Bool)"), "Bool",
                f"(accepts packet {DT_TERM})"),
    "QF_UFBVDT": ("dt", (BOX, LIST, "(declare-const packet (Box Bool))",
                         f"(declare-datatype Record ((rec (bits {BV}))))",
                         "(declare-fun accepts (Record Bool) Bool)"), "Bool",
                  f"(accepts (rec (bvadd (bits (rec #x01020304)) {{bv}})) {DT_TERM})"),
    "QF_AUFBVFP": ("mixed", (f"(declare-const table (Array {BV} {FP}))",
                            f"(declare-fun tune ({FP}) {FP})"), None,
                   f"(fp.lt (select (store table {{bv}} (tune {FP_RATIONAL})) "
                   "(bvadd {bv} #x00000001)) (_ +oo 8 24))"),
    "QF_UFFPDTNIRA": ("mixed", (f"(declare-datatype Record ((rec (reading {FP}) (count Int))))",
                               "(declare-fun accepts (Record) Bool)"), None,
                      f"(accepts (rec {FP_RATIONAL} (* {{value}} {{value}})))"),
    "ALL": ("mixed", (f"(declare-const grid (Array {BV} (Array Bool {FP})))",
                      f"(declare-datatype Record ((rec (reading {FP}) (count Int))))",
                      "(declare-fun accepts (Record) Bool)"), None,
            f"(accepts (rec (select (select (store grid {{bv}} ((as const (Array Bool {FP})) "
            f"{FP_RATIONAL})) {{bv}}) true) (ubv_to_int ((_ int_to_bv 32) {{value}}))))"),
}


class Fixture:
    """Write chunks while computing the digest and exact command spans."""

    def __init__(self, stream, newline="\n"):
        self.stream = stream
        self.newline = newline
        self.digest = hashlib.sha256()
        self.size = 0
        self.longest = 0
        self.counts = {"commands": 0, "assertions": 0, "definitions": 0}

    def write(self, chunk):
        data = chunk.encode("utf-8")
        self.stream.write(data)
        self.digest.update(data)
        self.size += len(data)

    def command(self, text, *chunks):
        start = self.size
        self.write(text)
        for parts in chunks:
            for chunk in parts:
                self.write(chunk)
        self.longest = max(self.longest, self.size - start)
        self.counts["commands"] += 1
        if text.startswith("(assert "):
            self.counts["assertions"] += 1
        if text.startswith(("(define-fun ", "(define-const ")):
            self.counts["definitions"] += 1
        self.write(self.newline)

    def metadata(self):
        return {
            "bytes": self.size,
            "sha256": self.digest.hexdigest(),
            "max_command_bytes": self.longest,
            **{f"expected_{key}": value for key, value in self.counts.items()},
        }


def theory_workload(writer, logic, count, rng):
    _, declarations, sort, term = THEORIES[logic]
    for command in declarations:
        writer.command(command)
    if sort is not None:
        for index in range(count):
            writer.command(f"(declare-const operand{index} {sort})")
    start = rng.randrange(10_000, 1_000_000)
    writer.command(
        "(assert (and",
        ("\n " + term.format(value=value, large=(1 << 80) + value,
                              bv=f"#x{value:08x}", symbol=f"operand{index}")
         for index, value in enumerate(range(start, start + count))),
        ("))",),
    )
    return {"operands": count, "distinct_operand_declarations": count if sort else 0}


def bool_declarations(writer, count):
    for index in range(count):
        writer.command(f"(declare-const p{index} Bool)")


def balanced_tree(start, stop):
    if stop - start == 1:
        yield f"p{start}"
    else:
        middle = (start + stop) // 2
        yield "(xor "
        yield from balanced_tree(start, middle)
        yield " "
        yield from balanced_tree(middle, stop)
        yield ")"


def boolean_shape(writer, shape, variant, count):
    if shape == "deep_chain":
        bool_declarations(writer, 1)
        writer.command("(assert ", repeat("(not ", count), ("p0",), repeat(")", count + 1))
        return {"depth_levels": count}
    if shape == "skewed_vs_balanced":
        bool_declarations(writer, count)
        # Left spine and balanced tree visit the same leaves in the same order.
        if variant == "skewed":
            writer.command("(assert ", repeat("(xor ", count - 1), ("p0",),
                           (f" p{index})" for index in range(1, count)), (")",))
        else:
            writer.command("(assert ", balanced_tree(0, count), (")",))
        return {"leaves": count, "binary_operations": count - 1}
    if shape == "wide_flat":
        bool_declarations(writer, count)
        writer.command("(assert (and", (f" p{index}" for index in range(count)), ("))",))
        return {"operands": count}
    if shape == "diamond_dag":
        bool_declarations(writer, 2)
        writer.command("(assert (let ((v0 p0)) ",
                       (f"(let ((v{i + 1} (xor (and v{i} p0) (or v{i} p1)))) "
                        for i in range(count)),
                       (f"v{count}",), repeat(")", count + 2))
        return {"diamond_levels": count}
    bool_declarations(writer, count + 1)
    writer.command("(assert (let ((outer p0) (current p0)) ",
                   (f"(let ((current (ite outer current p{i})) (previous current)) "
                    for i in range(1, count + 1)),
                   ("(and current previous outer outer)",), repeat(")", count + 2))
    return {"declarations": count + 1, "shadow_levels": count, "bindings_per_level": 2}


def sharing_workload(writer, variant, count, base):
    writer.command("(declare-const pivot Int)")
    body = "(and" + "".join(f" (<= (+ ARG {base + i}) 0)" for i in range(8)) + ")"
    closed = body.replace("ARG", "pivot")
    if variant == "spelled":
        writer.command("(assert (or", repeat(" " + closed, count), ("))",))
    elif variant == "let":
        writer.command(f"(assert (let ((shared {closed})) (or",
                       repeat(" shared", count), (")))",))
    elif variant == "named":
        writer.command(f"(assert (or (! {closed} :named shared)",
                       repeat(" shared", count - 1), ("))",))
    else:
        formal = body.replace("ARG", "arg")
        writer.command(f"(define-fun shared ((arg Int)) Bool {formal})")
        writer.command("(assert (or", repeat(" (shared pivot)", count), ("))",))
    return {"references": count, "comparisons_in_body": 8}


def partition_workload(writer, variant, units, base):
    comparisons = {"smoke": 2, "light": 4, "medium": 24, "heavy": 12}[units]
    count = SCALES[units]
    writer.command("(declare-const pivot Int)")
    writer.command("(declare-const cutoff Int)")

    def term(index):
        start = base + index * comparisons
        body = "".join(f" (<= (+ pivot {value}) cutoff)"
                       for value in range(start, start + comparisons))
        return f"(and{body})"

    if variant == "packed":
        writer.command("(assert (and", ("\n " + term(i) for i in range(count)), ("))",))
    else:
        for index in range(count):
            writer.command(f"(assert {term(index)})")
    return {"terms": count, "comparisons_per_term": comparisons}


def payload_workload(writer, variant, count, rng):
    digits = max(32, count)
    metadata_bytes = max(8193, 16 * count)
    prefix = '(set-info :source "'
    quote_data = 'input;""'
    literal_prefix = "(assert (= payload #x"
    nibble = rng.choice("123456789abcdef")
    writer.command(prefix, (quote_data,), _payload("m", metadata_bytes - len(quote_data)), ('")',))
    writer.command(f"(declare-const payload (_ BitVec {digits * 4}))")
    writer.command(literal_prefix, _payload(nibble, digits), ("))",))
    return {"hex_digits": digits, "metadata_bytes": metadata_bytes,
            "irregular_utf8_offset": 38 + (variant == "shifted"),
            "reader_4k_utf8_offset": 4095 + (variant == "shifted"),
            "reader_64k_cr_offset": 65535 + (variant == "shifted")}


def _payload(character, length):
    # At most one block is materialized, even for the largest quoted token.
    block = 64 * 1024
    full, tail = divmod(length, block)
    yield from repeat(character * min(length, block), full)
    yield character * tail


def payload_trivia(writer, shifted):
    for offset, ending in ((38, "é\r\n"), (4095, "é\r\n"), (65535, "\r\n")):
        writer.write(";")
        writer.write(" " * (offset + shifted - writer.size))
        writer.write(ending)


SHAPES = (
    ("QF_SAT", "deep_chain", ("unary",)),
    ("QF_SAT", "skewed_vs_balanced", ("skewed", "balanced")),
    ("QF_SAT", "wide_flat", ("distinct",)),
    ("QF_SAT", "diamond_dag", ("let",)),
    ("QF_LIA", "sharing_mechanisms", ("spelled", "let", "named", "define_fun")),
    ("QF_SAT", "shadowing_symtab", ("parallel_let",)),
    ("QF_LIA", "command_partition", ("packed", "split")),
    ("QF_BV", "payload_trivia_boundary", ("split", "shifted")),
)


def generate_case(output, tier, seed, logic, shape, variant):
    case_id = f"{tier}-{logic.lower()}-{shape}-{variant}"
    path = Path("inputs") / f"{case_id}.smt2"
    size = SCALES[tier]
    # Paired cases share the same generated operands and differ only in the
    # representation specified by their variant.
    rng = random.Random(f"{seed}/{logic}/{shape}")
    with (output / path).open("xb") as stream:
        is_payload = shape == "payload_trivia_boundary"
        writer = Fixture(stream, "\r\n" if is_payload else "\n")
        if is_payload:
            payload_trivia(writer, variant == "shifted")
        writer.command(f"(set-logic {logic})")
        if variant == "theory":
            params = theory_workload(writer, logic, size, rng)
        elif is_payload:
            params = payload_workload(writer, variant, size, rng)
        elif shape in ("sharing_mechanisms", "command_partition"):
            base = rng.randrange(10**13, 10**14)
            if shape == "sharing_mechanisms":
                params = sharing_workload(writer, variant, size, base)
            else:
                params = partition_workload(writer, variant, tier, base)
        else:
            params = boolean_shape(writer, shape, variant, size)
        writer.command("(check-sat)")
    return {
        "case": case_id, "logic": logic, "theory": THEORIES[logic][0],
        "shape": shape, "variant": variant, "params": {"tier": tier, "primary": size, **params},
        "relative_path": path.as_posix(), **writer.metadata(),
    }


def generate_suite(output_dir: Path, *, tier: str = "smoke", seed: int = 20261119,
                   jobs: int = 8) -> dict:
    output = outside_dataset(output_dir)
    if tier not in (*SCALES, "all"):
        raise ValueError(f"Unknown tier {tier!r}")
    if type(seed) is not int or type(jobs) is not int or jobs <= 0:
        raise ValueError("Specify an integer seed and a positive job count")
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise ValueError(f"Output directory already contains files: {output}")
    (output / "inputs").mkdir()
    workloads = [(logic, "wide_flat", "theory") for logic in THEORIES]
    workloads.extend((logic, shape, variant)
                     for logic, shape, variants in SHAPES for variant in variants)
    specs = [(scale, logic, shape, variant)
             for scale in (SCALES if tier == "all" else (tier,))
             for logic, shape, variant in workloads]

    def write_case(spec):
        scale, logic, shape, variant = spec
        return generate_case(output, scale, seed, logic, shape, variant)

    with ThreadPoolExecutor(max_workers=min(jobs, len(specs))) as workers:
        cases = list(workers.map(write_case, specs))

    manifest = {"schema": 1, "tier": tier, "seed": seed, "cases": cases}
    pending = output / ".manifest.tmp"
    for name in ("manifest.json", "generated_manifest.json"):
        write_json(pending, manifest)
        pending.replace(output / name)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tier", choices=(*SCALES, "all"), default="smoke")
    parser.add_argument("--seed", type=int, default=20261119)
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    try:
        generate_suite(args.output, tier=args.tier, seed=args.seed, jobs=args.jobs)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
