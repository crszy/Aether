#!/usr/bin/env python3
import argparse
import concurrent.futures
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

MAGIC = b"qsqmlcb1"
FORMAT = 1
UNIT_MAGIC = b"qv4cdata"
UNIT_HEADER = struct.Struct("<8sIIqI")
UNIT_FLAGS_OFFSET = UNIT_HEADER.size + 32
STATIC_DATA = 0x2
ALIGN = 16
HEADER = struct.Struct("<8sII16sII8x")
ENTRY = struct.Struct("<IIII16s")
BUNDLE_DIR = ".qmlcache"
BUNDLE_NAME = "bundle.bin"
SOURCE_SUFFIXES = (".qml", ".js", ".mjs")


def qt_version(qmlcachegen):
    out = subprocess.run([qmlcachegen, "--version"], capture_output=True, text=True, check=True)
    version = out.stdout.strip().split()[-1]
    if len(version.encode()) > 16:
        raise SystemExit(f"unexpected qmlcachegen version string: {out.stdout!r}")
    return version


def uses_preprocessor(data):
    return any(line.strip().startswith(b"//@ if ") for line in data.splitlines())


def collect(root):
    files = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if not d.startswith("."))
        for name in sorted(filenames):
            if not name.endswith(SOURCE_SUFFIXES):
                continue
            path = os.path.join(dirpath, name)
            if name.endswith(".qml") and os.path.exists(path + ".json"):
                continue
            files.append(os.path.relpath(path, root).replace(os.sep, "/"))
    return files


def compile_one(qmlcachegen, root, tmpdir, index, rel):
    source = os.path.join(root, rel)
    with open(source, "rb") as f:
        data = f.read()
    if rel.endswith(".qml") and uses_preprocessor(data):
        return rel, None, None, "uses //@ if"
    out = os.path.join(tmpdir, f"{index}{os.path.splitext(rel)[1]}c")
    proc = subprocess.run([qmlcachegen, "-o", out, source], capture_output=True, text=True)
    if proc.returncode != 0 or not os.path.exists(out):
        return rel, None, None, (proc.stderr or proc.stdout).strip() or f"exit {proc.returncode}"
    with open(out, "rb") as f:
        unit = f.read()
    return rel, hashlib.md5(data).digest(), unit, None


def check_unit(rel, unit, expected_version):
    if len(unit) < UNIT_FLAGS_OFFSET + 4:
        raise SystemExit(f"{rel}: truncated unit")
    magic, version, _, timestamp, size = UNIT_HEADER.unpack_from(unit, 0)
    (flags,) = struct.unpack_from("<I", unit, UNIT_FLAGS_OFFSET)
    if magic != UNIT_MAGIC or size != len(unit) or timestamp != 0 or not flags & STATIC_DATA:
        raise SystemExit(f"{rel}: unexpected unit layout from qmlcachegen")
    if expected_version is not None and version != expected_version:
        raise SystemExit(f"{rel}: unit version {version:#x} differs from {expected_version:#x}")
    return version


def pad(blob, align=ALIGN):
    return blob + b"\0" * (-len(blob) % align)


def write_bundle(path, qt, units):
    strings = bytearray()
    path_refs = []
    for rel, _, _ in units:
        encoded = rel.encode()
        path_refs.append((len(strings), len(encoded)))
        strings += encoded

    strings_offset = HEADER.size + ENTRY.size * len(units)
    unit_offset = strings_offset + len(strings)
    unit_offset += -unit_offset % ALIGN

    entries = bytearray()
    body = bytearray()
    for (rel, md5, unit), (path_offset, path_size) in zip(units, path_refs):
        entries += ENTRY.pack(path_offset, path_size, unit_offset + len(body), len(unit), md5)
        body += pad(unit)

    blob = HEADER.pack(MAGIC, FORMAT, len(units), qt.encode(), strings_offset, len(strings))
    blob = pad(bytes(blob + entries + strings)) + bytes(body)
    if len(blob) >= 1 << 30:
        raise SystemExit("bundle too large")

    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(blob)
    os.replace(tmp, path)
    return len(blob)


def main():
    parser = argparse.ArgumentParser(
        description="Precompile a quickshell config's QML/JS into the bytecode bundle quickshell loads "
        "from <config>/.qmlcache/bundle.bin"
    )
    parser.add_argument("config", help="config root (the folder holding shell.qml)")
    parser.add_argument("--qmlcachegen", required=True, help="host qmlcachegen of the same Qt version as the target")
    parser.add_argument("--qt-version", help="target Qt version, checked against qmlcachegen --version")
    parser.add_argument("-o", "--output", help=f"bundle path (default: <config>/{BUNDLE_DIR}/{BUNDLE_NAME})")
    parser.add_argument("-j", "--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("-q", "--quiet", action="store_true")
    args = parser.parse_args()

    root = os.path.abspath(args.config)
    output = args.output or os.path.join(root, BUNDLE_DIR, BUNDLE_NAME)
    qt = qt_version(args.qmlcachegen)
    if args.qt_version and args.qt_version != qt:
        raise SystemExit(f"qmlcachegen is Qt {qt}, target is Qt {args.qt_version}")

    files = collect(root)
    units = []
    skipped = []
    with tempfile.TemporaryDirectory(prefix="qmlcache-") as tmpdir:
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            jobs = [pool.submit(compile_one, args.qmlcachegen, root, tmpdir, i, rel) for i, rel in enumerate(files)]
            for job in jobs:
                rel, md5, unit, error = job.result()
                if error is None:
                    units.append((rel, md5, unit))
                else:
                    skipped.append((rel, error))

    version = None
    for rel, _, unit in units:
        version = check_unit(rel, unit, version)

    size = write_bundle(output, qt, units)
    for rel, error in skipped:
        print(f"qmlcache: skipped {rel}: {error.splitlines()[0] if error else ''}", file=sys.stderr)
    if not args.quiet:
        print(f"qmlcache: {len(units)} units, {len(skipped)} skipped, {size} bytes -> {output}")


if __name__ == "__main__":
    main()
