#!/usr/bin/env python3
"""Format project C++ files or check naming with a Clang CMake build."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TOOL_VERSION = "23.1.5"
SOURCE_ROOTS = ("src", "include", "tests", "tools", "benchmarks")
HEADER_EXTENSIONS = {".h", ".hh", ".hpp", ".hxx", ".inc", ".ipp", ".tpp"}
UNIT_EXTENSIONS = {".cc", ".cpp", ".cxx", ".c++", ".cppm", ".ixx", ".mpp"}
EXCLUDED_DIRECTORIES = {
    "third_party", "third-party", "vendor", "external", "generated",
    "build", "Build", "CMakeFiles", "_deps", ".git", "__pycache__",
}


class CheckError(Exception):
    """A missing prerequisite or an invalid check scope."""


def is_unit(path: Path) -> bool:
    return path.suffix in UNIT_EXTENSIONS or path.suffix == ".C"


def discover_sources(root: Path = ROOT) -> list[Path]:
    """Visit only owned source roots; never follow symlinks into dependencies."""
    files = []
    for name in SOURCE_ROOTS:
        source_root = root / name
        if not source_root.is_dir() or source_root.is_symlink():
            continue
        for directory, directories, names in os.walk(source_root):
            directories[:] = sorted(
                name for name in directories
                if name not in EXCLUDED_DIRECTORIES
                and not name.startswith("build-")
                and not (Path(directory) / name).is_symlink()
            )
            for name in sorted(names):
                path = Path(directory) / name
                if not path.is_symlink() and (
                    is_unit(path) or path.suffix.lower() in HEADER_EXTENSIONS
                ):
                    files.append(path.resolve())
    if not files:
        raise CheckError("No project C++ files found; no checks were performed.")
    return sorted(files)


def require_tool_version(tool: str, *, clang: bool = False) -> None:
    result = subprocess.run([tool, "--version"], capture_output=True, text=True)
    output = result.stdout + result.stderr
    if clang and not re.search(r"\bclang version\b", output, re.IGNORECASE):
        raise CheckError("Naming checks require a Clang build, not a GCC database.")
    version = re.search(r"\bversion\s+(\d+\.\d+\.\d+)\b", output, re.IGNORECASE)
    if result.returncode or not version or version.group(1) != TOOL_VERSION:
        raise CheckError(f"{tool} must be version {TOOL_VERSION}: {output.strip()}")


def run(command: list[str]) -> None:
    subprocess.run(command, cwd=ROOT, check=True)


def check_format(files: list[Path], tool: str, *, fix: bool = False) -> None:
    if not files:
        raise CheckError("No C++ files selected for formatting.")
    require_tool_version(tool)
    print(f"{'Formatting' if fix else 'Checking format in'} {len(files)} C++ files.",
          flush=True)
    run([tool, "--style=file", "--fallback-style=none",
         *(["-i"] if fix else ["--dry-run", "--Werror"]),
         "--", *(str(path) for path in files)])


def compilation_arguments(entry: dict) -> list[str]:
    return entry.get("arguments") or shlex.split(entry["command"])


def is_cmake_synthetic_module(entry: dict) -> bool:
    """Identify CMake's Clang-only synthesized BMI command, not an object build."""
    arguments = compilation_arguments(entry)
    if "--precompile" not in arguments or "-o" not in arguments:
        return False
    output_index = arguments.index("-o") + 1
    return output_index < len(arguments) and bool(re.search(
        r"(?:^|[/\\])[^/\\]*@synth_\d+\.dir[/\\]", arguments[output_index]
    ))


def check_naming(files: list[Path], tool: str, build_dir: Path) -> None:
    require_tool_version(tool)
    build_dir = build_dir.resolve()
    database_path = build_dir / "compile_commands.json"
    if not database_path.is_file():
        raise CheckError(f"Missing {database_path}; configure the Clang preset first.")
    database = json.loads(database_path.read_text())
    cache_path = build_dir / "CMakeCache.txt"
    if cache_path.is_file():
        cache = cache_path.read_text()
        for option, source_dir, label in (
            ("DANDELION_BUILD_BENCHMARKS", "benchmarks", "benchmark"),
            ("DANDELION_BUILD_FUZZERS", "tests/fuzz", "fuzzer"),
        ):
            match = re.search(rf"^{option}:BOOL=(.*)$", cache, re.MULTILINE)
            if not match:
                continue
            value = match.group(1).upper()
            if (value in {"", "0", "OFF", "NO", "FALSE", "N", "IGNORE", "NOTFOUND"}
                    or value.endswith("-NOTFOUND")):
                source_root = (ROOT / source_dir).resolve()
                enabled_files = [path for path in files if source_root not in path.parents]
                if len(enabled_files) != len(files):
                    print(f"Skipping {label} naming checks ({option} is disabled).",
                          flush=True)
                    files = enabled_files
    units = [path for path in files if is_unit(path)]
    if not units:
        raise CheckError("No C++ translation units found; no naming checks performed.")
    commands = {}
    for entry in database:
        directory = Path(entry["directory"])
        path = Path(entry["file"])
        path = (directory / path).resolve() if not path.is_absolute() else path.resolve()
        if path in units:
            if is_cmake_synthetic_module(entry):
                continue
            if path in commands:
                raise CheckError(f"Ambiguous compilation commands for {path}.")
            commands[path] = entry
    compilers = set()
    for path in units:
        if path not in commands:
            raise CheckError(f"No compilation command for {path}; add it to CMake first.")
        entry = commands[path]
        arguments = compilation_arguments(entry)
        compiler = Path(arguments[0])
        if not compiler.is_absolute() and len(compiler.parts) > 1:
            compiler = Path(entry["directory"]) / compiler
        compilers.add(str(compiler))
    for compiler in sorted(compilers):
        require_tool_version(compiler, clang=True)

    # CMake writes module response files and BMIs during the build, not configure.
    run(["cmake", "--build", str(build_dir)])
    run([tool, f"--config-file={ROOT / '.clang-tidy'}", "--verify-config"])
    headers = [str(path) for path in files if not is_unit(path)]
    header_filter = "^(" + "|".join(re.escape(path) for path in headers) + ")$" \
        if headers else "^$"
    # CMake can emit additional --precompile entries for synthetic BMI variants.
    # Give clang-tidy only the verified object command for each owned unit.
    with tempfile.TemporaryDirectory(prefix="dandelion-tidy-") as directory:
        (Path(directory) / "compile_commands.json").write_text(
            json.dumps(list(commands.values()))
        )
        for path in units:
            print(f"Checking naming: {path.relative_to(ROOT)}", flush=True)
            run([tool, f"--config-file={ROOT / '.clang-tidy'}",
                 f"--header-filter={header_filter}",
                 "--warnings-as-errors=readability-identifier-naming",
                 "-p", directory, str(path)])
    print(f"Checked {len(units)} project translation units (including module interfaces).",
          flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("format-check", "format", "tidy"))
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/clang-debug")
    parser.add_argument("--clang-format", default="clang-format")
    parser.add_argument("--clang-tidy", default="clang-tidy")
    arguments = parser.parse_args()
    try:
        files = discover_sources()
        if arguments.action == "tidy":
            check_naming(files, arguments.clang_tidy, arguments.build_dir)
        else:
            check_format(files, arguments.clang_format, fix=arguments.action == "format")
    except (CheckError, OSError, ValueError, KeyError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    except subprocess.CalledProcessError as error:
        return error.returncode if error.returncode > 0 else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
