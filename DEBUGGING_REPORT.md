# Trading Bot Debugging Report
**Date:** 2026-03-19
**Issue:** Multiple telegram_listener instances running, duplicate command execution, Python API startup failures

---

## Issues Identified

### 1. `wmic` Command Not Recognized (Windows 11)
**File:** `startup_telegram_listener.bat` (line 24)

**Error:**
```
'wmic' is not recognized as an internal or external command,
operable program or batch file.
```

**Root Cause:** The `wmic` command was deprecated in Windows 10 and removed in Windows 11.

**Fix Applied:** Replaced `wmic process where "name='python.exe'"...` with PowerShell using `Get-CimInstance Win32_Process`.

---

### 2. Market Status Check Always Returns "Before Market Open"
**File:** `startup_telegram_listener.bat` (lines 34-46)

**Error:**
```
Current ET: 2026-03-19 (Thursday)
Before market open. Waiting until 8:00 AM ET...
```
(The bot kept repeating this even though it was 9:20+ AM ET)

**Root Cause:** When PowerShell returned single-digit hours like "9", the `for /f` command captured it with a leading space, causing `set /a time_minutes= 9 * 60 + ...` to fail (space in number). The variable remained unset (became 0), making `0 LSS 480` (8:00 AM in minutes) always true.

**Additional Issues:**
- Timezone offset was hardcoded as `-5` which doesn't account for EDT ( Daylight Saving)
- Simple `AddHours(-5)` is not proper timezone conversion

**Fix Applied:**
- Added `tokens=* delims=` to properly capture values without leading spaces
- Added `set /a hour=%hour%+0` to strip leading zeros and ensure numeric conversion
- Changed to proper `System.TimeZoneInfo::ConvertTimeFromUtc()` with 'Eastern Standard Time' timezone

---

### 3. Python API Fails to Start - "Failed to start Python API"
**File:** `telegram_listener.py` (function `start_python_api_service()`)

**Error:**
```
Warning: Python API did not start in time - will retry on /run command
```

**Root Cause:** The code called `_python_api_process.communicate(timeout=5)` on a long-running server process. The `communicate()` method waits for the process to exit, which a server never does. After 5 seconds it timed out and reported failure, even though the API was actually starting successfully.

**Fix Applied:** Changed to check `process.poll()` instead of calling `communicate()`. If the process exited, capture output. If still running but health check hasn't passed, report as "slow to start" rather than failed.

---

### 4. Duplicate Telegram Listener Instances (UNRESOLVED)
**Files:** `startup_telegram_listener.bat`, `telegram_listener.py`

**Symptom:** Two `telegram_listener.py` instances running simultaneously with different Python interpreters:
```
PID 12876: C:\Users\Atharva\Documents\Trading_super\venv\Scripts\python.exe telegram_listener.py
PID 7332:  C:\Users\Atharva\AppData\Roaming\uv\python\cpython-3.12.12-windows-x86_64-none\python.exe telegram_listener.py
```

**Investigation:**
- PID 7332's parent is PID 12876 (child of the first instance)
- The second process is spawned during `start_python_api_service()` call in the main startup sequence
- Process tree shows: `bash.exe` → `venv python` (12876) → `uv python` (7332)

**Root Cause (Suspected):** The `subprocess.Popen` for starting the Python API service may be creating a child process that somehow inherits or re-spawns `telegram_listener.py`. This could be related to:
- How `uv` (Python version manager) wraps Python executables
- The `run_service.py` script potentially importing or executing code that spawns new processes

**Debugging Steps Attempted:**
1. Added `check_single_instance()` function with file locking (Windows: `msvcrt.locking`)
2. Created thorough `kill_processes.ps1` script to clean up all instances
3. Attempted to trace parent-child process relationships
4. Searched for `subprocess` calls that could spawn telegram_listener
5. Checked if `run_service.py` or scheduler starts telegram_listener (it does not)
6. Verified `PYTHON_VENV_PYTHON` path is correct

**Partial Fixes Applied:**
- Added single-instance lock mechanism using `msvcrt` (Windows-compatible)
- Updated `kill_processes.ps1` to thoroughly kill all related processes
- Changed Python API startup to not block on `communicate()`

**Still Needed:** The child process spawning needs more investigation. Possible approaches:
1. Add `creationflags=DETACHED_PROCESS` to `subprocess.Popen` to fully detach the child process
2. Investigate why `uv`-managed Python is being invoked instead of the venv Python
3. Check if there's an import-time side effect in the service scripts

---

### 5. Telegram Offset Tracking with Multiple Instances
**File:** `telegram_listener.py`

**Issue:** Each `telegram_listener` instance maintains its own `LAST_UPDATE_ID`. If two instances poll Telegram:
- Instance A processes command (update_id=100), sets LAST_UPDATE_ID=100
- Instance B already had LAST_UPDATE_ID=100 from earlier, processes it again (update_id=100)
- Commands get duplicated

**Status:** Will be resolved once duplicate instance issue (#4) is fixed.

---

## Files Modified

### `startup_telegram_listener.bat`
- Replaced `wmic` with PowerShell + `Get-CimInstance`
- Fixed time parsing with proper token handling
- Added proper Eastern Time timezone conversion
- Created `kill_processes.ps1` helper script

### `telegram_listener.py`
- Added `check_single_instance()` with Windows-compatible `msvcrt` locking
- Added single-instance check at startup (exits if another instance running)
- Fixed `start_python_api_service()` to not use `communicate()` on long-running process
- Added PID logging for debugging

### `kill_processes.ps1` (NEW)
- Thoroughly kills all telegram_listener, trading_bot, and port 8000 processes
- Removes stale lock files

---

## Recommendations for Full Resolution

1. **Priority 1:** Investigate why child process (PID 7332) is spawned with `uv` Python. Check:
   - `subprocess.Popen` with `creationflags=CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS`
   - Any `sys.executable` references that might resolve to wrong Python

2. **Priority 2:** Add logging to `start_python_api_service()` to capture which Python executable is actually being invoked

3. **Priority 3:** Consider using a proper process manager (e.g., `pm2`, `supervisor`) instead of custom startup scripts

4. **Priority 4:** Once single-instance is confirmed, verify Telegram offset tracking works correctly by sending a test command and confirming it's only processed once
