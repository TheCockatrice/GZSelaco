#!/bin/bash

#**
#** build_selaco_archipelago.sh
#** Comprehensive build script for Selaco with Archipelago integration
#**
#** This script handles all dependencies, compilation, and setup for Selaco
#** with the Archipelago multiworld randomizer integration.
#**
#** Prerequisites (Linux):
#**   - git, cmake, make, gcc/clang
#**   - nasm, autoconf, libtool, libsystemd-dev
#**   - libx11-dev, libsdl2-dev, libgtk-3-dev
#**   - build-essential, pkg-config
#**
#** Prerequisites (Windows):
#**   - Visual Studio 2019 or later
#**   - Git for Windows
#**   - CMake
#**
#**---------------------------------------------------------------------------

set -e  # Exit on any error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
ARCHIPELAGO_DIR="$SCRIPT_DIR/src/archipelago"

# Build configuration
BUILD_TYPE="Release"
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
PLATFORM="$(uname -s)"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --relwithdebinfo)
            BUILD_TYPE="RelWithDebInfo"
            shift
            ;;
        --jobs|-j)
            JOBS="$2"
            shift 2
            ;;
        --clean)
            CLEAN_BUILD=1
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  --debug              Build in Debug mode"
            echo "  --release            Build in Release mode (default)"
            echo "  --relwithdebinfo     Build with debug info"
            echo "  --jobs|-j N          Use N parallel jobs (default: $JOBS)"
            echo "  --clean              Clean build directory first"
            echo "  --help|-h            Show this help"
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

log_info "Starting Selaco with Archipelago integration build..."
log_info "Platform: $PLATFORM"
log_info "Build Type: $BUILD_TYPE"
log_info "Jobs: $JOBS"

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."
    
    local missing_deps=()
    
    # Check essential tools
    if ! command_exists git; then
        missing_deps+=("git")
    fi
    
    if ! command_exists cmake; then
        missing_deps+=("cmake")
    fi
    
    if ! command_exists make; then
        missing_deps+=("make")
    fi
    
    # Check compiler
    if ! command_exists gcc && ! command_exists clang; then
        missing_deps+=("gcc or clang")
    fi
    
    # Platform-specific checks
    if [[ "$PLATFORM" == "Linux" ]]; then
        # Check for pkg-config
        if ! command_exists pkg-config; then
            missing_deps+=("pkg-config")
        fi
        
        # Check for nasm
        if ! command_exists nasm; then
            missing_deps+=("nasm")
        fi
        
        # Check for development libraries (basic check)
        if ! pkg-config --exists x11 2>/dev/null; then
            missing_deps+=("libx11-dev")
        fi
        
        if ! pkg-config --exists sdl2 2>/dev/null; then
            missing_deps+=("libsdl2-dev")
        fi
        
        if ! pkg-config --exists gtk+-3.0 2>/dev/null; then
            missing_deps+=("libgtk-3-dev")
        fi
    fi
    
    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        log_error "Missing dependencies:"
        for dep in "${missing_deps[@]}"; do
            echo "  - $dep"
        done
        
        if [[ "$PLATFORM" == "Linux" ]]; then
            log_info "On Ubuntu/Debian, install with:"
            echo "  sudo apt-get install build-essential git cmake nasm autoconf libtool libsystemd-dev"
            echo "  sudo apt-get install libx11-dev libsdl2-dev libgtk-3-dev pkg-config"
        elif [[ "$PLATFORM" == "Darwin" ]]; then
            log_info "On macOS, install with Homebrew:"
            echo "  brew install git cmake nasm autoconf libtool pkg-config sdl2"
        fi
        
        exit 1
    fi
    
    log_success "All prerequisites found"
}

# Clean build directory
clean_build() {
    if [[ -n "$CLEAN_BUILD" ]]; then
        log_info "Cleaning build directory..."
        rm -rf "$BUILD_DIR"
    fi
}

# Create build directory
setup_build_dir() {
    log_info "Setting up build directory..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
}

# Setup vcpkg dependency manager
setup_vcpkg() {
    log_info "Setting up vcpkg..."
    
    if [[ -d "vcpkg" ]]; then
        log_info "Updating vcpkg..."
        git -C ./vcpkg pull
    else
        log_info "Cloning vcpkg..."
        git clone https://github.com/microsoft/vcpkg
    fi
    
    # Bootstrap vcpkg
    if [[ "$PLATFORM" == "Linux" ]] || [[ "$PLATFORM" == "Darwin" ]]; then
        if [[ ! -f "vcpkg/vcpkg" ]]; then
            log_info "Bootstrapping vcpkg..."
            ./vcpkg/bootstrap-vcpkg.sh
        fi
    elif [[ "$PLATFORM" == "MINGW"* ]] || [[ "$PLATFORM" == "MSYS"* ]]; then
        if [[ ! -f "vcpkg/vcpkg.exe" ]]; then
            log_info "Bootstrapping vcpkg..."
            ./vcpkg/bootstrap-vcpkg.bat
        fi
    fi
}

# Setup ZMusic dependency
setup_zmusic() {
    log_info "Setting up ZMusic..."
    
    if [[ -d "zmusic" ]]; then
        log_info "Updating ZMusic..."
        git -C ./zmusic fetch
    else
        log_info "Cloning ZMusic..."
        git clone https://github.com/zdoom/zmusic
    fi
    
    # Checkout specific version
    git -C ./zmusic checkout 1.1.12
    
    # Create build directory
    mkdir -p zmusic/build
    mkdir -p vcpkg_installed
    
    log_info "Building ZMusic..."
    cmake -S ./zmusic -B ./zmusic/build \
        -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake \
        -DVCPKG_LIBSNDFILE=1 \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DVCPKG_INSTALLED_DIR=../vcpkg_installed/
    
    make -C ./zmusic/build -j"$JOBS"
    
    log_success "ZMusic built successfully"
}

# Verify Archipelago integration
verify_archipelago() {
    log_info "Verifying Archipelago integration..."
    
    # Check if Archipelago source files exist
    local archipelago_files=(
        "websocket_client.h"
        "websocket_client.cpp"
        "archipelago_client.h"
        "archipelago_client.cpp"
        "ap_definitions.h"
        "ap_definitions.cpp"
    )
    
    for file in "${archipelago_files[@]}"; do
        if [[ ! -f "$ARCHIPELAGO_DIR/$file" ]]; then
            log_error "Missing Archipelago file: $file"
            exit 1
        fi
    done
    
    # Check if ZScript integration exists
    if [[ ! -f "$SCRIPT_DIR/wadsrc/static/zscript/archipelago.zs" ]]; then
        log_error "Missing Archipelago ZScript integration"
        exit 1
    fi
    
    # Check if CMakeLists.txt includes Archipelago sources
    if ! grep -q "archipelago/.*\.cpp" "$SCRIPT_DIR/src/CMakeLists.txt"; then
        log_error "Archipelago sources not found in CMakeLists.txt"
        exit 1
    fi
    
    log_success "Archipelago integration verified"
}

# Build Selaco with Archipelago
build_selaco() {
    log_info "Configuring Selaco build..."
    
    # Configure with CMake
    cmake -S .. -B . \
        -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DVCPKG_INSTALLED_DIR=./vcpkg_installed/ \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    
    log_info "Building Selaco with Archipelago integration..."
    make -j"$JOBS"
    
    if [[ $? -eq 0 ]]; then
        log_success "Selaco built successfully!"
    else
        log_error "Build failed!"
        exit 1
    fi
}

# Create configuration files
create_config_files() {
    log_info "Creating configuration files..."
    
    # Create Archipelago configuration
    cat > "$BUILD_DIR/archipelago.cfg" << EOF
# Archipelago Configuration for Selaco
# This file is automatically created and updated by the game

# Server connection settings
server_address=archipelago.gg
server_port=38281

# Player identification
slot_name=
password=

# Connection behavior  
auto_connect=false
auto_reconnect=true
reconnect_delay=3000

# UI settings
show_notifications=true
show_chat=true

# HUD display
# Set this in console: set ap_show_hud true/false
EOF
    
    # Create launch script
    cat > "$BUILD_DIR/launch_selaco.sh" << 'EOF'
#!/bin/bash

# Launch script for Selaco with Archipelago integration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Set library paths if needed
export LD_LIBRARY_PATH="$SCRIPT_DIR:$LD_LIBRARY_PATH"

# Launch Selaco
if [[ -f "gzdoom" ]]; then
    ./gzdoom "$@"
elif [[ -f "selaco" ]]; then
    ./selaco "$@"
else
    echo "Error: Selaco executable not found!"
    exit 1
fi
EOF
    
    chmod +x "$BUILD_DIR/launch_selaco.sh"
    
    log_success "Configuration files created"
}

# Show build summary
show_summary() {
    log_info "Build Summary:"
    echo "  Build Type: $BUILD_TYPE"
    echo "  Platform: $PLATFORM"
    echo "  Jobs Used: $JOBS"
    echo "  Build Directory: $BUILD_DIR"
    
    if [[ -f "$BUILD_DIR/gzdoom" ]]; then
        echo "  Executable: $BUILD_DIR/gzdoom"
    elif [[ -f "$BUILD_DIR/selaco" ]]; then
        echo "  Executable: $BUILD_DIR/selaco"
    fi
    
    if [[ -f "$BUILD_DIR/archipelago.cfg" ]]; then
        echo "  Archipelago Config: $BUILD_DIR/archipelago.cfg"
    fi
    
    if [[ -f "$BUILD_DIR/launch_selaco.sh" ]]; then
        echo "  Launch Script: $BUILD_DIR/launch_selaco.sh"
    fi
    
    echo ""
    log_success "Build completed successfully!"
    echo ""
    echo "To run Selaco with Archipelago integration:"
    echo "  cd $BUILD_DIR"
    echo "  ./launch_selaco.sh"
    echo ""
    echo "Archipelago console commands:"
    echo "  ap_connect <server> <port> <slot_name> [password]"
    echo "  ap_status"
    echo "  ap_items"
    echo "  ap_chat <message>"
    echo ""
}

# Main build process
main() {
    cd "$SCRIPT_DIR"
    
    check_prerequisites
    clean_build
    setup_build_dir
    setup_vcpkg
    setup_zmusic
    verify_archipelago
    build_selaco
    create_config_files
    show_summary
    
    # Open build directory on success (Linux with GUI)
    if [[ "$PLATFORM" == "Linux" ]] && command_exists xdg-open; then
        xdg-open "$BUILD_DIR"
    fi
}

# Error handling
trap 'log_error "Build failed at line $LINENO"' ERR

# Run main function
main "$@"