# Building Selaco with Archipelago Integration

This guide explains how to build Selaco with the Archipelago multiworld randomizer integration using the provided Python build script.

## Quick Start

### Cross-Platform (Python 3.7+)
```bash
# Run the Python build script
python build_selaco_archipelago.py

# Or use the simple wrapper
python build.py

# For verbose output
python build_selaco_archipelago.py --verbose
```

### Requirements
- Python 3.7 or later
- CMake 3.16 or later
- Git
- Platform-specific build tools (see Prerequisites section)

## Prerequisites

### Linux (Ubuntu/Debian)
```bash
# Install essential build tools
sudo apt-get update
sudo apt-get install build-essential git cmake nasm autoconf libtool pkg-config

# Install development libraries
sudo apt-get install libsystemd-dev libx11-dev libsdl2-dev libgtk-3-dev

# Optional: Install additional libraries for better compatibility
sudo apt-get install libgl1-mesa-dev libglu1-mesa-dev libopenal-dev
```

### Linux (Fedora/CentOS/RHEL)
```bash
# Install essential build tools
sudo dnf install gcc-c++ git cmake nasm autoconf libtool pkgconfig

# Install development libraries  
sudo dnf install systemd-devel libX11-devel SDL2-devel gtk3-devel

# Optional: Install additional libraries
sudo dnf install mesa-libGL-devel mesa-libGLU-devel openal-soft-devel
```

### macOS
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install git cmake nasm autoconf libtool pkg-config sdl2
```

### Windows
1. **Visual Studio 2019 or later** with C++ build tools
   - Download from: https://visualstudio.microsoft.com/vs/
   - Make sure to include "Desktop development with C++" workload
   - Required components: MSVC compiler, Windows SDK, CMake tools

2. **Git for Windows**
   - Download from: https://git-scm.com/download/win
   - Include Git Bash and Git GUI

3. **CMake 3.16 or later**
   - Download from: https://cmake.org/download/
   - Add to PATH during installation

## Build Script Options

The Python build script supports the following command-line options:

### `build_selaco_archipelago.py`
```bash
# Build type options
python build_selaco_archipelago.py --build-type Debug          # Debug build
python build_selaco_archipelago.py --build-type Release        # Release build
python build_selaco_archipelago.py --build-type RelWithDebInfo # Release with debug info (default)

# Utility options
python build_selaco_archipelago.py --clean          # Clean build directory first
python build_selaco_archipelago.py --verbose        # Enable verbose output
python build_selaco_archipelago.py --help           # Show help

# Examples
python build_selaco_archipelago.py --build-type Release --clean --verbose
python build.py --clean                             # Using the wrapper script
```

### Available Build Types
- **Debug**: Full debug symbols, no optimization (largest, slowest)
- **Release**: Full optimization, no debug symbols (smallest, fastest)
- **RelWithDebInfo**: Optimization with debug symbols (recommended for development)

## What the Python Build Script Does

### 1. **Dependency Management**
- Automatically downloads and sets up vcpkg package manager
- Clones and builds ZMusic dependency
- Handles platform-specific library requirements

### 2. **Verification**
- Checks that all prerequisites are installed
- Verifies Archipelago integration files are present
- Validates CMakeLists.txt includes Archipelago sources

### 3. **Build Process**
- Configures CMake with appropriate toolchain
- Builds Selaco with Archipelago integration
- Creates necessary configuration files

### 4. **Post-Build Setup**
- Creates `archipelago.cfg` configuration file
- Generates platform-specific launch scripts
- Opens build directory on completion

## Build Output

After successful build, you'll find in the `build/` directory:

### Files Created
- **`gzdoom`** or **`gzdoom.exe`** - Main Selaco executable
- **`archipelago.cfg`** - Archipelago configuration file
- **`launch_selaco.sh`** (Linux/macOS) or **`launch_selaco.bat`** (Windows) - Launch script
- **`zmusic/`** - ZMusic library build
- **`vcpkg/`** - Dependency packages

### Running Selaco
```bash
# Linux/macOS
cd build
./launch_selaco.sh

# Windows
cd build
launch_selaco.bat
```

## Archipelago Integration Usage

Once built, you can use the following console commands in Selaco:

### Connection Commands
```
ap_connect archipelago.gg 38281 YourSlotName [password]
ap_disconnect
ap_status
```

### Item and Location Commands
```
ap_items                    # List pending items
ap_process_items           # Process received items
ap_check <location_id>     # Manually check a location
```

### Chat Commands
```
ap_chat Hello everyone!    # Send chat message
```

## Configuration

Edit `build/archipelago.cfg` to customize settings:

```ini
# Server connection settings
server_address=archipelago.gg
server_port=38281

# Player identification
slot_name=YourName
password=optional_password

# Connection behavior
auto_connect=false
auto_reconnect=true
reconnect_delay=3000

# UI settings
show_notifications=true
show_chat=true
```

## Troubleshooting

### Common Issues

#### Linux: Missing Dependencies
```bash
# If you get "command not found" errors
sudo apt-get install build-essential cmake git

# If you get library errors
sudo apt-get install libsdl2-dev libgtk-3-dev libx11-dev
```

#### Windows: Visual Studio Not Found
- Make sure Visual Studio 2019+ is installed with C++ build tools
- Run the build script from "Developer Command Prompt for VS"
- Or ensure `cl.exe` is in your PATH

#### Build Fails with vcpkg Errors
```bash
# Clean and retry
python build_selaco_archipelago.py --clean

# Or manually clean vcpkg
rm -rf vcpkg
```

#### CMake Configuration Errors
- Ensure CMake version is 3.16 or later: `cmake --version`
- Check that all prerequisites are installed
- Try building with `--clean` option

### Getting Help

If you encounter issues:

1. **Check Prerequisites**: Make sure all required tools are installed
2. **Clean Build**: Try running with `--clean` option
3. **Check Logs**: Build script shows colored output for errors
4. **Platform-Specific**: Make sure you're using the correct script for your platform

## Advanced Usage

### Custom Build Types
```bash
# Debug build with symbols
python build_selaco_archipelago.py --build-type Debug

# Release with debug info (recommended for development)
python build_selaco_archipelago.py --build-type RelWithDebInfo
```

### Parallel Building
The Python script automatically detects and uses all available CPU cores for optimal build performance.

### Manual CMake
If you need to customize the build further:

```bash
cd build
cmake -S .. -B . \
    -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCUSTOM_OPTION=ON

make -j$(nproc)
```

## Next Steps

After building successfully:

1. **Test the Build**: Run `./launch_selaco.sh` to make sure the game starts
2. **Configure Archipelago**: Edit `archipelago.cfg` with your settings
3. **Join a Multiworld**: Use `ap_connect` command to join an Archipelago session
4. **Play**: Enjoy Selaco with multiworld randomizer integration!

## Contributing

If you encounter issues or want to improve the build script:

1. Check that your changes work on Windows, Linux, and macOS
2. Test with different build configurations (`--build-type Debug`, `--build-type Release`)
3. Ensure all Archipelago integration files are properly included
4. Update this README if you add new features or options

## License

This build system is part of the Selaco Archipelago integration project and follows the same licensing as the main Selaco project.