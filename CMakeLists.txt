cmake_minimum_required(VERSION 3.10)
project(FaasmClientCpp VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable PIC for all targets
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_definitions(-DGOOGLE_PROTOBUF_SKIP_VERSION_CHECK=1)

include(FetchContent)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG        v2.10.4
)
FetchContent_MakeAvailable(pybind11)

find_package(Protobuf 3.20.0 EXACT REQUIRED)
find_package(CURL REQUIRED)

message(STATUS "Using Protobuf ${Protobuf_VERSION}")

# Create the src/proto directory structure if needed
set(SRC_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/proto)
set(PROTO_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/faasmctl/util/gen_proto_cpp/faabric.pb.h)
set(PROTO_HEADER_COPY ${SRC_PROTO_DIR}/faabric.pb.h)
set(PROTO_SETUP_FLAG ${CMAKE_CURRENT_BINARY_DIR}/proto_setup.flag)

add_custom_command(
    OUTPUT ${PROTO_SETUP_FLAG}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SRC_PROTO_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${PROTO_HEADER} ${PROTO_HEADER_COPY}
    COMMAND ${CMAKE_COMMAND} -E touch ${PROTO_SETUP_FLAG}
    DEPENDS ${PROTO_HEADER}
    COMMENT "Setting up protobuf include structure"
)

add_custom_target(setup_proto_includes DEPENDS ${PROTO_SETUP_FLAG})

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${Protobuf_INCLUDE_DIRS}
)

set(FAASM_CLIENT_SOURCES
    src/FaasmClient.cpp
    src/FaasmUtils.cpp
    faasmctl/util/gen_proto_cpp/faabric.pb.cc
)

add_library(faasm_client_static STATIC ${FAASM_CLIENT_SOURCES})
add_dependencies(faasm_client_static setup_proto_includes)
target_link_libraries(faasm_client_static ${Protobuf_LIBRARIES} ${CURL_LIBRARIES})

add_library(faasm_client SHARED ${FAASM_CLIENT_SOURCES})
add_dependencies(faasm_client setup_proto_includes)
target_link_libraries(faasm_client ${Protobuf_LIBRARIES} ${CURL_LIBRARIES})

pybind11_add_module(faasm_client_cpp src/FaasmPythonModule.cpp)
add_dependencies(faasm_client_cpp setup_proto_includes)
target_link_libraries(faasm_client_cpp PRIVATE faasm_client_static)