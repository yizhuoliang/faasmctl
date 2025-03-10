#!/usr/bin/env bash
# Comprehensive Setup Script for Faasm CPP Client Development Environment

set -euo pipefail

# Configuration
PROTOBUF_VERSION="3.20.0"
INSTALLATION_DIR="/usr/local"
BUILD_DIR="/tmp/protobuf-build"

# Text formatting
BLUE="\033[1;34m"
GREEN="\033[1;32m"
RED="\033[1;31m"
YELLOW="\033[1;33m"
RESET="\033[0m"

log() {
    echo -e "${BLUE}[INFO]${RESET} $*"
}

success() {
    echo -e "${GREEN}[SUCCESS]${RESET} $*"
}

warn() {
    echo -e "${YELLOW}[WARNING]${RESET} $*"
}

error() {
    echo -e "${RED}[ERROR]${RESET} $*" >&2
}

# Check if running with sudo/root permissions
check_permissions() {
    if [[ $EUID -ne 0 ]]; then
        if ! command -v sudo &>/dev/null; then
            error "This script requires sudo privileges. Please run as root or install sudo."
            exit 1
        fi
    fi
}

# Check installed protobuf version
check_protobuf_version() {
    if command -v protoc &>/dev/null; then
        local version=$(protoc --version | awk '{print $2}' | sed 's/-.*//')
        log "Found protobuf version: $version"
        if [[ "$version" == "$PROTOBUF_VERSION" ]]; then
            success "Correct protobuf version is already installed."
            return 0
        else
            warn "Installed protobuf version ($version) doesn't match required version ($PROTOBUF_VERSION)."
            return 1
        fi
    else
        log "protoc not found. Need to install protobuf."
        return 1
    fi
}

# Detect system and install required dependencies
install_dependencies() {
    log "Installing build dependencies..."
    
    if command -v apt-get &>/dev/null; then
        log "Detected Debian/Ubuntu system. Using apt-get."
        sudo apt-get update
        sudo apt-get install -y \
            build-essential \
            cmake \
            g++ \
            libcurl4-openssl-dev \
            libssl-dev \
            autoconf \
            automake \
            libtool \
            pkg-config \
            curl \
            make \
            unzip \
            git \
            python3-dev \
            python3-pip
            
    elif command -v yum &>/dev/null; then
        log "Detected Red Hat/CentOS system. Using yum."
        sudo yum install -y \
            gcc \
            gcc-c++ \
            cmake \
            libcurl-devel \
            openssl-devel \
            autoconf \
            automake \
            libtool \
            pkgconfig \
            curl \
            make \
            unzip \
            git \
            python3-devel
            
    elif command -v brew &>/dev/null; then
        log "Detected macOS. Using Homebrew."
        brew install \
            cmake \
            curl \
            openssl \
            autoconf \
            automake \
            libtool \
            pkg-config \
            python3
            
    else
        error "Unsupported package manager. Please install dependencies manually."
        cat << EOF
Required packages:
- build-essential (or equivalent)
- cmake
- g++ / gcc-c++
- libcurl4-openssl-dev / libcurl-devel
- libssl-dev / openssl-devel
- autoconf
- automake
- libtool
- pkg-config
- curl
- make
- unzip
- git
- python3-dev / python3-devel
EOF
        exit 1
    fi
    
    success "Dependencies installed successfully."
}

# Install Protocol Buffers
install_protobuf() {
    log "Installing Protocol Buffers $PROTOBUF_VERSION..."
    
    # Create build directory
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Download and extract protobuf
    log "Downloading protobuf $PROTOBUF_VERSION..."
    curl -OL "https://github.com/protocolbuffers/protobuf/archive/v$PROTOBUF_VERSION.tar.gz"
    tar -xzf "v$PROTOBUF_VERSION.tar.gz"
    cd "protobuf-$PROTOBUF_VERSION"
    
    # Build and install
    log "Building protobuf (this may take a while)..."
    ./autogen.sh
    ./configure --prefix="$INSTALLATION_DIR"
    make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
    # Skipping make check due to missing googletest in release tarball
    sudo make install
    sudo ldconfig 2>/dev/null || true  # ldconfig might not exist on macOS
    
    # Clean up
    cd /
    rm -rf "$BUILD_DIR"
    
    success "Protocol Buffers $PROTOBUF_VERSION installed successfully."
}

# Update CMakeLists.txt file
update_cmake_config() {
    local cmake_file="$1"

    if [ ! -f "$cmake_file" ]; then
        error "CMakeLists.txt not found at $cmake_file."
        exit 1
    fi

    log "Backing up original CMakeLists.txt to ${cmake_file}.bak"
    cp "$cmake_file" "${cmake_file}.bak"

    log "Updating CMakeLists.txt to use system protobuf."

    cat >"$cmake_file" <<EOF
cmake_minimum_required(VERSION 3.10)
project(FaasmClientCpp VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_definitions(-DGOOGLE_PROTOBUF_SKIP_VERSION_CHECK=1)

include(FetchContent)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG        v2.10.4
)
FetchContent_MakeAvailable(pybind11)

find_package(Protobuf $PROTOBUF_VERSION EXACT REQUIRED)
find_package(CURL REQUIRED)

include_directories(
    \${CMAKE_CURRENT_SOURCE_DIR}
    \${CMAKE_CURRENT_SOURCE_DIR}/../
    \${Protobuf_INCLUDE_DIRS}
)

set(FAASM_CLIENT_SOURCES
    src/FaasmClient.cpp
    src/FaasmUtils.cpp
    faasmctl/util/gen_proto_cpp/faabric.pb.cc
)

add_library(faasm_client_static STATIC \${FAASM_CLIENT_SOURCES})
target_link_libraries(faasm_client_static \${Protobuf_LIBRARIES} \${CURL_LIBRARIES})

add_library(faasm_client SHARED \${FAASM_CLIENT_SOURCES})
target_link_libraries(faasm_client \${Protobuf_LIBRARIES} \${CURL_LIBRARIES})

pybind11_add_module(faasm_client_cpp src/FaasmPythonModule.cpp)
target_link_libraries(faasm_client_cpp PRIVATE faasm_client_static)
EOF

    success "CMakeLists.txt updated successfully."
}

# Configure Python environment
setup_python_env() {
    log "Setting up Python environment..."
    
    # Make sure pip is available
    if ! command -v pip3 &>/dev/null && ! command -v pip &>/dev/null; then
        warn "pip not found, attempting to install it"
        if command -v apt-get &>/dev/null; then
            sudo apt-get install -y python3-pip
        elif command -v yum &>/dev/null; then
            sudo yum install -y python3-pip
        fi
    fi
    
    # Install required Python packages
    PIP_CMD=$(command -v pip3 || command -v pip)
    sudo $PIP_CMD install --upgrade pip
    sudo $PIP_CMD install wheel setuptools
    
    # Create a basic setup.py if it doesn't exist
    if [ ! -f "setup.py" ]; then
        log "Creating a basic setup.py file"
        cat > "setup.py" <<EOF
from setuptools import setup, find_packages

setup(
    name="faasmctl",
    version="0.1.0",
    packages=find_packages(),
    install_requires=[
        "protobuf==$PROTOBUF_VERSION",
    ],
)
EOF
        success "Created setup.py file"
    fi
    
    success "Python environment setup complete."
}

# Build the project
build_project() {
    log "Building FaasmClient project..."
    
    # Create build directory
    mkdir -p build
    cd build
    
    # Run CMake
    cmake ..
    
    # Build
    make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
    
    cd ..
    
    success "Project built successfully!"
}

# Main function
main() {
    log "Starting Faasm Client setup script"
    
    check_permissions
    install_dependencies
    
    if ! check_protobuf_version; then
        install_protobuf
        check_protobuf_version || { error "Installation verification failed!"; exit 1; }
    fi

    if [ -f "CMakeLists.txt" ]; then
        update_cmake_config "CMakeLists.txt"
    else
        error "CMakeLists.txt not found. Run this script from the project root directory."
        exit 1
    fi
    
    setup_python_env
    
    log "Would you like to build the project now? [y/N]"
    read -r response
    if [[ "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
        build_project
    else
        log "Skipping build step."
        log "To build the project later, run: mkdir -p build && cd build && cmake .. && make"
    fi

    success "Faasm Client setup complete!"
    log "To run the example, execute: python3 examples/example.py"
}

main "$@"