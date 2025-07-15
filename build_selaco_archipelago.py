#!/usr/bin/env python3
"""
Setup script for Selaco with Archipelago integration
Configures build environment but doesn't compile - use Visual Studio or make to build
Supports Windows, Linux, and macOS
"""

import os
import sys
import subprocess
import platform
import shutil
import argparse
import json
from pathlib import Path
from typing import Optional, List, Dict, Any

class Colors:
    """ANSI color codes for colored output"""
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    MAGENTA = '\033[95m'
    CYAN = '\033[96m'
    WHITE = '\033[97m'
    BOLD = '\033[1m'
    RESET = '\033[0m'

class SelacoBuildError(Exception):
    """Custom exception for build errors"""
    pass

class SelacoSetup:
    def __init__(self, args):
        self.args = args
        self.root_dir = Path(__file__).parent.absolute()
        self.build_dir = self.root_dir / "build"
        self.vcpkg_dir = self.root_dir / "vcpkg"
        self.system = platform.system().lower()
        self.cpu_count = os.cpu_count() or 4
        
        # Platform-specific settings
        self.is_windows = self.system == "windows"
        self.is_linux = self.system == "linux"
        self.is_macos = self.system == "darwin"
        
        # Build configuration
        self.build_type = args.build_type
        self.clean = args.clean
        self.verbose = args.verbose
        self.quick = args.quick
        self.skip_checks = args.skip_checks
        
    def log(self, message: str, color: str = Colors.WHITE):
        """Print colored log message"""
        print(f"{color}{message}{Colors.RESET}")
    
    def log_error(self, message: str):
        """Print error message"""
        self.log(f"ERROR: {message}", Colors.RED)
    
    def log_success(self, message: str):
        """Print success message"""
        self.log(f"SUCCESS: {message}", Colors.GREEN)
    
    def log_info(self, message: str):
        """Print info message"""
        self.log(f"INFO: {message}", Colors.CYAN)
    
    def log_warning(self, message: str):
        """Print warning message"""
        self.log(f"WARNING: {message}", Colors.YELLOW)
    
    def run_command(self, cmd: List[str], cwd: Optional[Path] = None, check: bool = True) -> subprocess.CompletedProcess:
        """Run a command and return the result"""
        if cwd is None:
            cwd = self.root_dir
        
        if self.verbose:
            self.log_info(f"Running: {' '.join(cmd)} in {cwd}")
        
        try:
            result = subprocess.run(
                cmd,
                cwd=cwd,
                capture_output=not self.verbose,
                text=True,
                check=check
            )
            return result
        except subprocess.CalledProcessError as e:
            if not self.verbose:
                self.log_error(f"Command failed: {' '.join(cmd)}")
                if e.stdout:
                    self.log_error(f"STDOUT: {e.stdout}")
                if e.stderr:
                    self.log_error(f"STDERR: {e.stderr}")
            raise SelacoBuildError(f"Command failed: {' '.join(cmd)}")
    
    def check_prerequisites(self):
        """Check if all required tools are available"""
        self.log_info("Checking prerequisites...")
        
        if self.skip_checks:
            self.log_warning("Skipping prerequisite checks as requested")
            return
        
        required_tools = []
        
        # Check for CMake
        try:
            result = self.run_command(["cmake", "--version"], check=False)
            if result.returncode == 0:
                self.log_success("CMake found")
            else:
                required_tools.append("cmake")
        except FileNotFoundError:
            required_tools.append("cmake")
        
        # Check for Git
        try:
            result = self.run_command(["git", "--version"], check=False)
            if result.returncode == 0:
                self.log_success("Git found")
            else:
                required_tools.append("git")
        except FileNotFoundError:
            required_tools.append("git")
        
        # Platform-specific build tool checks
        if self.is_windows:
            # Check for MSBuild (more reliable than cl on Windows)
            msbuild_found = False
            try:
                result = self.run_command(["msbuild", "-version"], check=False)
                if result.returncode == 0:
                    self.log_success("MSBuild found")
                    msbuild_found = True
            except FileNotFoundError:
                pass
            
            # Check for Visual Studio compiler (optional since we're not compiling)
            try:
                result = self.run_command(["cl"], check=False)
                if result.returncode != 0:  # cl returns non-zero when no input files
                    self.log_success("Visual Studio compiler found")
                else:
                    self.log_info("Visual Studio compiler not found in PATH (this is okay)")
            except FileNotFoundError:
                self.log_info("Visual Studio compiler not found in PATH (this is okay)")
            
            # Only require MSBuild if we can't find it
            if not msbuild_found:
                self.log_warning("MSBuild not found - Visual Studio may not be properly installed")
                self.log_info("You can still continue, but you'll need Visual Studio to build")
        else:
            # Check for GCC/Clang on Unix systems
            gcc_found = False
            clang_found = False
            
            try:
                result = self.run_command(["gcc", "--version"], check=False)
                if result.returncode == 0:
                    self.log_success("GCC found")
                    gcc_found = True
            except FileNotFoundError:
                pass
            
            try:
                result = self.run_command(["clang", "--version"], check=False)
                if result.returncode == 0:
                    self.log_success("Clang found")
                    clang_found = True
            except FileNotFoundError:
                pass
            
            if not gcc_found and not clang_found:
                required_tools.append("gcc or clang")
            
            # Check for Make/Ninja on Unix systems
            make_found = False
            ninja_found = False
            
            try:
                result = self.run_command(["make", "--version"], check=False)
                if result.returncode == 0:
                    self.log_success("Make found")
                    make_found = True
            except FileNotFoundError:
                pass
            
            try:
                result = self.run_command(["ninja", "--version"], check=False)
                if result.returncode == 0:
                    self.log_success("Ninja found")
                    ninja_found = True
            except FileNotFoundError:
                pass
            
            if not make_found and not ninja_found:
                required_tools.append("make or ninja")
        
        if required_tools:
            self.log_error("Missing required tools:")
            for tool in required_tools:
                self.log_error(f"  - {tool}")
            
            # Provide installation instructions
            self.log_info("\nInstallation instructions:")
            if self.is_windows:
                self.log_info("Windows:")
                self.log_info("  - Install Visual Studio 2019 or 2022 with C++ development tools")
                self.log_info("  - Make sure to include 'MSBuild' and 'Windows SDK' components")
                self.log_info("  - Or install Visual Studio Build Tools (standalone)")
                self.log_info("  - Install Git from https://git-scm.com/")
                self.log_info("  - Install CMake from https://cmake.org/download/")
                self.log_info("\nAlternatively, you can use --skip-checks to continue anyway:")
                self.log_info("  python build_selaco_archipelago.py --skip-checks")
            elif self.is_linux:
                self.log_info("Linux (Ubuntu/Debian):")
                self.log_info("  sudo apt update")
                self.log_info("  sudo apt install build-essential cmake git pkg-config")
                self.log_info("  sudo apt install libasound2-dev libpulse-dev")
                self.log_info("\nLinux (CentOS/RHEL):")
                self.log_info("  sudo yum groupinstall 'Development Tools'")
                self.log_info("  sudo yum install cmake git")
            elif self.is_macos:
                self.log_info("macOS:")
                self.log_info("  xcode-select --install")
                self.log_info("  brew install cmake git")
            
            if not self.skip_checks:
                raise SelacoBuildError("Missing required tools")
        
        self.log_success("All prerequisites found")
    
    def setup_vcpkg(self):
        """Set up vcpkg for dependency management"""
        self.log_info("Setting up vcpkg...")
        
        if not self.vcpkg_dir.exists():
            self.log_info("Cloning vcpkg...")
            self.run_command([
                "git", "clone", "https://github.com/Microsoft/vcpkg.git", str(self.vcpkg_dir)
            ])
        else:
            self.log_info("vcpkg already exists, updating...")
            self.run_command(["git", "pull"], cwd=self.vcpkg_dir)
        
        # Build vcpkg
        bootstrap_script = "bootstrap-vcpkg.bat" if self.is_windows else "bootstrap-vcpkg.sh"
        bootstrap_path = self.vcpkg_dir / bootstrap_script
        
        if not bootstrap_path.exists():
            raise SelacoBuildError(f"vcpkg bootstrap script not found: {bootstrap_path}")
        
        self.log_info("Building vcpkg...")
        if self.is_windows:
            self.run_command([str(bootstrap_path)], cwd=self.vcpkg_dir)
        else:
            self.run_command(["bash", str(bootstrap_path)], cwd=self.vcpkg_dir)
        
        self.log_success("vcpkg setup complete")
    
    def install_dependencies(self):
        """Install required dependencies via vcpkg"""
        self.log_info("Installing dependencies...")
        
        vcpkg_exe = "vcpkg.exe" if self.is_windows else "vcpkg"
        vcpkg_path = self.vcpkg_dir / vcpkg_exe
        
        if not vcpkg_path.exists():
            raise SelacoBuildError(f"vcpkg executable not found: {vcpkg_path}")
        
        # Create vcpkg.json manifest file
        self.create_vcpkg_manifest()
        
        # Create vcpkg-configuration.json file for triplet specification
        self.create_vcpkg_configuration()
        
        # Install dependencies using manifest mode
        self.log_info("Installing dependencies from manifest...")
        self.run_command([
            str(vcpkg_path), "install"
        ], cwd=self.root_dir)
        
        self.log_success("All dependencies installed")
    
    def create_vcpkg_configuration(self):
        """Create vcpkg-configuration.json file for triplet specification"""
        config_path = self.root_dir / "vcpkg-configuration.json"
        
        # Check if configuration already exists
        if config_path.exists():
            self.log_info("vcpkg-configuration.json already exists, skipping creation")
            return
        
        # Determine triplet
        if self.is_windows:
            triplet = "x64-windows"
        elif self.is_linux:
            triplet = "x64-linux"
        elif self.is_macos:
            triplet = "x64-osx"
        else:
            triplet = "x64-linux"  # Default fallback
        
        config = {
            "default-triplet": triplet,
            "registries": [
                {
                    "kind": "git",
                    "repository": "https://github.com/Microsoft/vcpkg",
                    "baseline": "master",
                    "packages": ["*"]
                }
            ]
        }
        
        try:
            with open(config_path, 'w') as f:
                json.dump(config, f, indent=2)
            
            self.log_success(f"vcpkg configuration created: {config_path}")
            
        except Exception as e:
            self.log_warning(f"Failed to create vcpkg configuration: {e}")
            # This is not critical, continue without it
    
    def create_vcpkg_manifest(self):
        """Create vcpkg.json manifest file for dependencies"""
        self.log_info("Creating vcpkg manifest...")
        
        manifest_path = self.root_dir / "vcpkg.json"
        
        # Check if manifest already exists
        if manifest_path.exists():
            self.log_info("vcpkg.json already exists, skipping creation")
            return
        
        try:
            # Get the current vcpkg baseline (commit hash)
            result = self.run_command(["git", "rev-parse", "HEAD"], cwd=self.vcpkg_dir, check=False)
            baseline = result.stdout.strip() if result.returncode == 0 else None
            
            # Create vcpkg.json manifest
            manifest = {
                "name": "selaco-archipelago",
                "version": "1.0.0",
                "description": "Selaco with Archipelago multiworld randomizer integration",
                "dependencies": [
                    "rapidjson",
                    "zlib",
                    "bzip2",
                    "libjpeg-turbo",
                    "libpng",
                    "openal-soft",
                    "libvorbis",
                    "libflac",
                    "libsndfile",
                    "mpg123"
                ]
            }
            
            # Add baseline if we got it
            if baseline:
                manifest["builtin-baseline"] = baseline
            
            with open(manifest_path, 'w') as f:
                json.dump(manifest, f, indent=2)
            
            self.log_success(f"vcpkg manifest created: {manifest_path}")
            
        except Exception as e:
            self.log_error(f"Failed to create vcpkg manifest: {e}")
            # Fall back to a simple manifest without baseline
            manifest = {
                "name": "selaco-archipelago",
                "version": "1.0.0",
                "dependencies": [
                    "rapidjson",
                    "zlib",
                    "bzip2",
                    "libjpeg-turbo",
                    "libpng",
                    "openal-soft",
                    "libvorbis",
                    "libflac",
                    "libsndfile",
                    "mpg123"
                ]
            }
            
            with open(manifest_path, 'w') as f:
                json.dump(manifest, f, indent=2)
            
            self.log_success(f"vcpkg manifest created (without baseline): {manifest_path}")
    
    def setup_zmusic(self):
        """Set up ZMusic library"""
        self.log_info("Setting up ZMusic...")
        
        zmusic_dir = self.root_dir / "libraries" / "zmusic"
        
        if not zmusic_dir.exists():
            zmusic_dir.parent.mkdir(parents=True, exist_ok=True)
            self.log_info("Cloning ZMusic...")
            self.run_command([
                "git", "clone", "https://github.com/coelckers/ZMusic.git", str(zmusic_dir)
            ])
        else:
            self.log_info("ZMusic already exists, updating...")
            self.run_command(["git", "pull"], cwd=zmusic_dir)
        
        # Build ZMusic
        zmusic_build_dir = zmusic_dir / "build"
        if self.clean and zmusic_build_dir.exists():
            shutil.rmtree(zmusic_build_dir)
        
        zmusic_build_dir.mkdir(parents=True, exist_ok=True)
        
        # Configure ZMusic
        cmake_args = [
            "cmake", "..",
            f"-DCMAKE_BUILD_TYPE={self.build_type}",
            f"-DCMAKE_TOOLCHAIN_FILE={self.vcpkg_dir}/scripts/buildsystems/vcpkg.cmake"
        ]
        
        if self.is_windows:
            cmake_args.extend(["-A", "x64"])
        
        self.run_command(cmake_args, cwd=zmusic_build_dir)
        
        # Build ZMusic
        build_args = ["cmake", "--build", ".", "--config", self.build_type]
        if not self.is_windows:
            build_args.extend(["--", f"-j{self.cpu_count}"])
        
        self.run_command(build_args, cwd=zmusic_build_dir)
        
        self.log_success("ZMusic setup complete")
    
    def configure_cmake(self):
        """Configure CMake for Selaco build"""
        self.log_info("Configuring CMake...")
        
        if self.clean and self.build_dir.exists():
            shutil.rmtree(self.build_dir)
        
        # Clean vcpkg files if doing clean build
        if self.clean:
            vcpkg_manifest = self.root_dir / "vcpkg.json"
            if vcpkg_manifest.exists():
                vcpkg_manifest.unlink()
            
            vcpkg_config = self.root_dir / "vcpkg-configuration.json"
            if vcpkg_config.exists():
                vcpkg_config.unlink()
        
        self.build_dir.mkdir(parents=True, exist_ok=True)
        
        # CMake configuration
        cmake_args = [
            "cmake", "..",
            f"-DCMAKE_BUILD_TYPE={self.build_type}",
            "-DARCHIPELAGO_INTEGRATION=ON"
        ]
        
        # Add vcpkg toolchain if available
        vcpkg_toolchain = self.vcpkg_dir / "scripts" / "buildsystems" / "vcpkg.cmake"
        if vcpkg_toolchain.exists():
            cmake_args.append(f"-DCMAKE_TOOLCHAIN_FILE={vcpkg_toolchain}")
        elif not self.quick:
            self.log_warning("vcpkg toolchain not found, dependencies may not be available")
        
        # Platform-specific settings
        if self.is_windows:
            cmake_args.extend(["-A", "x64"])
        
        # Add ZMusic path
        zmusic_dir = self.root_dir / "libraries" / "zmusic"
        if zmusic_dir.exists():
            cmake_args.append(f"-DZMUSIC_ROOT={zmusic_dir}")
        
        self.run_command(cmake_args, cwd=self.build_dir)
        
        self.log_success("CMake configuration complete")
    
    def setup_build_environment(self):
        """Set up build environment without compiling"""
        self.log_info("Build environment setup complete!")
        self.log_info("Project configured and ready for compilation.")
        
        if self.is_windows:
            # Find Visual Studio solution file
            sln_files = list(self.build_dir.glob("*.sln"))
            if sln_files:
                self.log_success(f"Visual Studio solution created: {sln_files[0].name}")
            else:
                self.log_info("Visual Studio project files should be in the build directory")
        else:
            self.log_info("Makefiles generated and ready for compilation")
        
        self.log_info("Skipping compilation - you can now build manually")
    
    def create_config_file(self):
        """Create Archipelago configuration file"""
        self.log_info("Creating Archipelago configuration file...")
        
        config_content = """# Archipelago Configuration for Selaco
# This file is automatically generated by the build script

[connection]
# Server connection settings
server_address = archipelago.gg
server_port = 38281
slot_name = Player1
password = 

[game]
# Game-specific settings
death_link = false
auto_connect = false
debug_mode = false

[display]
# HUD and display settings
show_status_hud = true
show_item_notifications = true
show_location_notifications = true
hud_position_x = 10
hud_position_y = 10

[audio]
# Audio notification settings
item_received_sound = true
location_checked_sound = true
connection_sound = true
"""
        
        config_path = self.root_dir / "archipelago.cfg"
        with open(config_path, 'w') as f:
            f.write(config_content)
        
        self.log_success(f"Configuration file created: {config_path}")
    
    def create_launch_script(self):
        """Create launch script for easy testing"""
        self.log_info("Creating launch script...")
        
        if self.is_windows:
            # Windows batch script
            script_content = f"""@echo off
echo Starting Selaco with Archipelago integration...
echo.

cd /d "{self.root_dir}"

REM Set environment variables
set SELACO_CONFIG_PATH={self.root_dir}
set ARCHIPELAGO_CONFIG_PATH={self.root_dir}\\archipelago.cfg

REM Launch Selaco
if exist "build\\src\\{self.build_type}\\selaco.exe" (
    echo Launching Selaco...
    "build\\src\\{self.build_type}\\selaco.exe" %*
) else if exist "build\\src\\selaco.exe" (
    echo Launching Selaco...
    "build\\src\\selaco.exe" %*
) else (
    echo ERROR: Selaco executable not found!
    echo You need to build the project first.
    echo Open the Visual Studio solution in the build directory and build it.
    pause
    exit /b 1
)
"""
            script_path = self.root_dir / "launch_selaco.bat"
        else:
            # Unix shell script
            script_content = f"""#!/bin/bash
echo "Starting Selaco with Archipelago integration..."
echo

cd "{self.root_dir}"

# Set environment variables
export SELACO_CONFIG_PATH="{self.root_dir}"
export ARCHIPELAGO_CONFIG_PATH="{self.root_dir}/archipelago.cfg"

# Launch Selaco
if [ -f "build/src/selaco" ]; then
    echo "Launching Selaco..."
    ./build/src/selaco "$@"
else
    echo "ERROR: Selaco executable not found!"
    echo "You need to build the project first."
    echo "Run: cd build && make -j$(nproc)"
    exit 1
fi
"""
            script_path = self.root_dir / "launch_selaco.sh"
        
        with open(script_path, 'w') as f:
            f.write(script_content)
        
        if not self.is_windows:
            os.chmod(script_path, 0o755)
        
        self.log_success(f"Launch script created: {script_path}")
    
    def print_summary(self):
        """Print setup summary"""
        self.log(f"\n{Colors.BOLD}=== BUILD SETUP SUMMARY ==={Colors.RESET}")
        self.log_success("Selaco with Archipelago integration setup complete!")
        
        self.log(f"\n{Colors.BOLD}Setup Details:{Colors.RESET}")
        self.log_info(f"Build Type: {self.build_type}")
        self.log_info(f"Platform: {platform.system()} {platform.machine()}")
        self.log_info(f"Build Directory: {self.build_dir}")
        
        self.log(f"\n{Colors.BOLD}Files Created:{Colors.RESET}")
        self.log_info(f"Configuration: {self.root_dir}/archipelago.cfg")
        
        # Check for vcpkg files
        vcpkg_manifest = self.root_dir / "vcpkg.json"
        if vcpkg_manifest.exists():
            self.log_info(f"vcpkg Manifest: {vcpkg_manifest}")
        
        vcpkg_config = self.root_dir / "vcpkg-configuration.json"
        if vcpkg_config.exists():
            self.log_info(f"vcpkg Configuration: {vcpkg_config}")
        
        if self.is_windows:
            self.log_info(f"Launch Script: {self.root_dir}/launch_selaco.bat")
            sln_files = list(self.build_dir.glob("*.sln"))
            if sln_files:
                self.log_info(f"Visual Studio Solution: {self.build_dir}/{sln_files[0].name}")
        else:
            self.log_info(f"Launch Script: {self.root_dir}/launch_selaco.sh")
            self.log_info(f"Makefiles: {self.build_dir}/Makefile")
        
        self.log(f"\n{Colors.BOLD}How to Build:{Colors.RESET}")
        if self.is_windows:
            self.log_info("1. Open Visual Studio")
            if list(self.build_dir.glob("*.sln")):
                sln_file = list(self.build_dir.glob("*.sln"))[0]
                self.log_info(f"2. Open solution: {sln_file}")
            else:
                self.log_info(f"2. Open solution file in: {self.build_dir}")
            self.log_info("3. Select your build configuration (Debug/Release)")
            self.log_info("4. Build → Build Solution (or F7)")
        else:
            self.log_info(f"1. cd {self.build_dir}")
            self.log_info(f"2. make -j{self.cpu_count}")
            self.log_info("   or")
            self.log_info(f"   cmake --build . --config {self.build_type} -j{self.cpu_count}")
        
        self.log(f"\n{Colors.BOLD}Console Commands (after building):{Colors.RESET}")
        self.log_info("ap_connect <server> <port> <slot> [password] - Connect to Archipelago server")
        self.log_info("ap_status - Show connection status")
        self.log_info("ap_items - List received items")
        self.log_info("ap_chat <message> - Send chat message")
        
        self.log(f"\n{Colors.BOLD}Next Steps:{Colors.RESET}")
        self.log_info("1. Build the project using your preferred method (Visual Studio/make)")
        self.log_info("2. Edit archipelago.cfg with your server details")
        self.log_info("3. Run the launch script or executable to start Selaco")
        self.log_info("4. Use console commands to connect to Archipelago")
        self.log_info("5. Join a multiworld game and enjoy!")
        
        self.log(f"\n{Colors.BOLD}Note:{Colors.RESET}")
        self.log_info("This script only sets up the build environment - it doesn't compile the code.")
        self.log_info("You need to build the project manually using Visual Studio or make.")
    
    def setup(self):
        """Main setup function"""
        try:
            self.log(f"{Colors.BOLD}=== SELACO ARCHIPELAGO SETUP ==={Colors.RESET}")
            self.log_info(f"Setting up on {platform.system()} {platform.machine()}")
            self.log_info(f"Build type: {self.build_type}")
            self.log_info(f"Root directory: {self.root_dir}")
            
            self.check_prerequisites()
            
            if not self.quick:
                self.setup_vcpkg()
                self.install_dependencies()
                self.setup_zmusic()
            else:
                self.log_info("Quick setup mode: Skipping dependency setup")
                
            self.configure_cmake()
            self.setup_build_environment()
            self.create_config_file()
            self.create_launch_script()
            self.print_summary()
            
        except SelacoBuildError as e:
            self.log_error(str(e))
            return 1
        except KeyboardInterrupt:
            self.log_warning("Setup interrupted by user")
            return 1
        except Exception as e:
            self.log_error(f"Unexpected error: {e}")
            return 1
        
        return 0

def main():
    parser = argparse.ArgumentParser(
        description="Set up Selaco with Archipelago integration (configure but don't compile)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python build_selaco_archipelago.py                    # Default setup (RelWithDebInfo)
  python build_selaco_archipelago.py --build-type Release --clean
  python build_selaco_archipelago.py --verbose --clean
  python build_selaco_archipelago.py --skip-checks      # Skip prerequisite checks
  
This script sets up the build environment but doesn't compile. 
After running this, you can build in Visual Studio or with make.
  
For more information, see BUILD_ARCHIPELAGO.md
        """
    )
    parser.add_argument(
        "--build-type", 
        choices=["Debug", "Release", "RelWithDebInfo"], 
        default="RelWithDebInfo",
        help="CMake build configuration type (default: RelWithDebInfo)"
    )
    parser.add_argument(
        "--clean", 
        action="store_true",
        help="Clean build directory before setup"
    )
    parser.add_argument(
        "--verbose", 
        action="store_true",
        help="Enable verbose output for debugging"
    )
    parser.add_argument(
        "--quick", 
        action="store_true",
        help="Skip dependency setup for faster configuration (use only if dependencies are already set up)"
    )
    parser.add_argument(
        "--skip-checks", 
        action="store_true",
        help="Skip prerequisite checks (continue even if tools are missing)"
    )
    
    args = parser.parse_args()
    
    setup = SelacoSetup(args)
    return setup.setup()

if __name__ == "__main__":
    sys.exit(main())

# Create convenient aliases for the setup script
setup = main
build = main  # For backward compatibility