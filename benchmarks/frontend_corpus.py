"""Select and stage local SMT-LIB inputs for frontend benchmarks."""

from __future__ import annotations

import argparse
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from contextlib import ExitStack, contextmanager
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import stat
import tempfile

import process


_BLOCK_BYTES = 1024 * 1024
_LOCAL_NOATIME_FILESYSTEMS = {"btrfs", "ext2", "ext3", "ext4", "tmpfs", "xfs"}


def _protected(path):
    # Linux treats both possible absolute pathlib anchors as the same root.
    path = Path("/", *path.parts[1:])
    return path == process.PROTECTED_DATA or process.PROTECTED_DATA in path.parents


def _destination(path):
    output = process.outside_dataset(path)
    if not output.exists():
        for parent in path.absolute().parents:
            process.outside_dataset(parent.resolve())
    return process.outside_dataset(path.resolve())


@contextmanager
def _directory_at(fd, components):
    # O_PATH supplies directory descriptors for lookups without reading entries.
    flags = os.O_PATH | os.O_DIRECTORY | os.O_NOFOLLOW
    with ExitStack() as stack:
        for component in components:
            fd = os.open(component, flags, dir_fd=fd)
            stack.callback(os.close, fd)
        yield fd


@contextmanager
def _source_directory(path):
    with _directory_at(None, path.parts) as fd:
        yield fd


def _check_noatime(fd, flag):
    if not flag or not fcntl.fcntl(fd, fcntl.F_GETFL) & flag:
        raise ValueError("Metadata-safe corpus access is unavailable: O_NOATIME is required")
    with open(f"/proc/self/fdinfo/{fd}", encoding="ascii") as info:
        mount_id = next((line.split()[1] for line in info if line.startswith("mnt_id:")), None)
    with open("/proc/self/mountinfo", encoding="ascii", errors="surrogateescape") as mounts:
        for line in mounts:
            fields, filesystem = line.split(" - ", 1)
            if fields.split()[0] == mount_id:
                kind = filesystem.split()[0]
                if kind in _LOCAL_NOATIME_FILESYSTEMS:
                    return
                raise ValueError(f"Metadata-safe corpus access is unavailable on {kind}")
    raise ValueError("Metadata-safe corpus access is unavailable: source mount is unknown")


@contextmanager
def _regular_file_at(fd, relative, require_noatime):
    with _directory_at(fd, relative.parts[:-1]) as parent:
        info = os.stat(relative.name, dir_fd=parent, follow_symlinks=False)
        if not stat.S_ISREG(info.st_mode):
            raise ValueError(f"Expected a regular source file: {relative}")
        flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK
        noatime = getattr(os, "O_NOATIME", 0)
        if require_noatime and not noatime:
            raise ValueError("Metadata-safe corpus access is unavailable: O_NOATIME is required")
        try:
            file_fd = os.open(relative.name, flags | noatime, dir_fd=parent)
        except OSError as error:
            if not noatime or error.errno not in (errno.EPERM, errno.EINVAL, errno.EOPNOTSUPP):
                raise
            if require_noatime:
                raise ValueError(f"Metadata-safe corpus access is unavailable: {relative}") from error
            file_fd = os.open(relative.name, flags, dir_fd=parent)
        with os.fdopen(file_fd, "rb") as stream:
            if not stat.S_ISREG(os.fstat(file_fd).st_mode):
                raise ValueError(f"Expected a regular source file: {relative}")
            if require_noatime:
                _check_noatime(file_fd, noatime)
            yield stream


def _smt_path(value):
    if not isinstance(value, str) or any(char in value for char in "\0\r\n"):
        raise ValueError(f"Invalid SMT-LIB source path: {value!r}")
    path = Path(value)
    if (path.is_absolute() or ".." in path.parts or path.suffix != ".smt2"
            or path.as_posix().startswith("#") or path.as_posix().strip() != path.as_posix()):
        raise ValueError(f"Expected a relative .smt2 path: {value!r}")
    return path


def _explicit_paths(path, allow_unsafe_atime):
    require_noatime = _protected(path) and not allow_unsafe_atime
    with _source_directory(path.parent) as fd:
        with _regular_file_at(fd, Path(path.name), require_noatime) as stream:
            lines = stream.read().decode("utf-8").split("\n")
    selected = {}
    for number, line in enumerate(lines, 1):
        value = line.strip()
        if value and not value.startswith("#"):
            relative = _smt_path(value)
            if relative in selected:
                raise ValueError(f"Duplicate selection at {path}:{number}: {relative}")
            selected[relative] = None
    return selected


def _index_paths(fd, require_noatime):
    with _regular_file_at(fd, Path("benchmarks.json"), require_noatime) as stream:
        index = json.load(stream)
    groups = index if isinstance(index, list) else [index]
    paths = set()
    for group in groups:
        if not isinstance(group, dict) or not isinstance(group.get("benchmarks"), list):
            raise ValueError("Expected benchmarks.json groups with a benchmarks list")
        paths.update(_smt_path(value) for value in group["benchmarks"])
    return paths


def _logic(path):
    return path.parts[0] if len(path.parts) >= 2 else ""


def _family(path):
    return path.parts[1] if len(path.parts) >= 3 else ""


def _choose_indexed(fd, paths, *, per_logic, min_bytes, max_bytes, seed):
    by_directory = defaultdict(list)
    for path in sorted(paths):
        by_directory[path.parent].append(path)

    by_logic = defaultdict(list)
    for directory, entries in by_directory.items():
        with _directory_at(fd, directory.parts) as parent:
            for path in entries:
                info = os.stat(path.name, dir_fd=parent, follow_symlinks=False)
                if not stat.S_ISREG(info.st_mode):
                    raise ValueError(f"Expected a regular indexed file: {path}")
                if min_bytes <= info.st_size <= max_bytes:
                    by_logic[_logic(path)].append((info.st_size, path))

    selected = {}
    for logic, entries in sorted(by_logic.items()):
        rng = random.Random(f"{seed}:{logic}")
        entries.sort()
        count = min(per_logic, len(entries))
        family_counts = defaultdict(int)
        for bucket in range(count):
            start = bucket * len(entries) // count
            stop = (bucket + 1) * len(entries) // count
            if count > 1 and bucket == count - 1:
                # Sample the upper tail even when most indexed inputs are small.
                start = max(start, stop - max(1, len(entries) // 16))
            families = defaultdict(list)
            for size, path in entries[start:stop]:
                families[_family(path)].append((size, path))
            fewest = min(family_counts[family] for family in families)
            family = rng.choice(sorted(family for family in families
                                       if family_counts[family] == fewest))
            size, path = rng.choice(families[family])
            family_counts[family] += 1
            selected[path] = size
    return selected


def _copy_case(fd, path, expected_size, inputs, require_noatime):
    with _regular_file_at(fd, path, require_noatime) as source:
        before = os.fstat(source.fileno())
        if expected_size is not None and before.st_size != expected_size:
            raise ValueError(f"Indexed size changed before copying: {path}")
        destination = inputs / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        digest = hashlib.sha256()
        size = 0
        with destination.open("xb") as staged:
            for block in iter(lambda: source.read(_BLOCK_BYTES), b""):
                size += len(block)
                if size > before.st_size:
                    raise ValueError(f"Source grew during copying: {path}")
                digest.update(block)
                staged.write(block)
        after = os.fstat(source.fileno())
        if (size != before.st_size or size != after.st_size
                or before.st_mtime_ns != after.st_mtime_ns
                or before.st_ctime_ns != after.st_ctime_ns):
            raise ValueError(f"Source changed during copying: {path}")

    sha256 = digest.hexdigest()
    if destination.stat().st_size != size or process.sha256(destination) != sha256:
        raise ValueError(f"Staged file verification failed: {path}")
    return {
        "case": path.as_posix(),
        "logic": _logic(path),
        "family": _family(path),
        "source_relative": path.as_posix(),
        "relative_path": (Path("inputs") / path).as_posix(),
        "bytes": size,
        "sha256": sha256,
    }


def _publish(fd, output, selected, manifest, jobs, require_noatime):
    output.mkdir(parents=True)
    try:
        def copy(item):
            path, size = item
            try:
                return _copy_case(fd, path, size, output / "inputs", require_noatime)
            except OSError as error:
                raise ValueError(f"Could not stage {path}: {error}") from error

        with ThreadPoolExecutor(max_workers=min(jobs, len(selected))) as workers:
            manifest["cases"] = list(workers.map(copy, selected.items()))

        selection = "".join(f"{path.as_posix()}\n" for path in selected).encode("utf-8")
        (output / "corpus_selection.txt").write_bytes(selection)
        manifest["selection_sha256"] = hashlib.sha256(selection).hexdigest()
        with tempfile.TemporaryDirectory(dir=output) as temp:
            pending = Path(temp) / "corpus_manifest.json"
            process.write_json(pending, manifest)
            pending.replace(output / pending.name)
    except BaseException:
        shutil.rmtree(output)
        raise


def select_and_stage_corpus(
    *, dataset_root: Path, output_dir: Path, per_logic: int = 5,
    min_bytes: int = 1024, max_bytes: int = 64 * 1024 * 1024,
    seed: int = 20261119, jobs: int = 16, selection_file: Path | None = None,
    snapshot: str = "benchmark-2025", source_evidence: str = "",
    allow_unsafe_atime: bool = False,
) -> dict:
    """Stage a verified corpus in a fresh destination and return its manifest."""
    output = _destination(output_dir)
    if per_logic < 1 or jobs < 1 or not 0 <= min_bytes <= max_bytes:
        raise ValueError("Expected positive per-logic and jobs limits and an ordered byte range")
    root = dataset_root.absolute()
    selection = selection_file.absolute() if selection_file is not None else None
    if ".." in root.parts or (selection is not None and ".." in selection.parts):
        raise ValueError("Source directory and selection file paths must omit '..'")
    require_noatime = _protected(root) and not allow_unsafe_atime
    manifest = {
        "schema": 1,
        "snapshot": snapshot,
        "source_evidence": source_evidence,
        "dataset_root": str(root),
    }
    try:
        with _source_directory(root) as fd:
            if selection is None:
                settings = {"per_logic": per_logic, "min_bytes": min_bytes,
                            "max_bytes": max_bytes, "seed": seed}
                selected = _choose_indexed(fd, _index_paths(fd, require_noatime), **settings)
                manifest["indexed_selection"] = settings
            else:
                selected = _explicit_paths(selection, allow_unsafe_atime)
            if not selected:
                raise ValueError("No SMT-LIB inputs matched the corpus selection")
            _publish(fd, output, selected, manifest, jobs, require_noatime)
    except OSError as error:
        raise ValueError(f"Corpus staging unavailable: {error}") from error
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--selection", type=Path)
    parser.add_argument("--per-logic", type=int, default=5)
    parser.add_argument("--min-bytes", type=int, default=1024)
    parser.add_argument("--max-bytes", type=int, default=64 * 1024 * 1024)
    parser.add_argument("--seed", type=int, default=20261119)
    parser.add_argument("--jobs", type=int, default=16)
    parser.add_argument("--snapshot", default="benchmark-2025")
    parser.add_argument("--source-evidence", default="")
    parser.add_argument("--allow-unsafe-atime", action="store_true",
                        help="allow reads when access-time preservation cannot be guaranteed")
    args = parser.parse_args()
    try:
        manifest = select_and_stage_corpus(
            dataset_root=args.source, output_dir=args.output, selection_file=args.selection,
            per_logic=args.per_logic, min_bytes=args.min_bytes, max_bytes=args.max_bytes,
            seed=args.seed, jobs=args.jobs, snapshot=args.snapshot,
            source_evidence=args.source_evidence, allow_unsafe_atime=args.allow_unsafe_atime,
        )
    except ValueError as error:
        parser.exit(1, f"{parser.prog}: {error}\n")
    print(f"Staged {len(manifest['cases'])} corpus inputs in {args.output}")


if __name__ == "__main__":
    main()
