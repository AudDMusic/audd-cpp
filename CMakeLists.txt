cmake_minimum_required(VERSION 3.16)
project(audd-cpp VERSION 1.5.6 LANGUAGES CXX)

# ---------- options ----------
option(AUDD_CXX20           "Build with C++20 instead of C++17"        OFF)
option(AUDD_BUILD_SHARED    "Build the shared (libaudd.so) target"     ON)
option(AUDD_BUILD_STATIC    "Build the static (libaudd.a) target"      ON)
option(AUDD_BUILD_EXAMPLES  "Build runnable examples"                  OFF)
option(AUDD_BUILD_TESTS     "Build the doctest-based test suite"       OFF)
option(AUDD_INSTALL         "Generate install + CMake config rules"    ON)

if(AUDD_CXX20)
    set(CMAKE_CXX_STANDARD 20)
else()
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------- dependencies ----------
find_package(CURL REQUIRED)

# Threads (libpthread) — used by std::thread / std::async.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Vendored headers.
set(AUDD_VENDOR_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/vendor")

# ---------- public sources ----------
set(AUDD_SOURCES
    src/client.cpp
    src/streams.cpp
    src/custom_catalog.cpp
    src/advanced.cpp
    src/callback.cpp
    src/longpoll.cpp
    src/json_parse.cpp
    src/http_client.cpp
    src/md5.cpp
)

set(AUDD_PUBLIC_HEADERS
    include/audd/audd.hpp
    include/audd/client.hpp
    include/audd/recognition.hpp
    include/audd/streams.hpp
    include/audd/custom_catalog.hpp
    include/audd/advanced.hpp
    include/audd/callback.hpp
    include/audd/longpoll.hpp
    include/audd/error.hpp
    include/audd/source.hpp
    include/audd/version.hpp
)

# Internal target with shared compile flags / includes.
function(audd_apply_target tgt)
    target_include_directories(${tgt}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${AUDD_VENDOR_INCLUDE}>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(${tgt} PUBLIC CURL::libcurl Threads::Threads)
    target_compile_features(${tgt} PUBLIC
        $<IF:$<BOOL:${AUDD_CXX20}>,cxx_std_20,cxx_std_17>)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /W4)
    else()
        target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---------- shared library ----------
if(AUDD_BUILD_SHARED)
    add_library(audd SHARED ${AUDD_SOURCES})
    set_target_properties(audd PROPERTIES
        OUTPUT_NAME "audd"
        VERSION   ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        EXPORT_NAME audd
    )
    audd_apply_target(audd)
    add_library(audd::audd ALIAS audd)
endif()

# ---------- static library ----------
if(AUDD_BUILD_STATIC)
    add_library(audd_static STATIC ${AUDD_SOURCES})
    set_target_properties(audd_static PROPERTIES
        OUTPUT_NAME "audd"
        EXPORT_NAME audd_static
    )
    audd_apply_target(audd_static)
    add_library(audd::audd_static ALIAS audd_static)
endif()

# Pick a default for downstream consumers.
if(AUDD_BUILD_SHARED)
    set(AUDD_DEFAULT_TARGET audd)
elseif(AUDD_BUILD_STATIC)
    set(AUDD_DEFAULT_TARGET audd_static)
else()
    message(FATAL_ERROR "audd-cpp: enable at least one of AUDD_BUILD_SHARED / AUDD_BUILD_STATIC")
endif()

# ---------- examples ----------
if(AUDD_BUILD_EXAMPLES)
    file(GLOB AUDD_EXAMPLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp")
    foreach(src ${AUDD_EXAMPLE_SOURCES})
        get_filename_component(ex_name ${src} NAME_WE)
        add_executable(${ex_name} ${src})
        target_link_libraries(${ex_name} PRIVATE ${AUDD_DEFAULT_TARGET})
        # cpp-httplib is in vendor/cpp-httplib; expose it to examples that
        # use a callback-receiving HTTP server.
        target_include_directories(${ex_name} PRIVATE
            ${AUDD_VENDOR_INCLUDE}/cpp-httplib)
    endforeach()
endif()

# ---------- tests ----------
if(AUDD_BUILD_TESTS)
    enable_testing()
    file(GLOB AUDD_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    add_executable(audd_tests ${AUDD_TEST_SOURCES})
    target_link_libraries(audd_tests PRIVATE ${AUDD_DEFAULT_TARGET})
    target_include_directories(audd_tests PRIVATE
        ${AUDD_VENDOR_INCLUDE}/doctest
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME audd_tests COMMAND audd_tests)
endif()

# ---------- install + CMake config ----------
if(AUDD_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(DIRECTORY include/audd DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY vendor/nlohmann DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    set(AUDD_INSTALL_TARGETS)
    if(AUDD_BUILD_SHARED)
        list(APPEND AUDD_INSTALL_TARGETS audd)
    endif()
    if(AUDD_BUILD_STATIC)
        list(APPEND AUDD_INSTALL_TARGETS audd_static)
    endif()

    install(TARGETS ${AUDD_INSTALL_TARGETS}
        EXPORT auddTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(EXPORT auddTargets
        FILE auddTargets.cmake
        NAMESPACE audd::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/audd
    )

    set(_cfg "${CMAKE_CURRENT_BINARY_DIR}/auddConfig.cmake")
    file(WRITE ${_cfg} "
include(CMakeFindDependencyMacro)
find_dependency(CURL)
find_dependency(Threads)
include(\${CMAKE_CURRENT_LIST_DIR}/auddTargets.cmake)
")
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/auddConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    install(FILES
        ${_cfg}
        "${CMAKE_CURRENT_BINARY_DIR}/auddConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/audd)
endif()
