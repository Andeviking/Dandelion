"""Read the bounded trace from the pinned cvc5 adapter."""

from collections import Counter, defaultdict
from dataclasses import dataclass, field


class TraceError(ValueError):
    def __init__(self, message, status="corrupt"):
        super().__init__(message)
        self.status = status


@dataclass
class Definition:
    category: str
    kind: str
    operator: object
    children: tuple
    payload: tuple


@dataclass
class Trace:
    definitions: dict = field(default_factory=dict)
    types: dict = field(default_factory=dict)
    arities: dict = field(default_factory=dict)
    groups: dict = field(default_factory=dict)
    constructors: dict = field(default_factory=lambda: defaultdict(list))
    roots: dict = field(default_factory=lambda: defaultdict(list))
    events: list = field(default_factory=list)
    commands: list = field(default_factory=list)
    count: int = 0


def uint(word):
    if not word.isascii() or not word.isdecimal():
        raise TraceError("expected a nonnegative integer")
    return int(word)


def ids(words):
    return tuple(map(uint, words))


def trace_terminal(path):
    with path.open("rb") as stream:
        stream.seek(max(0, path.stat().st_size - 512))
        last = stream.read().splitlines()
        return last[-1].decode("ascii", errors="replace") if last else ""


def read_trace(path):
    trace = Trace()
    active = set()
    footer = False

    def defined(node):
        if node not in active:
            raise TraceError(f"undefined or collected ID {node}")
        return node

    with path.open(encoding="ascii") as stream:
        if stream.readline() != "dandelion-cvc5 1\n":
            raise TraceError("missing or unsupported cvc5 trace header")
        terminal = trace_terminal(path)
        if terminal.startswith("truncated "):
            raise TraceError("trace " + terminal, "truncated")
        if not terminal.startswith("end "):
            raise TraceError("incomplete trace footer", "incomplete")
        for line_number, line in enumerate(stream, start=2):
            try:
                words = line.split()
                if footer:
                    raise TraceError("trailing records after trace footer")
                if not line.endswith("\n") or len(words) < 2:
                    raise TraceError("incomplete trace record", "incomplete")
                event, phase, *args = words
                if event == "truncated":
                    raise TraceError("trace truncated by " + phase, "truncated")
                if event == "end":
                    if args or uint(phase) != trace.count:
                        raise TraceError("trace footer event count mismatch")
                    footer = True
                    continue
                trace.count += 1
                if event in ("new", "fresh"):
                    category, number, kind, op, count, *rest = args
                    node, count = uint(number), uint(count)
                    if category not in ("s", "e") or node in trace.definitions:
                        raise TraceError("invalid or duplicate definition")
                    if count > len(rest):
                        raise TraceError("missing operands in definition")
                    operator = defined(uint(op)) if op != "-" else None
                    children = tuple(defined(child) for child in ids(rest[:count]))
                    trace.definitions[node] = Definition(
                        category, kind, operator, children, tuple(rest[count:]))
                    active.add(node)
                    trace.events.append((event, phase, node))
                elif event in ("hit", "zero", "collect", "visit"):
                    node, = ids(args)
                    defined(node)
                    trace.events.append((event, phase, node))
                    if event == "collect":
                        active.remove(node)
                elif event == "type":
                    node, sort = map(defined, ids(args))
                    if trace.definitions[sort].category != "s":
                        raise TraceError("type attribute does not refer to a sort")
                    trace.types[node] = sort
                elif event == "arity":
                    sort, arity = ids(args)
                    trace.arities[defined(sort)] = arity
                elif event == "command":
                    name, = args
                    trace.commands.append(name)
                elif event == "snapshot":
                    role, = args
                    if role != "preprocessed":
                        raise TraceError("invalid snapshot role")
                    trace.roots[role].clear()
                    trace.events.append((event, phase, role))
                elif event == "root":
                    role, node_text = args
                    if role not in ("input", "preprocessed"):
                        raise TraceError("invalid root role")
                    node = defined(uint(node_text))
                    trace.roots[role].append(node)
                    trace.events.append((event, phase, (role, node)))
                elif event == "cache":
                    cache, key, value = args
                    trace.events.append((event, phase, (
                        cache, defined(uint(key)),
                        None if value == "-" else defined(uint(value)))))
                elif event == "cache-lookup":
                    cache, key, lookup = args
                    if lookup not in ("hit", "miss"):
                        raise TraceError("invalid rewrite lookup outcome")
                    defined(uint(key))
                    trace.events.append((event, phase, lookup))
                elif event == "datatype":
                    group, sort, *params = ids(args)
                    defined(sort)
                    for param in params:
                        defined(param)
                    trace.groups.setdefault(group, {})[sort] = tuple(params)
                elif event == "constructor":
                    dt, constructor, tester, *fields = ids(args)
                    if len(fields) % 2:
                        raise TraceError("missing datatype field sort")
                    for node in (constructor, tester, *fields):
                        defined(node)
                    trace.constructors[defined(dt)].append(
                        (constructor, tester, tuple(zip(fields[::2], fields[1::2]))))
                else:
                    raise TraceError(f"unknown trace event {event}")
            except (ValueError, IndexError) as error:
                status = getattr(error, "status", "corrupt")
                raise TraceError(f"trace line {line_number}: {error}", status) from error
    if not footer:
        raise TraceError("incomplete trace footer", "incomplete")
    return trace


def summarize(trace):
    """Counts reflect reference events, including those excluded from replay."""
    phases = defaultdict(Counter)
    kinds = defaultdict(Counter)
    sorts = defaultdict(Counter)
    constants = defaultdict(Counter)
    indices = defaultdict(Counter)
    live = Counter()
    for event, phase, data in trace.events:
        counters = phases[phase]
        if event in ("new", "fresh", "hit"):
            definition = trace.definitions[data]
            category = "sort" if definition.category == "s" else "expression"
            counters["requests"] += 1
            counters[f"{category}_requests"] += 1
            counters[{"hit": "lookup_hits", "new": "lookup_misses",
                      "fresh": "fresh_requests"}[event]] += 1
            if event != "fresh":
                counters[f"{category}_lookup_{'hit' if event == 'hit' else 'miss'}"] += 1
            counters["request_edges"] += len(definition.children)
            kinds[phase][f"{category}:{definition.kind}"] += 1
            if event != "hit":
                live[category] += 1
                counters[f"{category}_allocations"] += 1
                counters["allocated_edges"] += len(definition.children)
            sort = trace.types.get(data)
            if sort is not None:
                type_definition = trace.definitions[sort]
                size = " ".join(type_definition.payload)
                sorts[phase][f"{type_definition.kind} {size}".rstrip()] += 1
            elif category == "expression":
                counters["expression_requests_without_type_attribute"] += 1
            if definition.kind in ("CONST_BITVECTOR", "CONST_FLOATINGPOINT"):
                width = definition.payload[:-1]
                constants[phase][f"{definition.kind} {' '.join(width)}"] += 1
            if definition.kind in ("CONST_INTEGER", "CONST_RATIONAL"):
                lengths = tuple(len(piece.lstrip("-"))
                                for piece in definition.payload[0].split("/"))
                constants[phase][f"{definition.kind} decimal digits {lengths}"] += 1
            if definition.operator is not None:
                parameter = trace.definitions[definition.operator]
                if parameter.payload and parameter.kind.endswith("_OP"):
                    indices[phase][f"{definition.kind} {' '.join(parameter.payload)}"] += 1
        elif event in ("collect", "zero"):
            definition = trace.definitions[data]
            category = "sort" if definition.category == "s" else "expression"
            if event == "collect":
                live[category] -= 1
                counters[f"{category}_reclamations"] += 1
            else:
                counters[f"{category}_zero_transitions"] += 1
        elif event == "cache-lookup":
            counters["rewrite_cache_entry_hits" if data == "hit" else
                     "rewrite_cache_entry_misses"] += 1
        else:
            counters[{"visit": "rewrite_visits", "root": "root_occurrences",
                      "snapshot": "snapshots", "cache": "rewrite_cache_writes"}[event]] += 1
        for category in ("expression", "sort"):
            key = f"peak_recorded_live_{category}"
            counters[key] = max(counters[key], live[category])
    return {
        phase: dict(counters, kinds=dict(kinds[phase]), types=dict(sorts[phase]),
                    constants=dict(constants[phase]), indexed_parameters=dict(indices[phase]))
        for phase, counters in phases.items()
    }
