# Code Review Fixes

This document tracks the comprehensive fixes applied to address critical runtime, robustness, security, and maintainability issues in the victus-control project.

## Second Wave Fixes

### 1. Thread Lifecycle Management ✅
**Problem**: `fan_mode_trigger` created detached threads without proper management, causing potential `std::terminate()` on repeated calls.

**Solution**: 
- Implemented `FanThreadManager` class with proper thread lifecycle management
- Added stop flag (`std::atomic<bool>`) and mutex for thread coordination
- Worker threads are properly joined before starting new ones
- Added `cleanup_fan_threads()` function for graceful shutdown
- Replaced detached threads with managed thread instance

### 2. Header Include Guards ✅
**Problem**: Backend headers lacked include guards, risking multiple inclusion issues.

**Solution**: Added `#pragma once` to all backend headers:
- `backend/src/fan.hpp`
- `backend/src/util.hpp` 
- `backend/src/keyboard.hpp`

### 3. Input Validation ✅
**Problem**: Functions accepted any input without validation, risking system file corruption.

**Solution**: Created comprehensive validation utilities in `util.hpp/cpp`:
- `is_valid_fan_mode()`: Validates against {"AUTO","MANUAL","MAX"}
- `is_valid_fan_speed()`: Numeric validation with 0-6000 RPM range
- `is_valid_hex_rgb()`: 6-character hex validation 
- `is_valid_brightness()`: Numeric validation with 0-3 range
- Applied validation in all setter functions before file writes

### 4. Standardized Error Handling ✅
**Problem**: Inconsistent return patterns and mixed logging approaches.

**Solution**:
- Added `is_error()` helper utility for consistent "ERROR:" prefix checking
- Standardized all functions to return either "OK" or "ERROR: ..." 
- Implemented lightweight logging wrapper with severity levels
- Replaced scattered `std::cerr/std::cout` with structured logging

### 5. Backend Socket Hardening ✅
**Problem**: No request size limits, poor error handling, resource leaks.

**Solution**:
- Added `ClientSocketGuard` RAII wrapper for automatic socket cleanup
- Implemented protocol hardening with `MAX_COMMAND_LENGTH` (4096 bytes) limit
- Added defensive parsing with proper error responses
- Enhanced command validation and missing parameter checks
- Improved accept() error handling with EINTR support

### 6. Logging Abstraction ✅
**Problem**: Inconsistent logging made debugging difficult.

**Solution**: Implemented lightweight logging system:
```cpp
enum class LogLevel { Info, Warn, Error, Debug };
void log(LogLevel lvl, const std::string &msg);
```
- Maps to appropriate std::clog/std::cerr streams
- Used throughout modified code for consistent debugging

### 7. API Consistency ✅ 
**Problem**: Parameter naming inconsistency in function signatures.

**Solution**: 
- Changed `set_fan_mode(const std::string &value)` to `set_fan_mode(const std::string &mode)`
- Updated implementation and all callers accordingly

### 8. Frontend Robustness ✅
**Problem**: Large numeric strings could cause display issues.

**Solution**: Added defensive check in `update_fan_speeds()`:
- Treat fan speed strings longer than 8 characters as "N/A"
- Prevents UI corruption from malformed responses

### 9. Build System Improvements ✅
**Problem**: No compiler warnings enabled.

**Solution**: Added project-wide warning flags in root `meson.build`:
```meson
add_project_arguments('-Wall', '-Wextra', '-Wpedantic', language : 'cpp')
```

### 10. Constants Definition ✅
**Problem**: Magic numbers scattered throughout code.

**Solution**: Created `constants.hpp` with centralized definitions:
- `FAN_REAPPLY_SECONDS = 100`
- `MAX_COMMAND_LENGTH = 4096`
- `MAX_FAN_RPM = 6000`
- `MAX_BRIGHTNESS = 3`

### 11. Graceful Shutdown ✅
**Problem**: Server couldn't shut down cleanly, threads left running.

**Solution**:
- Added signal handlers for SIGINT/SIGTERM
- Implemented `server_running` atomic flag
- Enhanced main loop to break on shutdown signal
- Added fan thread cleanup before exit
- Proper socket cleanup and unlink on shutdown

### 12. Protocol Security ✅
**Problem**: No protection against oversized or malformed requests.

**Solution**:
- Early rejection of requests exceeding size limits
- Proper error responses for oversized requests
- Zero-length command validation
- Enhanced command parsing with missing parameter detection

## Implementation Details

### Thread Management Architecture
The new thread management uses a dedicated `FanThreadManager` structure:
- Mutex-protected operations prevent race conditions
- Atomic stop flag enables clean thread termination  
- Proper join() calls prevent resource leaks
- Worker threads check stop flag during sleep intervals

### Validation Strategy
Input validation follows defense-in-depth:
1. Format validation (length, characters)
2. Range validation (numeric bounds)
3. Early rejection with clear error messages
4. Prevents invalid data from reaching system files

### Error Handling Consistency
All functions now follow standardized patterns:
- Return "OK" on success
- Return "ERROR: <specific message>" on failure
- Use logging for internal error tracking
- Maintain backward compatibility with frontend expectations

## Testing Recommendations

- **Manual Testing**: Start server, send valid/invalid commands, verify responses
- **Shutdown Testing**: Send SIGINT, verify clean shutdown and thread cleanup
- **Load Testing**: Send multiple rapid fan mode changes, verify no crashes
- **Edge Cases**: Test with oversized requests, malformed commands, missing parameters

## Backward Compatibility

All changes maintain backward compatibility:
- Frontend continues to expect "OK"/"ERROR:" response format
- Socket protocol unchanged
- Command syntax preserved
- No breaking API changes

## Future Improvements (Out of Scope)

- Structured JSON responses
- Event-driven model replacing periodic reapply
- Full protocol redesign
- Comprehensive test suite