cmake_minimum_required(VERSION 3.16)

project(tugalex VERSION 2.2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_tugalex_top ON)
else()
    set(_tugalex_top OFF)
endif()
option(TUGALEX_BUILD_TESTS "Build the parity and unit tests" ${_tugalex_top})

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

set(TUGALEX_DATA_INSTALL_DIR ${CMAKE_INSTALL_DATADIR}/tugalex)
# The library looks for its data relative to the installed library, or to the
# executable it is linked into, before the absolute path, so an install made
# with `cmake --install --prefix` into another prefix finds its data.
file(RELATIVE_PATH _tugalex_data_from_lib "${CMAKE_INSTALL_FULL_LIBDIR}" "${CMAKE_INSTALL_FULL_DATADIR}/tugalex")
file(RELATIVE_PATH _tugalex_data_from_bin "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_DATADIR}/tugalex")

add_library(tugalex
    src/unicode.cpp
    src/csv.cpp
    src/paths.cpp
    src/noun_gender.cpp
    src/lexicon.cpp
)
add_library(tugalex::tugalex ALIAS tugalex)
target_include_directories(tugalex
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(tugalex PRIVATE
    TUGALEX_DEFAULT_DATA_DIR="${CMAKE_INSTALL_PREFIX}/${TUGALEX_DATA_INSTALL_DIR}"
    TUGALEX_DATA_FROM_LIBDIR="${_tugalex_data_from_lib}"
    TUGALEX_DATA_FROM_BINDIR="${_tugalex_data_from_bin}")
target_link_libraries(tugalex PRIVATE ZLIB::ZLIB ${CMAKE_DL_LIBS} PUBLIC Threads::Threads)
set_target_properties(tugalex PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tugalex PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS tugalex EXPORT tugalexTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY data/ DESTINATION ${TUGALEX_DATA_INSTALL_DIR})
install(EXPORT tugalexTargets
    NAMESPACE tugalex::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tugalex)
configure_package_config_file(cmake/tugalexConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/tugalexConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tugalex)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/tugalexConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/tugalexConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/tugalexConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tugalex)

if(TUGALEX_BUILD_TESTS)
    enable_testing()

    add_executable(tugalex_parity tests/parity.cpp)
    target_link_libraries(tugalex_parity PRIVATE tugalex ZLIB::ZLIB)
    add_test(NAME parity COMMAND tugalex_parity ${CMAKE_CURRENT_SOURCE_DIR}/tests)

    add_executable(tugalex_examples tests/examples.cpp)
    target_link_libraries(tugalex_examples PRIVATE tugalex)
    add_test(NAME examples COMMAND tugalex_examples)

    set_tests_properties(parity examples PROPERTIES
        ENVIRONMENT "TUGALEX_DATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/data")

    # Installs into a prefix that is not CMAKE_INSTALL_PREFIX and runs a
    # program installed there with no TUGALEX_DATA_DIR.
    add_test(NAME relocated_install
             COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                     -DPREFIX=${CMAKE_CURRENT_BINARY_DIR}/relocated-install -DINSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
                     -DBINDIR=${CMAKE_INSTALL_BINDIR} -DDATADIR=${CMAKE_INSTALL_DATADIR}
                     -DCXX_COMPILER=${CMAKE_CXX_COMPILER} "-DCXX_FLAGS=${CMAKE_CXX_FLAGS}"
                     -DBUILD_TYPE=${CMAKE_BUILD_TYPE} -DZLIB_LIBRARY=${ZLIB_LIBRARY}
                     -DZLIB_INCLUDE_DIR=${ZLIB_INCLUDE_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/relocated_install.cmake)
    # A sanitizer build replays the goldens in about ten minutes.
    set_tests_properties(parity PROPERTIES TIMEOUT 3600)
endif()
