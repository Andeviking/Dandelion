#!/usr/bin/env python3
"""Add a controlled entry to a temporary compilation database."""

import json
from pathlib import Path
import shlex
import sys

database = Path(sys.argv[1])
mode = sys.argv[2]
entries = json.loads(database.read_text())
entry = dict(entries[0])
if mode == "synthetic":
    arguments = entry.get("arguments") or shlex.split(entry.pop("command"))
    arguments[arguments.index("-o") + 1] = "probe@synth_0.dir/probe.bmi"
    arguments.append("--precompile")
    entry["arguments"] = arguments
elif mode != "duplicate":
    raise ValueError("Unknown fixture mode")
entries.append(entry)
database.write_text(json.dumps(entries))
