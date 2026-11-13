@rem SPDX-License-Identifier: MIT
@rem Copyright (c) 2026 Oleg Kozhevnikov
@echo off
python "%~dp0tools\format_cpp.py" --apply %*
exit /b %errorlevel%
