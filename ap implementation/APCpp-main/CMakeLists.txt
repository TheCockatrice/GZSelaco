cmake_minimum_required(VERSION 3.24)

project(APCpp)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(APCpp SHARED Archipelago.cpp)
set_target_properties(APCpp PROPERTIES PUBLIC_HEADER Archipelago.h)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)

# Enable TLS. On Windows, build mbedTLS
set(USE_TLS ON CACHE BOOL "Enable TLS support" FORCE)

# Fallback to mbedtls if openssl not found
if (NOT MINGW)
    find_package(OpenSSL)
    if (NOT OPENSSL_FOUND)
        set(USE_MBED_TLS ON)
    endif(NOT OPENSSL_FOUND)
endif (NOT MINGW)

if ((WIN32 AND USE_TLS) OR USE_MBED_TLS)
    set(APCPP_MBEDTLS_VER 3.6.3)
    execute_process(COMMAND ${CMAKE_COMMAND} -E remove -f mbedtls-${APCPP_MBEDTLS_VER})
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls-${APCPP_MBEDTLS_VER}.tar.bz2
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
    set(MBEDTLS_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/mbedtls-${APCPP_MBEDTLS_VER}/include)
    set(MBEDTLS_VERSION_GREATER_THAN_3 ${MBEDTLS_INCLUDE_DIRS})
    set(MBEDTLS_LIBRARY mbedtls)
    set(MBEDX509_LIBRARY mbedx509)
    set(MBEDCRYPTO_LIBRARY mbedcrypto)
    add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/mbedtls-${APCPP_MBEDTLS_VER} mbedtls_bin)
endif ((WIN32 AND USE_TLS) OR USE_MBED_TLS)

# Attempt finding system version of zlib
if (NOT MINGW)
    find_package(ZLIB)
endif (NOT MINGW)
if (MINGW OR NOT ZLIB_FOUND)
    # Force static build for zlib if not in system, and alias static to shared for IXWebsocket
    set(ZLIB_BUILD_SHARED OFF)
    include(FetchContent)
    FetchContent_Declare(
        zlib
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zlib
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(zlib)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif (MINGW OR NOT ZLIB_FOUND)

set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(IXWebSocket)
include_directories(IXWebSocket)
target_link_libraries(APCpp ixwebsocket)

if (MINGW)
    target_link_libraries(APCpp -static -static-libstdc++ -static-libgcc)
endif(MINGW)

# Attempt finding system version of JsonCpp
find_package(PkgConfig)
if (PKGCONFIG_FOUND AND (NOT MINGW) AND (NOT BUNDLED_JSONCPP))
    pkg_check_modules(JSONCPP jsoncpp)
    if (JSONCPP_FOUND)
        include_directories(${JSONCPP_INCLUDE_DIRS})
        target_link_libraries(APCpp ${JSONCPP_LDFLAGS})
    endif(JSONCPP_FOUND)
endif(PKGCONFIG_FOUND AND (NOT MINGW) AND (NOT BUNDLED_JSONCPP))

# No system version: Enable bundled
if((NOT JSONCPP_FOUND) OR BUNDLED_JSONCPP)
    set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(jsoncpp)
    set_property(TARGET jsoncpp_static PROPERTY POSITION_INDEPENDENT_CODE ON)
    include_directories(jsoncpp/include)
    target_link_libraries(APCpp jsoncpp_static)
endif((NOT JSONCPP_FOUND) OR BUNDLED_JSONCPP)

if (WIN32)
    target_link_libraries(APCpp ws2_32)
endif(WIN32)

install(TARGETS APCpp PUBLIC_HEADER)
