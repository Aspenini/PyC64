"""Compile-time binary assets; CPython reads the same bytes at call time.

Compiled relative paths use the calling source module's directory, or the
project's assets.directory. CPython relative paths use its current directory.
"""
from pathlib import Path


def load(path: str) -> bytes:
    data = Path(path).read_bytes()
    if len(data) > 32764:
        raise ValueError("asset exceeds the 32764-byte payload limit")
    return data
