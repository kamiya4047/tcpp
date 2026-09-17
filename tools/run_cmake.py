"""Invoke bundled/system CMake with a case-normalized Windows process environment."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

environment = {key.upper(): value for key, value in os.environ.items()} if os.name == "nt" else dict(os.environ)
environment["VSLANG"] = "1033"
cmake = shutil.which("cmake")
if not cmake and os.name == "nt":
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    installation = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], env=environment, text=True).strip()
    cmake = str(Path(installation) / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe")
if not cmake:
    raise SystemExit("CMake not found; install CMake and a C++23 compiler.")
arguments = sys.argv[1:]
if arguments and arguments[0] in ("--ctest", "--cpack"):
    tool = arguments.pop(0)[2:]
    cmake = str(Path(cmake).with_name(tool + (".exe" if os.name == "nt" else "")))
raise SystemExit(subprocess.call([cmake, *arguments], env=environment))
