cmake_minimum_required(VERSION 3.15...3.25)
project(
  "${SKBUILD_PROJECT_NAME}"
  LANGUAGES CXX C
  VERSION "${SKBUILD_PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17 CACHE STRING "The C++ standard to build with")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SPHERELY_CODE_COVERAGE "Enable coverage reporting" OFF)

# Dependencies

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/third_party/cmake")

find_package(
  Python
  COMPONENTS Interpreter Development.Module
  REQUIRED)

find_package(pybind11 CONFIG REQUIRED)

find_package(s2 CONFIG REQUIRED)
if(s2_FOUND)
  get_target_property(s2_INCLUDE_DIRS s2::s2 INTERFACE_INCLUDE_DIRECTORIES)
  message(STATUS "Found s2: ${s2_INCLUDE_DIRS}")
else()
  message(FATAL_ERROR "Couldn't find s2")
endif()

# this is needed so that openssl headers included from s2geometry headers are found.
find_package(OpenSSL REQUIRED)
target_include_directories(s2::s2 INTERFACE ${OPENSSL_INCLUDE_DIR})

find_package(s2geography CONFIG REQUIRED)
if(${s2geography_FOUND})
  get_target_property(s2geography_INCLUDE_DIRS s2geography INTERFACE_INCLUDE_DIRECTORIES)
  message(STATUS "Found s2geography v${s2geography_VERSION}: ${s2geography_INCLUDE_DIRS}")
else()
  message(FATAL_ERROR "Couldn't find s2geography")
endif()

# geoarrow-c: used directly (i.e. not through s2geography) for its GeoArrow-C WKB
# reader and WKT writer, which are not exposed through s2geography's public
# headers. The 'geoarrow-c' conda package ships the library as C sources
# alongside its Python bindings rather than as a conventional
# installed/CMake-discoverable C library, so we locate and compile the handful
# of sources we need directly.
execute_process(
  COMMAND "${Python_EXECUTABLE}" -c "import geoarrow.c, os; print(os.path.dirname(geoarrow.c.__file__))"
  OUTPUT_VARIABLE GEOARROW_C_PACKAGE_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE GEOARROW_C_FIND_RESULT)
if(NOT GEOARROW_C_FIND_RESULT EQUAL 0)
  message(FATAL_ERROR "Couldn't locate the 'geoarrow-c' Python package (needed for its "
                      "GeoArrow-C library sources). Please install the 'geoarrow-c' conda "
                      "package in the build environment.")
endif()
message(STATUS "Found geoarrow-c sources: ${GEOARROW_C_PACKAGE_DIR}")

set(GEOARROW_C_SOURCE_DIR "${GEOARROW_C_PACKAGE_DIR}/geoarrow")

add_library(geoarrow_wkb STATIC
  "${GEOARROW_C_SOURCE_DIR}/wkb_reader.c"
  "${GEOARROW_C_SOURCE_DIR}/wkt_writer.c"
  "${GEOARROW_C_SOURCE_DIR}/geometry.c"
  "${GEOARROW_C_SOURCE_DIR}/util.c"
  "${GEOARROW_C_SOURCE_DIR}/visitor.c"
  "${GEOARROW_C_SOURCE_DIR}/double_print.c"
  "${GEOARROW_C_SOURCE_DIR}/ryu/d2s.c"
  "${GEOARROW_C_SOURCE_DIR}/nanoarrow/nanoarrow.c")
target_include_directories(geoarrow_wkb PUBLIC "${GEOARROW_C_PACKAGE_DIR}" "${GEOARROW_C_SOURCE_DIR}")
set_target_properties(geoarrow_wkb PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  C_VISIBILITY_PRESET hidden
  POSITION_INDEPENDENT_CODE ON)

if(SPHERELY_CODE_COVERAGE)
  message(STATUS "Building spherely with coverage enabled")
  add_library(coverage_config INTERFACE)
endif()

# Compile definitions and flags

if (MSVC)
  # used in s2geometry's CMakeLists.txt but not defined in target
  # TODO: move this in FindS2.cmake?
  target_compile_definitions(s2::s2 INTERFACE _USE_MATH_DEFINES)
  target_compile_definitions(s2::s2 INTERFACE NOMINMAX)
  target_compile_options(s2::s2 INTERFACE /J)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR
    CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR
    (CMAKE_CXX_COMPILER_ID MATCHES "Intel" AND NOT WIN32))
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wextra -Wreorder")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunused-variable -Wunused-parameter")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wconversion -Wold-style-cast -Wsign-conversion")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  add_definitions(-D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /MP /bigobj /J")
  set(CMAKE_EXE_LINKER_FLAGS /MANIFEST:NO)
endif()

# Build

set(CPP_SOURCES
  src/accessors-geog.cpp
  src/boolean-operations.cpp
  src/creation.cpp
  src/geography.cpp
  src/io.cpp
  src/predicates.cpp
  src/spherely.cpp
  src/wkb_geoarrow.cpp)

if(${s2geography_VERSION} VERSION_GREATER_EQUAL "0.2.0")
    set(CPP_SOURCES ${CPP_SOURCES} src/geoarrow.cpp src/projections.cpp)
endif()

add_library(spherely MODULE ${CPP_SOURCES})

target_compile_definitions(
  spherely
  PRIVATE
  VERSION_INFO=${PROJECT_VERSION}
  S2GEOGRAPHY_VERSION=${s2geography_VERSION}
  S2GEOGRAPHY_VERSION_MAJOR=${s2geography_VERSION_MAJOR}
  S2GEOGRAPHY_VERSION_MINOR=${s2geography_VERSION_MINOR})

target_link_libraries(spherely
  PRIVATE pybind11::module pybind11::lto pybind11::windows_extras geoarrow_wkb
  PUBLIC s2::s2 s2geography
  )

pybind11_extension(spherely)
if(NOT MSVC AND NOT ${CMAKE_BUILD_TYPE} MATCHES Debug|RelWithDebInfo)
    # Strip unnecessary sections of the binary on Linux/macOS
    pybind11_strip(spherely)
endif()

set_target_properties(spherely PROPERTIES CXX_VISIBILITY_PRESET "hidden")

if (SPHERELY_CODE_COVERAGE)
  target_compile_options(coverage_config INTERFACE -O0 -g --coverage)
  target_link_options(coverage_config INTERFACE --coverage)
  target_link_libraries(spherely PUBLIC coverage_config)
endif()

# Install

install(TARGETS spherely LIBRARY DESTINATION .)

# install type annotations
install(FILES src/spherely.pyi DESTINATION .)
