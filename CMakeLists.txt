cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" REBALANCER_VERSION LIMIT_COUNT 1)
string(STRIP "${REBALANCER_VERSION}" REBALANCER_VERSION)

# Wheel-build hook: scikit-build-core invokes cmake from a tmp dir and can't
# easily be told about the getdeps-installed deps via env vars (cibuildwheel's
# CIBW_ENVIRONMENT does shell-substitution but its bashlex evaluator rejects
# common forms like `cat 2>/dev/null`). Instead, the wheels workflow's
# CIBW_BEFORE_ALL writes the colon-separated dep install paths to
# ./.cmake_prefix_path in the source tree (durable across cibuildwheel
# phases on macOS, where /tmp may be cleaned), and we splice them into
# CMAKE_PREFIX_PATH here.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.cmake_prefix_path")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.cmake_prefix_path" _wheel_prefix)
    string(STRIP "${_wheel_prefix}" _wheel_prefix)
    if(_wheel_prefix)
        string(REPLACE ":" ";" _wheel_prefix_list "${_wheel_prefix}")
        list(APPEND CMAKE_PREFIX_PATH ${_wheel_prefix_list})
        message(STATUS "Wheel build: appended ${_wheel_prefix_list} to CMAKE_PREFIX_PATH")
        # FindBoost looks at BOOST_ROOT before CMAKE_PREFIX_PATH; pluck the
        # boost-<hash> entry out so module-mode discovery hits the
        # getdeps-installed boost. Set BOOST_INCLUDEDIR / BOOST_LIBRARYDIR
        # explicitly too — even CMake 3.31's FindBoost can't auto-detect
        # the layout from BOOST_ROOT alone for some boost installs.
        foreach(_p IN LISTS _wheel_prefix_list)
            get_filename_component(_pname "${_p}" NAME)
            if(_pname MATCHES "^boost-")
                set(BOOST_ROOT "${_p}" CACHE PATH "" FORCE)
                set(BOOST_INCLUDEDIR "${_p}/include" CACHE PATH "" FORCE)
                set(BOOST_LIBRARYDIR "${_p}/lib" CACHE PATH "" FORCE)
                set(Boost_NO_SYSTEM_PATHS TRUE CACHE BOOL "" FORCE)
                message(STATUS "Wheel build: BOOST_ROOT=${BOOST_ROOT}")
                break()
            endif()
        endforeach()
    endif()
endif()

project(rebalancer VERSION ${REBALANCER_VERSION} LANGUAGES CXX)

# cmake_policy(SET CMP0022 NEW) cmake_policy(SET CMP0023 NEW)

# Use compiler ID "AppleClang" instead of "Clang" for XCode. Not setting this
# sometimes makes XCode C compiler gets detected as "Clang", even when the C++
# one is detected as "AppleClang".
cmake_policy(SET CMP0010 NEW)
cmake_policy(SET CMP0025 NEW)

# Enables CMake to set LTO on compilers other than Intel.
cmake_policy(SET CMP0069 NEW)
# Enable the policy for CMake subprojects. protobuf currently causes issues
# set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

# Prohibit in-source builds
if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
  message(FATAL_ERROR "In-source build are not supported")
endif()

if(MSVC)
    message(FATAL_ERROR
        "MSVC is not supported. Rebalancer requires a Unix-like toolchain "
        "(GCC or Clang on Linux, Apple Clang on macOS)."
    )
endif()

option(USE_GUROBI "Build with Gurobi solver" OFF)
if(USE_GUROBI)
    add_compile_definitions(REBALANCER_USE_GUROBI)
endif()

option(USE_XPRESS "Build with FICO Xpress solver" OFF)
if(USE_XPRESS)
    add_compile_definitions(REBALANCER_USE_XPRESS)
endif()

option(REBALANCER_BUILD_WEBSITE_EXAMPLES "Build website reference examples" OFF)

# Demo / example executables (Sudoku, NQueens, EightQueens, WebBalancing,
# ShardAllocation, Replayer, StandaloneSolver, etc.). They're useful for
# integration testing and to keep examples in sync with library API
# changes, but nothing ships them -- the wheel is data-only and the
# deb/rpm/bottle paths package only librebalancer + headers. CI test jobs
# should pass -DEXAMPLES=ON to keep building (and thus type/link-checking)
# them.
option(EXAMPLES "Build example/demo executables" OFF)

option(BENCHMARKS "Build benchmark executables" OFF)
option(INSTALL_BENCHMARKS "Install benchmark executables (requires BENCHMARKS=ON)" OFF)

# Set CMAKE_MODULE_PATH to include fbcode_builder CMake modules
# This must be done before find_package calls so that FBThrift's
# find_dependency(Xxhash) can find FindXxhash.cmake
list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/CMake"
    # For in-fbsource builds
    "${CMAKE_CURRENT_SOURCE_DIR}/../opensource/fbcode_builder/CMake"
    # For shipit-transformed builds
    "${CMAKE_CURRENT_SOURCE_DIR}/build/fbcode_builder/CMake"
)

# Also add to CMAKE_PREFIX_PATH for getdeps-installed dependencies
list(APPEND CMAKE_PREFIX_PATH
    # For in-fbsource builds
    "${CMAKE_CURRENT_SOURCE_DIR}/../opensource/fbcode_builder/CMake"
    # For shipit-transformed builds
    "${CMAKE_CURRENT_SOURCE_DIR}/build/fbcode_builder/CMake"
)

find_package(Boost REQUIRED COMPONENTS thread)
find_package(fmt CONFIG REQUIRED)
find_package(folly CONFIG REQUIRED)
find_package(GTest REQUIRED)
find_package(FBThrift REQUIRED)
find_package(highs CONFIG)


find_package(glog CONFIG REQUIRED)
include(GoogleTest)

set(THRIFT1 "${FBTHRIFT_COMPILER}")
# ThriftLibrary.cmake is installed alongside fbthrift headers
list(APPEND CMAKE_MODULE_PATH "${FBTHRIFT_INCLUDE_DIR}/thrift")
include(ThriftLibrary)

message(STATUS "FBTHRIFT_COMPILER: ${FBTHRIFT_COMPILER}")
message(STATUS "FBTHRIFT_INCLUDE_DIR: ${FBTHRIFT_INCLUDE_DIR}")

set(CMAKE_CXX_STANDARD
    20
    CACHE STRING
          "The C++ standard whose features are requested to build this target.")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nullability -Wno-deprecated-declarations -Wno-nullability-completeness")
# Enable Clang's compile-time thread safety analysis when available.
# The REBALANCER_GUARDED_BY / REBALANCER_EXCLUDES macros in
# common/ThreadAnnotations.h are no-ops on compilers that lack support.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wthread-safety")
endif()
# Homebrew's x86_64 folly bottle is compiled with AVX2; F14's link-time
# check requires the consumer to match. Only applies on macOS/Intel —
# Linux builds folly from source via getdeps without AVX2.
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
    if(COMPILER_SUPPORTS_AVX2)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")
    endif()
endif()

# Used to make <algopt/rebalancer/...> a viable include path.
set(THRIFT_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/thrift_out")
file(MAKE_DIRECTORY ${THRIFT_OUTPUT_DIR})
include_directories("${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" "${THRIFT_OUTPUT_DIR}")

add_definitions(-DREBALANCER_OSS_BUILD)

option(USE_HIGHS "Build with HiGHS solver" OFF)
if(highs_FOUND OR USE_HIGHS)
    add_compile_definitions(REBALANCER_USE_HIGHS)
    message(STATUS "Building Rebalancer with HiGHS support.")
else()
    message(WARNING "HiGHS not found. Building Rebalancer without HiGHS support.")
endif()

set(CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use lld linker if available for faster linking.
# lld is faster and more memory-efficient than the default ld.bfd.
find_program(LLD_LINKER ld.lld)
if(LLD_LINKER)
    add_link_options("-fuse-ld=lld")
    message(STATUS "Using lld linker for faster linking")
endif()


################################################
# Generate Thrift code
################################################


# Gather the Thrift files and generate them. This isn't the most ergonomic setup because
# we would like to be able to edit these files without rerunning cmake setup. But it will
# work for now. TODO: Fix this.
file(GLOB_RECURSE thrift_files "*.thrift")
foreach(FILE ${thrift_files})
    cmake_path(GET FILE PARENT_PATH FILE_PATH)
    cmake_path(GET FILE FILENAME FILE_NAME)

    file(RELATIVE_PATH RELATIVE_FILE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${FILE_PATH}")
    set(OUTPUT_PATH "${THRIFT_OUTPUT_DIR}/${RELATIVE_FILE_PATH}")
    file(MAKE_DIRECTORY ${OUTPUT_PATH})

    message(STATUS
        "${THRIFT1} --gen mstch_cpp2:json,include_prefix=${RELATIVE_FILE_PATH} -o ${OUTPUT_PATH} -I ${CMAKE_CURRENT_BINARY_DIR} -I ${FBTHRIFT_INCLUDE_DIR} ${FILE}"
      )

    execute_process(
        COMMAND ${THRIFT1}
          --gen "mstch_cpp2:json,include_prefix=${RELATIVE_FILE_PATH}"
          -o "${OUTPUT_PATH}"
          -I "${CMAKE_CURRENT_SOURCE_DIR}"
          -I "${CMAKE_CURRENT_BINARY_DIR}"
          -I "${FBTHRIFT_INCLUDE_DIR}"
          ${FILE}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      )
endforeach()


################################################
# Categorize source files
################################################

function(categorize_source_files INPUT_FILES TEST_FILES BENCHMARK_FILES EXAMPLE_FILES EXECUTABLE_FILES LIBRARY_FILES)
    # Clear output lists
    set(tests "")
    set(benchmarks "")
    set(examples "")
    set(executables "")
    set(libraries "")

    foreach(file ${INPUT_FILES})
        # File contains int main
        get_filename_component(filename ${file} NAME)

        string(FIND "${filename}" "test_main.cpp" is_test_main)
        if(is_test_main GREATER -1)
            message(STATUS "Skipping test_main file: ${file}")
            continue()
        endif()

        # Files ending in "Test.cpp" are test files, but test utility/helper
        # files (e.g. "TestUtils.cpp") are library files, not standalone tests.
        string(REGEX MATCH "Tests?\\.(cpp|cc)$" is_test_file "${filename}")
        if(is_test_file)
            message(STATUS "Identified test file: ${file}")
            list(APPEND tests ${file})
            continue()
        endif()

        # Files in tests/ directories are test helpers (base classes, mocks,
        # utils) that must be compiled with the test binary, not the library.
        string(FIND "${file}" "/tests/" is_in_tests_dir)
        if(is_in_tests_dir GREATER -1)
            message(STATUS "Identified test helper file: ${file}")
            list(APPEND tests ${file})
            continue()
        endif()

        string(FIND "${file}" "benchmarks/" is_in_benchmarks_dir)
        if(is_in_benchmarks_dir GREATER -1)
            message(STATUS "Identified benchmark file: ${file}")
            list(APPEND benchmarks ${file})
            continue()
        endif()

        string(FIND "${file}" "examples/" is_example)
        if(is_example GREATER -1)
            message(STATUS "Identified example file: ${file}")
            list(APPEND examples ${file})
            continue()
        endif()

        # Read file content
        file(READ ${file} file_content)

        # Check if file contains 'int main'
        string(FIND "${file_content}" "int main" has_main)

        if(has_main GREATER -1)
            # Regular executables (have int main, no other matches)
            message(STATUS "Identified executable file: ${file}")
            list(APPEND executables ${file})
        else()
            # File doesn't contain int main - it's a library file
            message(STATUS "Identified library file: ${file}")
            list(APPEND libraries ${file})
        endif()
    endforeach()

    # Set output variables in parent scope
    set(${TEST_FILES} ${tests} PARENT_SCOPE)
    set(${BENCHMARK_FILES} ${benchmarks} PARENT_SCOPE)
    set(${EXAMPLE_FILES} ${examples} PARENT_SCOPE)
    set(${EXECUTABLE_FILES} ${executables} PARENT_SCOPE)
    set(${LIBRARY_FILES} ${libraries} PARENT_SCOPE)
endfunction()

# Collect all source files
file(GLOB_RECURSE all_cpp_files "*.cpp")
message(STATUS "All source files: ${all_cpp_files}")

# Optionally exclude website reference examples (broken/WIP)
if(NOT REBALANCER_BUILD_WEBSITE_EXAMPLES)
    list(FILTER all_cpp_files EXCLUDE REGEX "examples/website/")
endif()

# Exclude the nanobind Python extension's translation unit. It pulls in
# <nanobind/nanobind.h> and is built separately below (SKBUILD path), not
# bundled into librebalancer.
list(FILTER all_cpp_files EXCLUDE REGEX
    "algopt/rebalancer/python/Bindings\\.cpp$")

# Categorize the files
categorize_source_files(
    "${all_cpp_files}"
    rebalancer_test_files
    rebalancer_benchmark_files
    rebalancer_example_files
    rebalancer_exe_files
    rebalancer_library_files
)

# Collect thrift-generated .cpp files and add to the library sources
file(GLOB_RECURSE thrift_gen_cpp_files "${THRIFT_OUTPUT_DIR}/*.cpp")
list(LENGTH thrift_gen_cpp_files thrift_gen_count)
message(STATUS "Found ${thrift_gen_count} thrift-generated .cpp files")
list(APPEND rebalancer_library_files ${thrift_gen_cpp_files})

################################################
# Create libraries and executables
################################################

function(create_executables TARGET_NAME EXECUTABLE_FILES)
    # Supports an optional NO_INSTALL flag; all other extra args are link libs.
    cmake_parse_arguments(CE "NO_INSTALL" "" "" ${ARGN})
    set(link_libraries ${CE_UNPARSED_ARGUMENTS})

    set(executable_targets "")

    foreach(exec_file ${EXECUTABLE_FILES})
        # Get executable name from filename (without extension)
        get_filename_component(exec_name ${exec_file} NAME_WE)

        # Skip if this target name already exists
        if(TARGET "${exec_name}.exe")
            message(WARNING "Target ${exec_name}.exe already exists. Skipping creation.")
            continue()
        endif()

        message(STATUS "Creating executable target: ${exec_name}.exe from source file: ${exec_file}")
        add_executable("${exec_name}.exe" ${exec_file})
        target_include_directories("${exec_name}.exe"
            PUBLIC
                ${FBTHRIFT_INCLUDE_DIR}
                ${CMAKE_CURRENT_BINARY_DIR}
                ${THRIFT_OUTPUT_DIR}
        )

        # Link against all provided libraries
        if(link_libraries)
            target_link_libraries("${exec_name}.exe" PRIVATE ${link_libraries})
        endif()

        if(NOT CE_NO_INSTALL)
            install(TARGETS "${exec_name}.exe" RUNTIME DESTINATION bin)
        endif()

        # Track this executable
        list(APPEND executable_targets "${exec_name}.exe")
    endforeach()

    # Create a custom target that builds all executables
    if(executable_targets)
        add_custom_target(${TARGET_NAME}
            DEPENDS ${executable_targets}
            COMMENT "Building all ${TARGET_NAME}"
        )

        message(STATUS "Created meta-target '${TARGET_NAME}' target with: ${executable_targets}")
    endif()
endfunction()

add_library(rebalancer SHARED ${rebalancer_library_files})
# Deps are PRIVATE: rebalancer links them but does not propagate -lfolly etc.
# to its consumers. This keeps _rebalancer.so (the Python extension) from
# acquiring direct LC_LOAD_DYLIB entries for getdeps-installed dylibs (e.g.
# @rpath/libfolly.0.58.0-dev.dylib) whose rpath isn't set in the wheel layout
# — delocate-wheel fails if it can't resolve those deps.
#
# With all deps as shared libs (BUILD_SHARED_LIBS=ON in their manifests),
# runtime consumers get folly/fbthrift/fmt/glog/gflags via the DT_NEEDED
# chain from librebalancer.so — no direct linker flags needed. The old
# gflags double-DEFINE problem that required Linux PRIVATE / macOS PUBLIC
# toggling is gone now that each dep is a single shared library.
#
# Public headers and compile defs are still exposed to consumers via the
# explicit target_include_directories / target_compile_definitions below.
target_link_libraries(rebalancer
    PRIVATE
        Folly::folly
        FBThrift::thriftprotocol
        FBThrift::thriftmetadata
        FBThrift::thrifttype
        FBThrift::rpcmetadata
        fmt::fmt
        glog::glog
)
target_include_directories(rebalancer
    PUBLIC
        $<TARGET_PROPERTY:Folly::folly,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:FBThrift::thriftprotocol,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:FBThrift::thriftmetadata,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:FBThrift::thrifttype,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:FBThrift::rpcmetadata,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:fmt::fmt,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:glog::glog,INTERFACE_INCLUDE_DIRECTORIES>
        ${FBTHRIFT_INCLUDE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${THRIFT_OUTPUT_DIR}
)
target_compile_definitions(rebalancer
    PUBLIC
        $<TARGET_PROPERTY:Folly::folly,INTERFACE_COMPILE_DEFINITIONS>
        $<TARGET_PROPERTY:FBThrift::thriftprotocol,INTERFACE_COMPILE_DEFINITIONS>
        $<TARGET_PROPERTY:FBThrift::thriftmetadata,INTERFACE_COMPILE_DEFINITIONS>
        $<TARGET_PROPERTY:FBThrift::thrifttype,INTERFACE_COMPILE_DEFINITIONS>
        $<TARGET_PROPERTY:FBThrift::rpcmetadata,INTERFACE_COMPILE_DEFINITIONS>
        $<TARGET_PROPERTY:fmt::fmt,INTERFACE_COMPILE_DEFINITIONS>
        $<TARGET_PROPERTY:glog::glog,INTERFACE_COMPILE_DEFINITIONS>
)

if(highs_FOUND OR USE_HIGHS)
    target_link_libraries(rebalancer PRIVATE highs::highs)
endif()

# Find test_main.cpp from source tree,
# path varies between in-fbsource and shipit builds.
file(GLOB_RECURSE test_main_file "*/test_main.cpp")
# Get the first match
list(GET test_main_file 0 test_main_file)
if(NOT test_main_file)
    message(FATAL_ERROR "Could not find test_main.cpp in source tree")
endif()
add_library(
    test_main
    ${test_main_file}
)
target_link_libraries(test_main
    PUBLIC
        Folly::folly
        GTest::gtest
)

if(EXAMPLES)
    # rebalancer links folly/fbthrift/fmt PRIVATELY so they don't propagate
    # to consumers automatically. Examples that directly call folly/fmt APIs
    # need them on their own link line.
    set(_example_deps
        rebalancer
        Folly::folly
        FBThrift::thriftprotocol
        FBThrift::thriftmetadata
        FBThrift::thrifttype
        FBThrift::rpcmetadata
        fmt::fmt
    )
    create_executables(rebalancer_exes "${rebalancer_exe_files}" ${_example_deps})
    create_executables(rebalancer_examples "${rebalancer_example_files}" ${_example_deps})
endif()
if(BENCHMARKS)
    # Split benchmark files into executables (have int main) and support libraries
    set(rebalancer_benchmark_exe_files "")
    set(rebalancer_benchmark_lib_files "")
    foreach(file ${rebalancer_benchmark_files})
        file(READ ${file} file_content)
        string(FIND "${file_content}" "int main" has_main)
        if(has_main GREATER -1)
            list(APPEND rebalancer_benchmark_exe_files ${file})
        else()
            list(APPEND rebalancer_benchmark_lib_files ${file})
        endif()
    endforeach()

    # Build benchmark support library (BenchmarkUtils, etc.)
    if(rebalancer_benchmark_lib_files)
        add_library(rebalancer_benchmark_support ${rebalancer_benchmark_lib_files})
        target_link_libraries(rebalancer_benchmark_support
            PUBLIC
                rebalancer
                Folly::follybenchmark
        )
        target_include_directories(rebalancer_benchmark_support
            PUBLIC
                ${FBTHRIFT_INCLUDE_DIR}
                ${CMAKE_CURRENT_BINARY_DIR}
                ${THRIFT_OUTPUT_DIR}
        )
    endif()

    set(benchmark_link_libs rebalancer Folly::follybenchmark GTest::gtest Boost::thread)
    if(rebalancer_benchmark_lib_files)
        list(APPEND benchmark_link_libs rebalancer_benchmark_support)
    endif()

    if(INSTALL_BENCHMARKS)
        create_executables(rebalancer_benchmarks "${rebalancer_benchmark_exe_files}" ${benchmark_link_libs})
    else()
        create_executables(rebalancer_benchmarks "${rebalancer_benchmark_exe_files}" NO_INSTALL ${benchmark_link_libs})
    endif()
endif()

add_executable(rebalancer_tests.exe ${rebalancer_test_files})
target_link_libraries(rebalancer_tests.exe
    PUBLIC
        rebalancer
        Folly::folly
        Folly::follybenchmark
        FBThrift::thriftprotocol
        FBThrift::thriftmetadata
        FBThrift::thrifttype
        FBThrift::rpcmetadata
        fmt::fmt
        test_main
        GTest::gtest
        GTest::gmock
        Boost::thread
)
target_include_directories(rebalancer_tests.exe
    PUBLIC
        ${FBTHRIFT_INCLUDE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${THRIFT_OUTPUT_DIR}
)
if(highs_FOUND OR USE_HIGHS)
    target_link_libraries(rebalancer_tests.exe PUBLIC highs::highs)
endif()
# rebalancer_tests.exe links against librebalancer (a shared library
# living in the build tree alongside the test binary). Tell the dynamic
# loader to look there at run time so gtest_discover_tests / ctest can
# launch the binary without a separately-installed librebalancer.
#
# Append to BOTH BUILD_RPATH and INSTALL_RPATH because getdeps sets
# CMAKE_BUILD_WITH_INSTALL_RPATH=ON on macOS, in which case the
# build-tree binary uses INSTALL_RPATH at link time. On Linux the
# build-tree binary uses BUILD_RPATH instead.
if(APPLE)
    set(_rebalancer_loader_rpath "@loader_path")
else()
    set(_rebalancer_loader_rpath "$ORIGIN")
endif()
set_property(TARGET rebalancer_tests.exe APPEND PROPERTY
    BUILD_RPATH "${_rebalancer_loader_rpath}"
)
set_property(TARGET rebalancer_tests.exe APPEND PROPERTY
    INSTALL_RPATH "${_rebalancer_loader_rpath}"
)

# Enable CTest and register tests for GitHub Actions
enable_testing()
gtest_discover_tests(rebalancer_tests.exe
  DISCOVERY_MODE PRE_TEST
  PROPERTIES TIMEOUT 120
)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(
        NAME check_benchmark_paths
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_benchmark_paths.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/algopt"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

################################################
# Install targets
################################################

if(NOT SKBUILD)
    install(TARGETS rebalancer
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )

    # Install source headers and thrift-generated headers for the dev-build
    # (deb/rpm/bottle) path. The wheel build is data-only and only needs the
    # shared library, installed below into the Python package layout.
    install(DIRECTORY algopt/ DESTINATION include/algopt
            FILES_MATCHING PATTERN "*.h")

    install(DIRECTORY ${THRIFT_OUTPUT_DIR}/algopt/ DESTINATION include/algopt
            FILES_MATCHING PATTERN "*.h" PATTERN "*.tcc")

    # Packaging smoke test — only built in the SDK artifact context.
    # Pass -DPACKAGING_TEST=ON (via build_linux_sdk.sh extra-cmake-defines)
    # to enable; off by default so normal CI builds are unaffected.
    option(PACKAGING_TEST "Build and install the packaging smoke test binary" OFF)
    if(PACKAGING_TEST)
        add_executable(test_solve tools/packages/test_solve.cpp)
        # test_solve.cpp includes rebalancer headers which transitively pull in
        # folly coro / cancellation templates that instantiate to symbols in
        # libfolly.so. With PRIVATE folly on rebalancer, cmake doesn't add
        # -lfolly to test_solve — add it explicitly so the linker can resolve
        # those symbols. build_linux_sdk.sh's patchelf pass converts the
        # resulting absolute DT_NEEDED paths to SONAMEs + $ORIGIN rpath.
        target_link_libraries(test_solve PRIVATE
            rebalancer
            Folly::folly
            FBThrift::thriftprotocol
            FBThrift::thriftmetadata
            FBThrift::thrifttype
            FBThrift::rpcmetadata
        )
        set_target_properties(test_solve PROPERTIES
            INSTALL_RPATH "\$ORIGIN/../lib:\$ORIGIN/../lib/rebalancer"
        )
        install(TARGETS test_solve RUNTIME DESTINATION bin)
    endif()
endif()

# When invoked by scikit-build-core (during a `pip install` / wheel build),
# also install librebalancer.{so,dylib} into the Python package layout so
# auditwheel/delocate can bundle it. SKBUILD is set by scikit-build-core
# only; a normal cmake invocation leaves these targets untouched.
if(SKBUILD)
    # INSTALL_RPATH_USE_LINK_PATH: include all linked library directories in the
    # installed librebalancer.{so,dylib}'s RPATH. On macOS delocate-wheel walks
    # the RPATH of every lib in the wheel to find and bundle transitive deps.
    # Without this, librebalancer.dylib's INSTALL_RPATH is empty (cmake strips
    # the build RPATH on install) and delocate can't find @rpath/libfolly.dylib.
    set_target_properties(rebalancer PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
    install(TARGETS rebalancer
        LIBRARY DESTINATION ${SKBUILD_PROJECT_NAME}/_lib
        ARCHIVE DESTINATION ${SKBUILD_PROJECT_NAME}/_lib
    )

    ############################################
    # nanobind Python extension (_rebalancer)
    ############################################
    find_package(Python 3.12 REQUIRED COMPONENTS Interpreter Development.Module)
    find_package(nanobind CONFIG REQUIRED)

    nanobind_add_module(_rebalancer
        NB_STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/algopt/rebalancer/python/Bindings.cpp
    )
    # Bindings.cpp instantiates fmt templates and folly/thrift types that
    # require link-time symbol resolution. With PRIVATE deps on rebalancer,
    # these don't propagate to _rebalancer. Use -undefined dynamic_lookup
    # (macOS) / --allow-shlib-undefined (Linux) so the linker accepts the
    # undefined folly/thrift symbols — they resolve at dlopen time through
    # _rebalancer.so's → librebalancer dependency chain which loads libfolly.
    # fmt is still linked explicitly since it's reliably findable by delocate
    # (Homebrew on Mac, bundled in lib/rebalancer/ on Linux).
    target_link_libraries(_rebalancer
        PRIVATE
            rebalancer
            fmt::fmt
    )
    if(APPLE)
        target_link_options(_rebalancer PRIVATE -undefined dynamic_lookup)
    else()
        target_link_options(_rebalancer PRIVATE -Wl,--allow-shlib-undefined)
    endif()
    # The wheel layout places _rebalancer.cpython-*.so at the package root
    # and librebalancer.{so,dylib} one level down in _lib/. Tell the dynamic
    # loader where to look so the @rpath/librebalancer.* load command in
    # _rebalancer resolves. Without this, delocate-wheel (macOS) / auditwheel
    # (Linux) fail to walk the dep tree and bundle librebalancer into the
    # wheel.
    if(APPLE)
        set(_rebalancer_ext_rpath "@loader_path/_lib")
    else()
        set(_rebalancer_ext_rpath "$ORIGIN/_lib")
    endif()
    set_property(TARGET _rebalancer APPEND PROPERTY
        BUILD_RPATH "${_rebalancer_ext_rpath}"
    )
    set_property(TARGET _rebalancer APPEND PROPERTY
        INSTALL_RPATH "${_rebalancer_ext_rpath}"
    )
    # Install the extension into the wheel package; the wrapper __init__.py
    # already lives in src/rebalancer/ (picked up via wheel.packages in
    # pyproject.toml). Ship the type stub from the in-tree binding source.
    install(TARGETS _rebalancer LIBRARY DESTINATION ${SKBUILD_PROJECT_NAME})
    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/algopt/rebalancer/python/_rebalancer.pyi
        DESTINATION ${SKBUILD_PROJECT_NAME}
    )
endif()

################################################
# Print all targets
################################################

function(print_all_targets DIR)
    get_property(TGTS DIRECTORY "${DIR}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(TGT IN LISTS TGTS)
        message(STATUS "Target: ${TGT}")
        # TODO: Do something about it
    endforeach()

    get_property(SUBDIRS DIRECTORY "${DIR}" PROPERTY SUBDIRECTORIES)
    foreach(SUBDIR IN LISTS SUBDIRS)
        print_all_targets("${SUBDIR}")
    endforeach()
endfunction()

print_all_targets(.)
