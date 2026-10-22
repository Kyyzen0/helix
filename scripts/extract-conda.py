#!/usr/bin/env python3
"""Extracts the payload of a conda package.

A .conda file is a zip that holds zstd compressed tarballs. A .tar.bz2 file is
already a tarball. Both are unpacked into one directory, so the caller can
copy the binaries and the libraries they need out of it.

Usage: extract-conda.py <package> <destination>
"""

import io
import os
import sys
import tarfile
import zipfile


def extract_conda_package(package: str, destination: str) -> None:
    with zipfile.ZipFile(package) as archive:
        members = [n for n in archive.namelist() if n.startswith("pkg-")]
        if not members:
            raise SystemExit(f"{package} holds no pkg- payload")
        member = next((n for n in members if n.endswith(".tar.zst")), members[0])

        with archive.open(member) as raw:
            if member.endswith(".zst"):
                import zstandard

                decompressor = zstandard.ZstdDecompressor()
                payload = decompressor.stream_reader(raw).read()
            else:
                payload = raw.read()

    with tarfile.open(fileobj=io.BytesIO(payload)) as tar:
        tar.extractall(destination)


def extract_tarball(package: str, destination: str) -> None:
    with tarfile.open(package) as tar:
        tar.extractall(destination)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    package, destination = sys.argv[1], sys.argv[2]
    os.makedirs(destination, exist_ok=True)

    if package.endswith(".conda"):
        extract_conda_package(package, destination)
    else:
        extract_tarball(package, destination)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
