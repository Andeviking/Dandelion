"""Bounded Linux subprocess execution and benchmark artifact utilities."""

import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import signal
import subprocess
import time

PROTECTED_DATA = Path("/pub/netdisk1/zhangx")


def outside_dataset(path):
    requested = Path(os.path.abspath(path))
    if requested == PROTECTED_DATA or PROTECTED_DATA in requested.parents:
        raise ValueError("Use an existing metadata-safe copy outside the protected dataset")
    resolved = requested.resolve()
    if resolved == PROTECTED_DATA or PROTECTED_DATA in resolved.parents:
        raise ValueError("Path resolves to the protected dataset")
    return resolved


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n")


def run_process(argv, prefix, timeout, memory_mib, *, env=None, cpu=None,
                file_bytes=64 * 1024 * 1024, child_rss=False):
    """Measure a single child and kill its process group on the wall deadline."""
    argv = [str(arg) for arg in argv]
    stdout = prefix.with_suffix(".stdout")
    stderr = prefix.with_suffix(".stderr")
    record = {"argv": argv, "timeout_s": timeout, "address_space_mib": memory_mib,
              "file_limit_bytes": file_bytes, "cpu_affinity": cpu,
              "stdout": str(stdout), "stderr": str(stderr)}
    command = argv
    if child_rss:
        time_program = shutil.which("time")
        if time_program is None:
            return dict(record, status="launch-error", message="GNU time is unavailable")
        rss_path = prefix.with_suffix(".rusage")
        command = [time_program, "-f", "%M", "-o", str(rss_path), "--", *argv]
        record.update(rss_output=str(rss_path), rss_method="GNU time child peak KiB",
                      launcher_argv=command)
        env = dict(os.environ if env is None else env, LC_ALL="C")
    else:
        record["rss_method"] = "wait4 process peak KiB (includes launch)"

    def set_limits():
        memory = memory_mib * 1024 * 1024
        resource.setrlimit(resource.RLIMIT_AS, (memory, memory))
        resource.setrlimit(resource.RLIMIT_FSIZE, (file_bytes, file_bytes))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        if cpu is not None:
            os.sched_setaffinity(0, {cpu})

    started = time.monotonic()
    with stdout.open("wb") as out, stderr.open("wb") as err:
        try:
            child = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                     stdout=out, stderr=err, env=env,
                                     start_new_session=True, preexec_fn=set_limits)
        except (OSError, subprocess.SubprocessError) as error:
            return dict(record, status="launch-error", message=str(error))
        expired = False
        try:
            while True:
                pid, status, usage = os.wait4(child.pid, os.WNOHANG)
                if pid:
                    break
                if time.monotonic() - started >= timeout:
                    expired = True
                    raise TimeoutError
                time.sleep(0.005)
        except BaseException:
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            _, status, usage = os.wait4(child.pid, 0)
            child.returncode = decode_wait(status)
            if not expired:
                raise
        code = child.returncode = decode_wait(status)
    rss_kib = usage.ru_maxrss
    if child_rss:
        # GNU time forks a small child after exec. Its child peak excludes
        # Python's pre-exec memory, which wait4 can otherwise include.
        rss_kib = None
        if rss_path.is_file():
            rows = rss_path.read_text(errors="replace").splitlines()
            if rows and rows[-1].isascii() and rows[-1].isdecimal():
                rss_kib = int(rows[-1])
            if rows:
                killed = re.fullmatch(r"Command terminated by signal (\d+)", rows[0])
                if killed:
                    code = -int(killed[1])
    if expired:
        outcome = "timeout"
    elif code == 0:
        outcome = "ok" if rss_kib else "invalid-rss"
    elif code == -signal.SIGXFSZ:
        outcome = "resource-limit"
    elif code < 0:
        outcome = "crash"
    else:
        outcome = "failed"
    return dict(record, status=outcome, returncode=code,
                wall_s=time.monotonic() - started,
                user_s=usage.ru_utime, system_s=usage.ru_stime,
                rss_kib=rss_kib)


def decode_wait(status):
    if os.WIFEXITED(status):
        return os.WEXITSTATUS(status)
    return -os.WTERMSIG(status)
