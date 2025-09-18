# Changelog

## Version 2.0.0 - Security and Stability Update

### Security Fixes
- **CRITICAL**: Fixed command injection vulnerability by removing shell script execution
- Added comprehensive input validation for all user inputs
- Removed passwordless sudo requirement

### Bug Fixes
- Fixed missing includes causing compilation issues
- Fixed race condition in fan control
- Fixed memory leaks in texture loading
- Fixed UI freezing during mode changes
- Fixed socket disconnection handling

### New Features
- Added comprehensive logging system
- Added automatic socket reconnection
- Added configuration management system
- Added graceful shutdown handling
- Added hwmon path caching for performance

### Improvements
- Made all UI operations asynchronous
- Reduced CPU usage when window is not visible
- Improved error messages and user feedback
- Better thread management and cleanup
- Direct sysfs writes for better reliability

### Breaking Changes
- Removed set-fan-speed.sh script
- Changed permission model (no sudo required)
- Updated udev rules format

### Technical Debt
- Centralized all constants in config.hpp
- Removed magic numbers throughout codebase
- Improved code organization and separation of concerns
- Added proper resource cleanup