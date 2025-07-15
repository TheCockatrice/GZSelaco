# Selaco Archipelago Build Script

A cross-platform Python build script for building Selaco with Archipelago integration.

## Quick Start

```bash
# Basic build
python build_selaco_archipelago.py

# Or use the wrapper
python build.py

# With options
python build_selaco_archipelago.py --build-type Release --clean --verbose
```

## Requirements

- Python 3.7+
- CMake 3.16+
- Git
- Platform-specific build tools (Visual Studio on Windows, GCC/Clang on Linux/macOS)

## Options

- `--build-type {Debug,Release,RelWithDebInfo}`: Build configuration (default: RelWithDebInfo)
- `--clean`: Clean build directory before building
- `--verbose`: Show detailed build output
- `--help`: Show help message

## What It Does

1. **Checks Prerequisites**: Verifies all required tools are installed
2. **Sets Up Dependencies**: Downloads and builds vcpkg, ZMusic, and other dependencies
3. **Configures Build**: Sets up CMake with proper toolchain and options
4. **Builds Selaco**: Compiles the game with Archipelago integration
5. **Creates Config**: Generates `archipelago.cfg` and launch scripts

## Output

After successful build:
- `build/src/selaco` (or `selaco.exe` on Windows) - Game executable
- `archipelago.cfg` - Configuration file
- `launch_selaco.sh` (or `.bat` on Windows) - Launch script

## Troubleshooting

- **Missing tools**: Install CMake, Git, and build tools for your platform
- **Build fails**: Try `--clean` option
- **vcpkg errors**: Delete `vcpkg` directory and rebuild
- **Need help**: Run with `--verbose` for detailed output

## Platform Notes

### Windows
- Requires Visual Studio 2019+ with C++ tools
- Run from Developer Command Prompt if compiler not found

### Linux
- Install build-essential, cmake, git packages
- May need additional dev libraries (libsdl2-dev, etc.)

### macOS
- Install Xcode Command Line Tools
- Use Homebrew for dependencies

For detailed instructions, see `BUILD_ARCHIPELAGO.md`.