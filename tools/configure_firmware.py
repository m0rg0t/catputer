#!/usr/bin/env python3
"""Inject the validated repository VERSION into the PlatformIO compilation."""
from pathlib import Path
import re

SAFE_VERSION = re.compile(r"^[0-9a-z.-]+$")


def read_version(project_dir):
    path = Path(project_dir).resolve().parent / "VERSION"
    version = path.read_text().strip()
    if not version or not SAFE_VERSION.fullmatch(version):
        raise ValueError(f"Unsafe VERSION value in {path}")
    return version


def configure(environment):
    project_dir = Path(environment.subst("$PROJECT_DIR"))
    version = read_version(project_dir)
    environment.Append(CPPDEFINES=[("LOFI_VERSION", environment.StringifyMacro(version))])


try:
    Import("env")  # type: ignore[name-defined]  # PlatformIO injects this helper.
except NameError:
    if __name__ == "__main__":
        print(read_version(Path(__file__).resolve().parents[1] / "firmware"))
else:
    configure(env)  # type: ignore[name-defined]
