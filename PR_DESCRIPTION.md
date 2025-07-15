# Add Cross-Platform Python Build System for Selaco Archipelago Integration

## Summary

This PR introduces a comprehensive build system for Selaco with Archipelago multiworld randomizer integration, replacing the previous batch/shell scripts with a unified Python solution that works across Windows, Linux, and macOS.

## Key Changes

### 🔧 **New Python Build System**
- **`build_selaco_archipelago.py`** - Cross-platform Python build script with:
  - Automatic dependency management (vcpkg, ZMusic)
  - Prerequisites checking with helpful error messages
  - Colored output and progress indicators
  - Support for Debug, Release, and RelWithDebInfo builds
  - `--quick` mode for faster subsequent builds
  - Verbose mode for debugging

### 🏗️ **Complete Archipelago Implementation**
- **C++ Core Infrastructure**:
  - `src/archipelago/websocket_client.cpp/.h` - Cross-platform WebSocket client
  - `src/archipelago/archipelago_client.cpp/.h` - High-level Archipelago client
  - `src/archipelago/ap_definitions.cpp/.h` - Full Archipelago protocol v0.5.1 support

- **ZScript Game Integration**:
  - `wadsrc/static/zscript/archipelago.zs` - Game-facing API with:
    - `ArchipelagoManager` for connection management
    - `ArchipelagoHelpers` for game events
    - `ArchipelagoStatusHUD` for in-game display
    - `ArchipelagoConsoleCommands` for console interface

### 📚 **Comprehensive Documentation**
- **`BUILD_ARCHIPELAGO.md`** - Complete build instructions for all platforms
- **`README_BUILD.md`** - Quick reference guide
- **`archipelago_implementation_summary.md`** - Technical implementation details
- **`archipelago_integration_plan.md`** - Integration roadmap and architecture

### ⚙️ **Configuration and Setup**
- **`archipelago.cfg.example`** - Configuration template
- Updated `src/CMakeLists.txt` with Archipelago sources
- Updated `wadsrc/static/zscript.txt` to include integration

## Features

### 🌐 **Cross-Platform Support**
- Works on Windows, Linux, and macOS
- Automatic platform detection and configuration
- Platform-specific dependency handling

### 🔍 **Smart Prerequisites Checking**
- Verifies CMake, Git, and build tools
- Provides installation instructions for missing tools
- Checks for Visual Studio on Windows, GCC/Clang on Unix

### 🚀 **Optimized Build Process**
- Parallel building using all CPU cores
- Dependency caching for faster subsequent builds
- Option to skip dependency setup with `--quick` flag

### 🎮 **Game Integration**
- Console commands: `ap_connect`, `ap_status`, `ap_items`, `ap_chat`
- In-game HUD showing connection status
- Automatic item processing and location checking
- Chat integration with multiworld sessions

## Usage

```bash
# Basic build
python build_selaco_archipelago.py

# Release build with clean
python build_selaco_archipelago.py --build-type Release --clean

# Quick build (skip dependencies)
python build_selaco_archipelago.py --quick

# Verbose output for debugging
python build_selaco_archipelago.py --verbose
```

## Archipelago Protocol Support

- **Full v0.5.1 compatibility**
- Connection authentication and room management
- Location checking and item receiving
- Chat system integration
- Death link support (configurable)
- Automatic reconnection handling

## Testing

The build system has been tested with:
- **Prerequisites checking** on multiple platforms
- **Dependency resolution** via vcpkg
- **Build configuration** for different target types
- **Integration verification** with CMake and ZScript

## Breaking Changes

- Replaces previous batch/shell scripts with unified Python approach
- Requires Python 3.7+ as build dependency
- Changes build command from `./build_selaco_archipelago.sh` to `python build_selaco_archipelago.py`

## Migration Guide

**Old approach:**
```bash
# Linux/macOS
./build_selaco_archipelago.sh --release

# Windows
build_selaco_archipelago.bat --release
```

**New approach:**
```bash
# All platforms
python build_selaco_archipelago.py --build-type Release
```

## Files Changed

- ✅ **Added**: `build_selaco_archipelago.py` - Main build script
- ✅ **Added**: Complete Archipelago C++ implementation
- ✅ **Added**: ZScript game integration
- ✅ **Added**: Comprehensive documentation
- ✅ **Updated**: CMakeLists.txt with Archipelago sources
- ✅ **Updated**: ZScript includes

## Next Steps

After merging:
1. Users can build Selaco with `python build_selaco_archipelago.py`
2. Configure Archipelago settings in `archipelago.cfg`
3. Connect to multiworld sessions using in-game console commands
4. Participate in Archipelago randomizer sessions alongside other supported games

## Benefits

- **Unified Experience**: One script works on all platforms
- **Better UX**: Colored output, progress indicators, helpful error messages
- **Maintainability**: Single codebase instead of separate batch/shell scripts
- **Extensibility**: Easy to add new build options and features
- **Documentation**: Comprehensive guides for users and developers

This implementation provides a solid foundation for Selaco's participation in the Archipelago multiworld randomizer ecosystem.