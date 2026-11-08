"""Compile the actual permission-check method against fake Windows COM folders.

Run on Windows with a MinGW compiler, for example:
    python tests/empty_shell_read_permission.py --compiler C:/Qt/Tools/mingw1310_64/bin/g++.exe

The method is extracted from the production file; no Qt or real Shell state is
needed. Generated sources and executables live in a temporary directory.
"""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=shutil.which("g++"))
    args = parser.parse_args()
    if not args.compiler:
        parser.error("Specify --compiler with the path to a Windows MinGW g++ compiler")

    compiler = Path(args.compiler).resolve()
    root = Path(__file__).resolve().parent.parent
    source = (root / "src/WindowsFileSystemProvider.cpp").read_text(encoding="utf-8")
    method = re.search(
        r"bool WindowsFileSystemProvider::hasReadPermission\(\) const\s*\{.*?^\}",
        source,
        re.MULTILINE | re.DOTALL,
    )
    if method is None:
        raise RuntimeError("Cannot locate the production hasReadPermission method")
    fixture = Path(__file__).with_suffix(".cpp.in").read_text(encoding="utf-8")
    fixture = fixture.replace("@PRODUCTION_METHOD@", method.group())

    environment = os.environ.copy()
    environment["PATH"] = str(compiler.parent) + os.pathsep + environment.get("PATH", "")
    with tempfile.TemporaryDirectory(prefix="treewalker-empty-shell-") as temporary:
        directory = Path(temporary)
        test_source = directory / "empty_shell_read_permission.cpp"
        test_executable = directory / "empty_shell_read_permission.exe"
        test_source.write_text(fixture, encoding="utf-8")
        subprocess.run(
            [str(compiler), "-std=c++17", "-Wall", "-Wextra", "-Werror",
             str(test_source), "-o", str(test_executable)],
            check=True,
            env=environment,
        )
        subprocess.run([str(test_executable)], check=True, env=environment)


if __name__ == "__main__":
    main()
