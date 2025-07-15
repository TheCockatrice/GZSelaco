# Selaco Archipelago Build Scripts

This directory contains several build scripts to make building Selaco with Archipelago integration as easy as possible.

## 📁 Available Scripts

### 🚀 **`build_and_compile.bat`** - Complete Build Script
**Use this for your first build or when you want to rebuild everything from scratch.**

- Sets up the build environment with Python script
- Downloads and configures all dependencies (vcpkg, ZMusic)
- Builds the project with MSBuild
- Creates configuration files and launch scripts

```cmd
build_and_compile.bat
```

**What it does:**
1. Runs `python build_selaco_archipelago.py --skip-checks`
2. Finds and uses MSBuild to compile the project
3. Creates `archipelago.cfg` and `launch_selaco.bat`
4. Provides helpful status messages and error handling

---

### ⚡ **`quick_build.bat`** - Fast Rebuild Script
**Use this when dependencies are already set up and you just want to rebuild quickly.**

- Skips dependency downloads and setup
- Only reconfigures and builds the project
- Much faster than the full build

```cmd
quick_build.bat
```

**What it does:**
1. Runs `python build_selaco_archipelago.py --skip-checks --quick`
2. Builds with MSBuild in Release configuration
3. Perfect for iterative development

---

### 🔧 **`build_debug.bat`** - Debug Build Script
**Use this when you want to debug the code or need debug symbols.**

- Builds with Debug configuration
- Includes full debug symbols (.pdb files)
- No optimization for easier debugging

```cmd
build_debug.bat
```

**What it does:**
1. Configures for Debug build type
2. Builds with MSBuild in Debug configuration
3. Creates debug executable with symbols

---

### 🐍 **`build_selaco_archipelago.py`** - Python Setup Script
**The core script that all batch files use. Can be run directly for more control.**

```cmd
python build_selaco_archipelago.py [options]
```

**Options:**
- `--skip-checks` - Skip prerequisite validation
- `--quick` - Skip dependency setup (faster)
- `--build-type {Debug,Release,RelWithDebInfo}` - Build configuration
- `--clean` - Clean build directory first
- `--verbose` - Show detailed output

---

## 🛠️ Prerequisites

Before running any build script, make sure you have:

### Required:
- **Python 3.7+** - Download from [python.org](https://python.org)
- **Visual Studio 2019 or 2022** - With C++ development tools
- **Git** - For downloading dependencies
- **CMake 3.16+** - Usually included with Visual Studio

### Optional but Recommended:
- **MSBuild** - Usually included with Visual Studio
- **Internet connection** - For downloading dependencies

---

## 🔄 Typical Workflow

### First Time Setup:
1. Run `build_and_compile.bat`
2. Wait for dependencies to download and build
3. Project builds automatically

### Development (Iterative):
1. Make your code changes
2. Run `quick_build.bat` for fast rebuilds
3. Test your changes

### Debugging:
1. Run `build_debug.bat` 
2. Open `build\gzdoom.sln` in Visual Studio
3. Set breakpoints and debug

---

## 📂 Output Files

After successful build, you'll find:

### Executables:
- `build\src\Release\gzdoom.exe` - Release build
- `build\src\Debug\gzdoom.exe` - Debug build (if built)

### Configuration:
- `archipelago.cfg` - Archipelago settings
- `launch_selaco.bat` - Launch script

### Project Files:
- `build\gzdoom.sln` - Visual Studio solution
- `build\*.vcxproj` - Project files

---

## 🎮 Running the Game

### Quick Start:
```cmd
launch_selaco.bat
```

### Console Commands:
Once the game is running, open the console (`~` key) and try:
```
ap_status                    # Check Archipelago status
ap_connect archipelago.gg 38281 YourSlot [password]
ap_items                     # List pending items
ap_chat Hello world!         # Send chat message
```

---

## 🔧 Troubleshooting

### "Python not found"
- Install Python 3.7+ from [python.org](https://python.org)
- Make sure Python is added to your PATH

### "MSBuild not found"
- Install Visual Studio 2019 or 2022 with C++ tools
- Or run from "Developer Command Prompt for VS"

### "Build failed"
- Check if all prerequisites are installed
- Try running `build_and_compile.bat` for a fresh build
- Look at the error messages in the output

### "Dependencies failed"
- Make sure you have internet connection
- Try running with `--clean` flag
- Check if vcpkg downloaded properly

---

## 📝 Script Details

### Build Script Options:

| Script | Speed | Use Case | Dependencies |
|--------|-------|----------|--------------|
| `build_and_compile.bat` | Slow | First build, clean rebuild | Downloads all |
| `quick_build.bat` | Fast | Development, iterative | Uses existing |
| `build_debug.bat` | Medium | Debugging, development | Uses existing |

### Configuration Types:

| Type | Optimization | Debug Info | File Size | Use Case |
|------|--------------|------------|-----------|----------|
| Debug | None | Full | Large | Development |
| Release | Full | None | Small | Distribution |
| RelWithDebInfo | Full | Partial | Medium | Testing |

---

## 🚀 Advanced Usage

### Custom Python Script Options:
```cmd
# Clean build with verbose output
python build_selaco_archipelago.py --clean --verbose

# Debug build with dependency setup
python build_selaco_archipelago.py --build-type Debug

# Quick setup only (no build)
python build_selaco_archipelago.py --skip-checks --quick
```

### Manual MSBuild:
```cmd
# Build specific configuration
MSBuild build\gzdoom.sln /p:Configuration=Release /p:Platform=x64

# Parallel build
MSBuild build\gzdoom.sln /p:Configuration=Release /p:Platform=x64 /m

# Verbose output
MSBuild build\gzdoom.sln /p:Configuration=Release /p:Platform=x64 /v:detailed
```

---

## 📄 License

These build scripts are part of the Selaco Archipelago integration project and follow the same licensing as the main project.

---

## 🤝 Contributing

If you improve these build scripts:
1. Test on both fresh and existing setups
2. Test with different Visual Studio versions
3. Update this README with any changes
4. Consider backward compatibility

---

## 📞 Support

If you encounter issues:
1. Check the troubleshooting section above
2. Try a clean build with `build_and_compile.bat`
3. Look at the console output for specific error messages
4. Make sure all prerequisites are properly installed