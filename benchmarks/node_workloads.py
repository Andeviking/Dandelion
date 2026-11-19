"""Map high-level cvc5 operations and schedule Dandelion Node owners."""

from collections import Counter, defaultdict
from dataclasses import dataclass
from fractions import Fraction
import sys

from cvc5_trace import TraceError, ids

if hasattr(sys, "set_int_max_str_digits"):
    # The input format supports exact numerals. The capture has a byte limit.
    sys.set_int_max_str_digits(0)

CORE = {
    "NOT": "kNot", "AND": "kAnd", "OR": "kOr", "XOR": "kXor",
    "IMPLIES": "kImplies", "EQUAL": "kEqual", "DISTINCT": "kDistinct",
    "ITE": "kIte",
}
INT = {
    "NEG": "kIntNeg", "ADD": "kIntAdd", "SUB": "kIntSub", "MULT": "kIntMul",
    "NONLINEAR_MULT": "kIntMul", "INTS_DIVISION": "kIntDiv",
    "INTS_MODULUS": "kIntMod", "ABS": "kIntAbs",
    "LT": "kIntLt", "LEQ": "kIntLe", "GT": "kIntGt", "GEQ": "kIntGe",
    "DIVISIBLE": "kIntDivisible",
}
BV = {"BITVECTOR_" + name.upper(): "kBv" + name for name in (
    "Not", "Neg", "And", "Or", "Xor", "Add", "Nand", "Nor", "Xnor",
    "Sub", "Udiv", "Urem", "Sdiv", "Srem", "Smod", "Shl", "Lshr", "Ashr",
    "Comp", "Ult", "Ule", "Ugt", "Uge", "Slt", "Sle", "Sgt", "Sge", "Nego",
    "Uaddo", "Saddo", "Umulo", "Smulo", "Usubo", "Ssubo", "Sdivo", "Concat",
    "Extract", "Repeat",
)}
BV.update({
    "BITVECTOR_MULT": "kBvMul", "BITVECTOR_ZERO_EXTEND": "kBvZeroExtend",
    "BITVECTOR_SIGN_EXTEND": "kBvSignExtend", "BITVECTOR_ROTATE_LEFT": "kBvRotateLeft",
    "BITVECTOR_ROTATE_RIGHT": "kBvRotateRight", "BITVECTOR_UBV_TO_INT": "kUbvToInt",
    "BITVECTOR_SBV_TO_INT": "kSbvToInt", "INT_TO_BITVECTOR": "kIntToBv",
})
FP = {"FLOATINGPOINT_" + name.upper(): "kFp" + name for name in (
    "Abs", "Neg", "Add", "Sub", "Div", "Fma", "Sqrt", "Rem", "Min",
    "Max", "Eq", "Lt", "Leq", "Gt", "Geq",
)}
FP.update({
    "FLOATINGPOINT_FP": "kFpConstruct", "FLOATINGPOINT_MULT": "kFpMul",
    "FLOATINGPOINT_RTI": "kFpRoundToIntegral",
    "FLOATINGPOINT_IS_NORMAL": "kFpIsNormal",
    "FLOATINGPOINT_IS_SUBNORMAL": "kFpIsSubnormal",
    "FLOATINGPOINT_IS_ZERO": "kFpIsZero", "FLOATINGPOINT_IS_INF": "kFpIsInfinite",
    "FLOATINGPOINT_IS_NAN": "kFpIsNaN", "FLOATINGPOINT_IS_NEG": "kFpIsNegative",
    "FLOATINGPOINT_IS_POS": "kFpIsPositive",
    "FLOATINGPOINT_TO_FP_FROM_IEEE_BV": "kFpFromBits",
    "FLOATINGPOINT_TO_FP_FROM_FP": "kFpConvert",
    "FLOATINGPOINT_TO_FP_FROM_REAL": "kFpFromRational",
    "FLOATINGPOINT_TO_FP_FROM_SBV": "kFpFromSbv",
    "FLOATINGPOINT_TO_FP_FROM_UBV": "kFpFromUbv",
    "FLOATINGPOINT_TO_SBV": "kFpToSbv", "FLOATINGPOINT_TO_UBV": "kFpToUbv",
})
ARRAY = {"SELECT": "kArraySelect", "STORE": "kArrayStore", "STORE_ALL": "kArrayConst"}
OPERATORS = {**CORE, **INT, **BV, **FP, **ARRAY}
BOOL_RESULTS = (
    set(CORE.values()) - {"kIte"}
    | {"kIntLt", "kIntLe", "kIntGt", "kIntGe", "kIntDivisible"}
    | {"kBv" + suffix for suffix in (
        "Ult", "Ule", "Ugt", "Uge", "Slt", "Sle", "Sgt", "Sge", "Nego",
        "Uaddo", "Saddo", "Umulo", "Smulo", "Usubo", "Ssubo", "Sdivo")}
    | {"kFpEq", "kFpLt", "kFpLeq", "kFpGt", "kFpGeq"}
    | {kind for kind in FP.values() if kind.startswith("kFpIs")}
)
ATOMIC = {"BOOLEAN_TYPE": "bool", "INTEGER_TYPE": "int", "ROUNDINGMODE_TYPE": "rm"}
VARIABLES = {"VARIABLE", "SKOLEM", "DUMMY_SKOLEM"}
DT_APPLY = {"APPLY_CONSTRUCTOR": "constructor", "APPLY_SELECTOR": "selector",
            "APPLY_TESTER": "tester"}
LITERALS = {
    "CONST_BOOLEAN": ("kBoolValue", ("bool",), 0),
    "CONST_INTEGER": ("kIntValue", ("int",), 0),
    "CONST_ROUNDINGMODE": ("kRoundingModeValue", ("rm",), 0),
    "CONST_BITVECTOR": ("kBvValue", ("bv",), 1),
    "CONST_FLOATINGPOINT": ("kFpValue", ("fp",), 2),
}


class Unsupported(ValueError):
    pass


class MetadataOnly(Unsupported):
    pass


@dataclass(frozen=True)
class Request:
    sort: int
    kind: str
    children: tuple
    payload: tuple = ()


def line(*fields):
    return " ".join(map(str, fields)) + "\n"


class Mapper:
    def __init__(self, trace):
        self.trace = trace
        self.blocks = []
        self.metadata = {}
        self.sort_shapes = {}
        self.sort_ids = {}
        self.raw_sorts = {}
        self.preferred = {}
        self.next_sort = max(trace.definitions, default=0) + 2
        self.next_instance = 1
        self.instance_ids = {}
        self.group_of = {sort: group for group, members in trace.groups.items()
                         for sort in members}
        self.groups_in_progress = set()
        self.datatype_ops = {}
        self.requests = {}
        self.excluded = {}
        self.rationals = {}
        for node, definition in trace.definitions.items():
            if definition.category == "s":
                if definition.kind in ATOMIC:
                    self.preferred.setdefault((ATOMIC[definition.kind],), node + 1)
                elif definition.kind in ("BITVECTOR_TYPE", "FLOATINGPOINT_TYPE"):
                    kind = "bv" if definition.kind == "BITVECTOR_TYPE" else "fp"
                    self.preferred.setdefault((kind, *ids(definition.payload)), node + 1)
            elif definition.kind in ("CONST_INTEGER", "CONST_RATIONAL"):
                self.rationals[node] = Fraction(definition.payload[0])
            elif definition.kind in ("NEG", "DIVISION", "TO_REAL"):
                values = [self.rationals.get(child) for child in definition.children]
                if all(value is not None for value in values):
                    if definition.kind == "NEG" and len(values) == 1:
                        self.rationals[node] = -values[0]
                    elif definition.kind == "TO_REAL" and len(values) == 1:
                        self.rationals[node] = values[0]
                    elif definition.kind == "DIVISION" and len(values) == 2 and values[1]:
                        self.rationals[node] = values[0] / values[1]
        for sort, constructors in trace.constructors.items():
            for ctor, tester, fields in constructors:
                self.datatype_ops[ctor] = (sort, "constructor", None)
                self.datatype_ops[tester] = (sort, "tester", None)
                for selector, field_sort in fields:
                    self.datatype_ops[selector] = (sort, "selector", field_sort)

    def add(self, keys, depends, *lines):
        index = len(self.blocks)
        self.blocks.append((tuple(depends), lines))
        for key in keys:
            if key in self.metadata:
                raise TraceError(f"duplicate metadata key {key}")
            self.metadata[key] = index

    def sort(self, shape, raw=None):
        if shape in self.sort_ids:
            return self.sort_ids[shape]
        numeric = raw + 1 if raw is not None else self.preferred.get(shape)
        if numeric is None:
            numeric = self.next_sort
            self.next_sort += 1
        kind, *args = shape
        depends = []
        if kind == "array":
            depends = [("sort", arg) for arg in args]
        elif kind == "apply":
            depends = [("symbol", args[0]), *(("sort", arg) for arg in args[1:])]
        self.add([("sort", numeric)], depends, line("sort", numeric, *shape))
        self.sort_ids[shape] = numeric
        self.sort_shapes[numeric] = shape
        return numeric

    def name_sort(self, raw):
        symbol = raw + 1
        if ("symbol", symbol) not in self.metadata:
            if self.trace.definitions[raw].kind != "SORT_TYPE":
                raise Unsupported("unrecognized uninterpreted sort constructor")
            self.add([("symbol", symbol)], [],
                     line("symbol", symbol, self.trace.arities.get(raw, 0)))
        return symbol

    def use_sort(self, raw):
        if raw in self.raw_sorts:
            return self.raw_sorts[raw]
        definition = self.trace.definitions[raw]
        kind, children = definition.kind, definition.children
        if kind in ATOMIC:
            shape = (ATOMIC[kind],)
        elif kind == "BITVECTOR_TYPE":
            shape = ("bv", *ids(definition.payload))
        elif kind == "FLOATINGPOINT_TYPE":
            shape = ("fp", *ids(definition.payload))
        elif kind == "ARRAY_TYPE":
            shape = ("array", *(self.use_sort(child) for child in children))
        elif kind in ("SORT_TYPE", "INSTANTIATED_SORT_TYPE"):
            symbol, args = (raw, ()) if kind == "SORT_TYPE" else (children[0], children[1:])
            if len(args) != self.trace.arities.get(symbol, 0):
                raise Unsupported("unsaturated uninterpreted sort")
            shape = ("apply", self.name_sort(symbol), *(self.use_sort(arg) for arg in args))
        elif kind in ("DATATYPE_TYPE", "PARAMETRIC_DATATYPE"):
            base, args = (raw, ()) if kind == "DATATYPE_TYPE" else (children[0], children[1:])
            self.use_group(base)
            if len(args) != len(self.trace.groups[self.group_of[base]][base]):
                raise Unsupported("unsaturated datatype sort")
            shape = ("apply", base + 1, *(self.use_sort(arg) for arg in args))
        else:
            raise Unsupported("sort " + kind)
        self.raw_sorts[raw] = self.sort(shape, raw)
        return self.raw_sorts[raw]

    def use_group(self, base):
        if ("symbol", base + 1) in self.metadata:
            return
        if base not in self.group_of:
            raise Unsupported("datatype group unavailable")
        group = self.group_of[base]
        if group in self.groups_in_progress:
            raise Unsupported("cycle across datatype groups")
        members = self.trace.groups[group]
        slots = {raw: index for index, raw in enumerate(members)}
        keys = [("symbol", raw + 1) for raw in members]
        depends = set()
        records = [line("group", *(arg for raw, params in members.items()
                                   for arg in (raw + 1, len(params))))]
        recipes = {}

        def recipe(raw, slot, nested=False):
            definition = self.trace.definitions[raw]
            params = members[member_order[slot]]
            kind, children = definition.kind, definition.children
            if raw in params:
                shape = ("param", slot, params.index(raw))
            elif raw in slots or (kind == "PARAMETRIC_DATATYPE" and children[0] in slots):
                if nested:
                    raise Unsupported("nested recursive datatype field")
                base, args = (raw, ()) if raw in slots else (children[0], children[1:])
                shape = ("recursive", slots[base],
                         *(recipe(arg, slot, True) for arg in args))
            elif kind == "ARRAY_TYPE":
                shape = ("array", *(recipe(arg, slot, True) for arg in children))
            elif kind in ("INSTANTIATED_SORT_TYPE", "PARAMETRIC_DATATYPE"):
                if kind == "INSTANTIATED_SORT_TYPE":
                    symbol = self.name_sort(children[0])
                else:
                    self.use_group(children[0])
                    symbol = children[0] + 1
                depends.add(("symbol", symbol))
                shape = ("apply", symbol, *(recipe(arg, slot, True) for arg in children[1:]))
            else:
                concrete = self.use_sort(raw)
                depends.add(("sort", concrete))
                shape = ("concrete", concrete)
            if shape not in recipes:
                recipes[shape] = len(recipes) + 1
                records.append(line("recipe", recipes[shape], *shape))
            return recipes[shape]

        member_order = tuple(members)
        self.groups_in_progress.add(group)
        try:
            for raw in members:
                if not self.trace.constructors[raw]:
                    raise Unsupported("datatype constructors unavailable")
                slot = slots[raw]
                for ctor, tester, fields in self.trace.constructors[raw]:
                    keys.extend([("decl", ctor + 1), ("decl", tester + 1)])
                    fields_with_recipes = []
                    for selector, field_sort in fields:
                        keys.append(("decl", selector + 1))
                        fields_with_recipes.extend((selector + 1, recipe(field_sort, slot)))
                    records.append(line("ctor", slot, ctor + 1, tester + 1, *fields_with_recipes))
        finally:
            self.groups_in_progress.remove(group)
        self.add(keys, sorted(depends), *records, "commit\n")

    def function(self, raw, function_use):
        if raw not in self.trace.types:
            raise Unsupported("declaration type unavailable")
        raw_type = self.trace.types[raw]
        definition = self.trace.definitions[raw_type]
        if definition.kind == "FUNCTION_TYPE":
            if not function_use:
                raise MetadataOnly("function declaration")
            *domain, result = definition.children
        else:
            if function_use:
                raise Unsupported("UF operator lacks a function signature")
            domain, result = (), raw_type
        domain = tuple(self.use_sort(sort) for sort in domain)
        result = self.use_sort(result)
        key = ("decl", raw + 1)
        if key not in self.metadata:
            kind = "function" if self.trace.definitions[raw].kind == "VARIABLE" else "internal"
            self.add([key], [("sort", sort) for sort in (*domain, result)],
                     line("decl", raw + 1, kind, result, *domain))
        return result

    def instance(self, operator, args):
        key = (operator, args)
        if key not in self.instance_ids:
            instance = self.next_instance
            self.next_instance += 1
            self.add([("inst", instance)],
                     [("decl", operator + 1), *(("sort", arg) for arg in args)],
                     line("inst", instance, operator + 1, *args))
            self.instance_ids[key] = instance
        return self.instance_ids[key]

    def datatype_application(self, raw, operator, role, child_sorts):
        definition = self.trace.definitions[operator]
        if definition.kind == "APPLY_TYPE_ASCRIPTION" and len(definition.children) == 1:
            operator = definition.children[0]
        entry = self.datatype_ops.get(operator)
        if entry is None or entry[1] != role:
            raise Unsupported("datatype operation lacks constructor-specific identity")
        base, _, field_sort = entry
        if role == "constructor":
            datatype_sort = self.use_sort(self.trace.types.get(raw, base))
        else:
            if len(child_sorts) != 1:
                raise Unsupported("unknown datatype operand shape")
            datatype_sort = child_sorts[0]
        shape = self.sort_shapes[datatype_sort]
        if shape[0] != "apply" or shape[1] != base + 1:
            raise Unsupported("datatype instance differs from declaration")
        if role == "constructor":
            result = datatype_sort
        elif role == "tester":
            result = self.sort(("bool",))
        else:
            raw_result = self.trace.types.get(raw)
            if raw_result is None:
                parameters = self.trace.groups[self.group_of[base]][base]
                result = self.substituted_sort(field_sort, dict(zip(parameters, shape[2:])))
            else:
                result = self.use_sort(raw_result)
        return result, self.instance(operator, shape[2:])

    def substituted_sort(self, raw, parameters):
        if not parameters:
            return self.use_sort(raw)
        if raw in parameters:
            return parameters[raw]
        definition = self.trace.definitions[raw]
        kind, children = definition.kind, definition.children
        if kind == "ARRAY_TYPE":
            return self.sort(("array", *(self.substituted_sort(child, parameters)
                                         for child in children)))
        if kind in ("PARAMETRIC_DATATYPE", "INSTANTIATED_SORT_TYPE"):
            base, *args = children
            if kind == "PARAMETRIC_DATATYPE":
                self.use_group(base)
                symbol = base + 1
            else:
                symbol = self.name_sort(base)
            return self.sort(("apply", symbol, *(self.substituted_sort(arg, parameters)
                                                 for arg in args)))
        return self.use_sort(raw)

    def expression(self, raw, definition):
        source, children = definition.kind, definition.children
        operator, payload = definition.operator, definition.payload
        if definition.category == "s":
            raise MetadataOnly("sort metadata")
        if raw in self.datatype_ops:
            raise MetadataOnly("datatype declaration")
        if source == "BUILTIN" or source.endswith("_OP") or source == "APPLY_TYPE_ASCRIPTION":
            raise MetadataOnly("operator metadata")
        if source in VARIABLES:
            result = self.function(raw, False)
            return Request(result, "kApply", (), (self.instance(raw, ()),))
        if source in LITERALS:
            kind, sort, count = LITERALS[source]
            if len(payload) != count + 1 or children or operator is not None:
                raise TraceError("invalid literal payload")
            return Request(self.sort((*sort, *ids(payload[:-1]))), kind, (), payload[-1:])
        if source == "POW":
            raise Unsupported("reference exponentiation has a different domain")
        if source not in OPERATORS and source != "APPLY_UF" and source not in DT_APPLY:
            raise Unsupported("operator " + source)

        indices = ()
        if source == "FLOATINGPOINT_TO_FP_FROM_REAL":
            if len(children) != 2 or children[1] not in self.rationals:
                raise Unsupported("FP conversion has no closed rational operand")
            rational = self.rationals[children[1]]
            children = children[:1]
        else:
            rational = None
        if not all(child in self.requests for child in children):
            raise Unsupported("unsupported operand")
        child_sorts = tuple(self.requests[child].sort for child in children)
        if source == "APPLY_UF":
            if operator is None or self.trace.definitions[operator].kind not in VARIABLES:
                raise Unsupported("UF operator lacks a stable declaration")
            result = self.function(operator, True)
            return Request(result, "kApply", children, (self.instance(operator, ()),))
        if source in DT_APPLY:
            if operator is None:
                raise TraceError("missing datatype operator")
            result, instance = self.datatype_application(raw, operator, DT_APPLY[source], child_sorts)
            return Request(result, "kApply", children, (instance,))

        kind = OPERATORS[source]
        if operator is not None:
            parameter = self.trace.definitions[operator]
            if parameter.kind != source + "_OP" or not parameter.payload:
                raise Unsupported("unrecognized index payload")
            indices = parameter.payload
        result = self.builtin_sort(source, kind, child_sorts, indices, payload)
        if rational is not None:
            indices = (*indices, rational.numerator, rational.denominator)
        return Request(result, kind, children, indices)

    def builtin_sort(self, source, kind, child_sorts, indices, payload):
        shapes = [self.sort_shapes[sort] for sort in child_sorts]
        if source in INT and any(shape != ("int",) for shape in shapes):
            raise Unsupported("arithmetic request has a non-Int operand")
        if kind in BOOL_RESULTS:
            return self.sort(("bool",))
        if kind == "kIte":
            return child_sorts[1]
        if source in INT or kind in ("kUbvToInt", "kSbvToInt"):
            return self.sort(("int",))
        if kind == "kArrayConst":
            if len(payload) != 1:
                raise TraceError("missing constant array qualifier")
            return self.use_sort(ids(payload)[0])
        if kind == "kArrayStore":
            return child_sorts[0]
        if kind == "kArraySelect":
            return shapes[0][2]
        if kind == "kBvConcat":
            if len(shapes) != 2:
                raise Unsupported("n-ary concat has no single matching factory request")
            return self.sort(("bv", shapes[0][1] + shapes[1][1]))
        if kind == "kBvComp":
            return self.sort(("bv", 1))
        if kind == "kBvExtract":
            high, low = ids(indices)
            return self.sort(("bv", high - low + 1))
        if kind == "kBvRepeat":
            return self.sort(("bv", shapes[0][1] * ids(indices)[0]))
        if kind in ("kBvZeroExtend", "kBvSignExtend"):
            return self.sort(("bv", shapes[0][1] + ids(indices)[0]))
        if kind in ("kIntToBv", "kFpToUbv", "kFpToSbv"):
            return self.sort(("bv", *ids(indices)))
        if kind.startswith("kBv"):
            return child_sorts[0]
        if kind == "kFpConstruct":
            return self.sort(("fp", shapes[1][1], shapes[2][1] + 1))
        if kind.startswith("kFpFrom") or kind == "kFpConvert":
            return self.sort(("fp", *ids(indices)))
        for sort, shape in zip(child_sorts, shapes):
            if shape[0] == "fp":
                return sort
        raise Unsupported("operation result sort unavailable")

    def map_requests(self):
        for raw, definition in self.trace.definitions.items():
            try:
                request = self.expression(raw, definition)
                raw_result = self.trace.types.get(raw)
                if raw_result is not None and request.sort != self.use_sort(raw_result):
                    raise Unsupported("captured and mapped result sorts differ")
                self.requests[raw] = request
            except MetadataOnly as error:
                self.excluded[raw] = ("metadata", str(error))
            except Unsupported as error:
                self.excluded[raw] = ("excluded", str(error))
            except (ValueError, IndexError, KeyError) as error:
                raise TraceError(f"malformed {definition.kind} definition {raw}: {error}") from error

    def write_metadata(self, stream, raw_requests):
        required = []
        for raw in raw_requests:
            request = self.requests[raw]
            required.append(("sort", request.sort))
            if request.kind == "kApply":
                required.append(("inst", request.payload[0]))
        seen = set()
        while required:
            index = self.metadata[required.pop()]
            if index not in seen:
                seen.add(index)
                required.extend(self.blocks[index][0])
        for index in sorted(seen):
            stream.writelines(self.blocks[index][1])


def schedule(path, mapper, events, roots, graph):
    """Reuse empty slots and drop temporary operands after their final use."""
    uses = Counter(child for action, _, _, operands in events if action != "drop"
                   for child in operands)
    slots = {}
    free = []
    next_slot = 0
    peak = 0
    make_ids = {raw for action, _, raw, _ in events if action == "make"}
    operations = 0
    with path.open("w", encoding="ascii") as out:
        out.write(line("dandelion-node", 1, "graph" if graph else "mixed"))
        mapper.write_metadata(out, make_ids)

        def drop(version):
            nonlocal operations
            slot = slots.pop(version)
            out.write(line("drop", slot))
            free.append(slot)
            operations += 1

        for action, version, raw, operands in events:
            if action == "drop":
                drop(version)
                continue
            if free:
                slot = free.pop()
            else:
                slot = next_slot
                next_slot += 1
            slots[version] = slot
            peak = max(peak, len(slots))
            if action == "make":
                request = mapper.requests[raw]
                out.write(line("make", slot, request.sort, request.kind, len(operands),
                               *(slots[operand] for operand in operands), *request.payload))
                if not uses[version]:
                    drop(version)
            else:
                out.write(line("hold", slot, slots[operands[0]]))
            operations += 1
            for operand in operands:
                uses[operand] -= 1
                if uses[operand] == 0:
                    drop(operand)
        if roots:
            out.write(line("roots", *(slots[version] for version in roots)))
        out.write("end\n")
    return {"peak_slots": peak, "operations": operations,
            "requests": sum(action == "make" for action, *_ in events)}


def snapshot(output, name, mapper, roots):
    pending = list(roots)
    reachable = set()
    blockers = Counter()
    while pending:
        raw = pending.pop()
        if raw in reachable:
            continue
        reachable.add(raw)
        if raw in mapper.requests:
            pending.extend(mapper.requests[raw].children)
        else:
            definition = mapper.trace.definitions[raw]
            blockers[f"{definition.kind}: {mapper.excluded[raw][1]}"] += 1
            pending.extend(definition.children)
    if blockers:
        most_common = "; ".join(f"{reason} ({count})" for reason, count in
                                blockers.most_common(3))
        return {"status": "unsupported", "reason": most_common,
                "blocking_definitions": dict(blockers), "roots": len(roots)}

    # A captured definition can only refer to earlier definitions.
    order = [raw for raw in mapper.requests if raw in reachable]
    events = [("make", raw, raw, mapper.requests[raw].children) for raw in order]
    first = max(order) + 1
    root_owners = tuple(range(first, first + len(roots)))
    events.extend(("hold", owner, None, (root,)) for owner, root in zip(root_owners, roots))
    depths = {}
    edges, fanout = 0, 0
    for raw in order:
        children = mapper.requests[raw].children
        depths[raw] = 1 + max((depths[child] for child in children), default=0)
        edges += len(children)
        fanout = max(fanout, len(children))
    filename = name + ".node"
    work = schedule(output / filename, mapper, events, root_owners, True)
    theories = set()
    sorts = set()
    labels = {"bool": "Bool", "int": "Int", "bv": "BV", "fp": "FP", "rm": "FP",
              "array": "Array"}
    for raw in order:
        request = mapper.requests[raw]
        sorts.add(request.sort)
        if request.kind == "kApply":
            instance_index = mapper.metadata[("inst", request.payload[0])]
            decl_dep = mapper.blocks[instance_index][0][0]
            decl_index = mapper.metadata[decl_dep]
            if mapper.blocks[decl_index][1][0].startswith("group"):
                theories.add("DT")
            elif request.children:
                theories.add("UF")
    visited_sorts = set()
    while sorts:
        sort = sorts.pop()
        if sort in visited_sorts:
            continue
        visited_sorts.add(sort)
        kind, *args = mapper.sort_shapes[sort]
        if kind == "apply":
            symbol = mapper.metadata[("symbol", args[0])]
            theories.add("DT" if mapper.blocks[symbol][1][0].startswith("group") else "UF")
            sorts.update(args[1:])
        else:
            theories.add(labels[kind])
            if kind == "array":
                sorts.update(args)
    return dict(work, status="ok", file=filename, roots=len(roots),
                nodes=len(reachable), edges=edges, depth=max(depths.values()),
                max_fanout=fanout, theories=sorted(theories))


def mixed(output, trace, mapper, retain_input_roots):
    events = []
    current = {}
    caches = defaultdict(dict)
    roots = defaultdict(list)
    coverage = defaultdict(lambda: {"mapped_requests": 0, "metadata": Counter(),
                                    "excluded": Counter(), "ownership": Counter()})
    for event, phase, data in trace.events:
        counts = coverage[phase]
        if event in ("new", "fresh", "hit"):
            if data in mapper.requests:
                request = mapper.requests[data]
                version = len(events)
                events.append(("make", version, data,
                               tuple(current[child] for child in request.children)))
                current[data] = version
                counts["mapped_requests"] += 1
            else:
                category, reason = mapper.excluded[data]
                counts[category][reason] += 1
        elif event == "root":
            role, raw = data
            if role == "input" and not retain_input_roots:
                counts["ownership"]["root_observations"] += 1
            elif raw in current:
                version = len(events)
                events.append(("hold", version, None, (current[raw],)))
                roots[role].append(version)
                counts["ownership"]["roots_mapped"] += 1
            else:
                counts["ownership"]["roots_excluded"] += 1
        elif event == "snapshot":
            events.extend(("drop", owner, None, ()) for owner in roots.pop(data, ()))
        elif event == "cache":
            cache, key, value = data
            prior = caches[key].pop(cache, None)
            if prior is not None:
                events.append(("drop", prior, None, ()))
            if value is not None:
                if key in current and value in current:
                    version = len(events)
                    events.append(("hold", version, None, (current[value],)))
                    caches[key][cache] = version
                    counts["ownership"]["cache_values_mapped"] += 1
                else:
                    counts["ownership"]["cache_values_excluded"] += 1
        elif event == "collect":
            current.pop(data, None)
            events.extend(("drop", owner, None, ())
                          for owner in caches.pop(data, {}).values())
    for owners in roots.values():
        events.extend(("drop", owner, None, ()) for owner in owners)
    for slots in caches.values():
        events.extend(("drop", owner, None, ()) for owner in slots.values())
    if not any(action == "make" for action, *_ in events):
        return {"status": "unsupported", "reason": "no mapped construction requests"}, coverage
    filename = "mixed.node"
    work = schedule(output / filename, mapper, events, (), False)
    return dict(work, status="ok", file=filename), coverage


def lookup(output, trace, mapper):
    """Replay the mapped expression hits against their retained recipes."""
    hits = []
    phases = defaultdict(Counter)
    exclusions = Counter()
    for event, phase, raw in trace.events:
        if event != "hit" or trace.definitions[raw].category != "e":
            continue
        counts = phases[phase]
        counts["expression_hits"] += 1
        if raw in mapper.requests:
            hits.append(raw)
            counts["mapped_hits"] += 1
        else:
            category, reason = mapper.excluded[raw]
            counts[f"{category}_hits"] += 1
            exclusions[reason] += 1

    work = {"lookups": len(hits), "phases": dict(phases),
            "exclusions": dict(exclusions)}
    if not hits:
        return dict(work, status="unsupported", reason="no mapped expression lookup hits")

    reachable = set(hits)
    distinct_hits = len(reachable)
    pending = list(reachable)
    while pending:
        for child in mapper.requests[pending.pop()].children:
            if child not in reachable:
                reachable.add(child)
                pending.append(child)
    # Definitions are stored in operand-first order by the trace reader.
    order = (raw for raw in mapper.requests if raw in reachable)
    slots = {raw: slot for slot, raw in enumerate(order)}
    filename = "lookup.node"
    edges = 0
    with (output / filename).open("w", encoding="ascii") as out:
        out.write("dandelion-node 1 lookup\n")
        mapper.write_metadata(out, slots)
        for raw, slot in slots.items():
            recipe = mapper.requests[raw]
            edges += len(recipe.children)
            out.write(line("make", slot, recipe.sort, recipe.kind, len(recipe.children),
                           *(slots[child] for child in recipe.children), *recipe.payload))
        for start in range(0, len(hits), 4096):
            out.write(line("lookup", *(slots[raw] for raw in hits[start:start + 4096])))
        out.write("end\n")
    return dict(work, status="ok", file=filename, requests=len(slots),
                seed_edges=edges, distinct_hit_ids=distinct_hits)


def compile_workloads(trace, output, retain_input_roots=True):
    if not trace.roots.get("input"):
        raise TraceError("capture has no input roots", "empty")
    mapper = Mapper(trace)
    mapper.map_requests()
    snapshots = {name: snapshot(output, name, mapper, roots) for name, roots in
                 trace.roots.items() if roots}
    workload, coverage = mixed(output, trace, mapper, retain_input_roots)
    return {"snapshots": snapshots, "mixed": workload, "mapping": dict(coverage),
            "lookup": lookup(output, trace, mapper)}
