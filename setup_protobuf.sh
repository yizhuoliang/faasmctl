#!/usr/bin/env bash
# Robust Script to Check and Install Protocol Buffers 3.20.0

set -euo pipefail

PROTOBUF_VERSION="3.20.0"
INSTALLATION_DIR="/usr/local"
BUILD_DIR="/tmp/protobuf-build"

log() {
    echo -e "\033[1;34m[INFO]\033[0m $*"
}

error() {
    echo -e "\033[1;31m[ERROR]\033[0m $*" >&2
}

check_protobuf_version() {
    if command -v protoc &> /dev/null; then
        local version=$(protoc --version | awk '{print $2}' | sed 's/-.*//')
        echo "Found protobuf version: $version"
        if [[ "$version" == "$PROTOBUF_VERSION" ]]; then
            echo "Correct protobuf version is already installed."
            return 0
        else
            echo "Installed protobuf version ($version) doesn't match required version ($PROTOBUF_VERSION)."
            return 1
        fi
    else
        echo "protoc not found. Need to install protobuf."
        return 1
    fi
}

install_dependencies() {
    log "Installing dependencies..."
    if command -v apt-get &>/dev/null; then
        sudo apt-get install -y autoconf automake libtool curl make g++ unzip
    elif command -v yum &>/dev/null; then
        sudo yum install -y autoconf automake libtool curl make gcc-c++ unzip
    elif command -v brew &>/dev/null; then
        brew install autoconf automake libtool curl
    else
        error "Unsupported package manager. Install dependencies manually."
        exit 1
    fi
}

install_protobuf() {
    echo "Installing Protocol Buffers $PROTOBUF_VERSION..."
    
    # Install dependencies
    echo "Installing dependencies..."
    if command -v apt-get &> /dev/null; then
        sudo apt-get install -y autoconf automake libtool curl make g++ unzip
    elif command -v yum &> /dev/null; then
        sudo yum install -y autoconf automake libtool curl make gcc-c++ unzip
    elif command -v brew &> /dev/null; then
        brew install autoconf automake libtool
    else
        echo "Could not determine package manager. Please install required dependencies manually."
        exit 1
    fi
    
    # Create build directory
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Download and extract protobuf
    echo "Downloading protobuf $PROTOBUF_VERSION..."
    curl -OL "https://github.com/protocolbuffers/protobuf/archive/v$PROTOBUF_VERSION.tar.gz"
    tar -xzf "v$PROTOBUF_VERSION.tar.gz"
    cd "protobuf-$PROTOBUF_VERSION"
    
    # Build and install
    echo "Building protobuf (this may take a while)..."
    ./autogen.sh
    ./configure --prefix="$INSTALLATION_DIR"
    make -j$(nproc)
    # Skipping make check due to missing googletest in release tarball
    sudo make install
    sudo ldconfig
    
    # Clean up
    cd /
    rm -rf "$BUILD_DIR"
    
    echo "Protocol Buffers $PROTOBUF_VERSION installed successfully."
}

update_cmake_config() {
    local cmake_file="$1"

    if [ ! -f "$cmake_file" ]; then
        error "CMakeLists.txt not found at $cmake_file."
        exit 1
    fi

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
}

main() {
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

    log "Setup complete!"
}

main "$@"
