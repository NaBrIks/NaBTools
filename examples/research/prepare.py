# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleg Kozhevnikov
Import('env')
import subprocess
from pathlib import Path
example = Path(env.subst('$PROJECT_DIR'))
# PlatformIO watches the application's CMakeLists, but not sibling components.
# Refresh its CMake source inventory when a component adds a translation unit.
# https://github.com/platformio/platform-espressif32/blob/v8.0.0/builder/frameworks/espidf.py
cache = Path(env.subst('$BUILD_DIR')) / 'CMakeCache.txt'
components = [example.parents[1] / 'CMakeLists.txt', example.parents[2] / 'NaWServer/CMakeLists.txt']
if cache.is_file() and any(p.is_file() and p.stat().st_mtime_ns > cache.stat().st_mtime_ns for p in components):
    cache.unlink()
subprocess.run([env.subst('$PYTHONEXE'),str(example/'tools/pack_research.py')],check=True)
