# =============================================================================
# Project Definition and Versioning
# =============================================================================
cmake_minimum_required(VERSION 3.15)
project(Relocator VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include helpers for standard install paths and fetching dependencies
include(GNUInstallDirs)
include(FetchContent)

# =============================================================================
# Dependencies
# =============================================================================
# For modern command-line parsing, we use CLI11.
# FetchContent will download it automatically at configure time.
FetchContent_Declare(
        CLI11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG        v2.4.2
)
FetchContent_MakeAvailable(CLI11)

# =============================================================================
# Source File Definitions
# =============================================================================

# Define the source files for the application.
# We will conditionally compile the platform-specific implementation.
# Process.hpp is a header-only utility and does not need a .cpp file here.
set(RELOCATOR_SOURCES
        src/main.cpp
)

if(APPLE)
    list(APPEND RELOCATOR_SOURCES src/MacOS_Relocator.cpp)
elseif(UNIX)
    list(APPEND RELOCATOR_SOURCES src/Linux_Relocator.cpp)
elseif(WIN32)
    list(APPEND RELOCATOR_SOURCES src/Windows_Relocator.cpp)
endif()

# =============================================================================
# Target Definition
# =============================================================================
add_executable(relocator ${RELOCATOR_SOURCES})

# =============================================================================
# Target Properties
# =============================================================================
# Link our executable to the argument parser library
target_link_libraries(relocator PRIVATE CLI11::CLI11)

if(APPLE)
    # On macOS, we need to link against the CoreFoundation framework
    # to use CFBundle related functions for robust framework handling.
    target_link_libraries(relocator PRIVATE "-framework CoreFoundation")
endif()


# =============================================================================
# Installation and Packaging
# =============================================================================

# Install the final 'relocator' executable to a standard binary directory
# We assign it to a component to make CPack happy.
install(TARGETS relocator
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT main
)

# --- CPack Configuration ---
set(CPACK_PACKAGE_NAME "Relocator")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "MaplePost")
set(CPACK_PACKAGE_CONTACT "pslack@pjslack.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A tool to create relocatable application and library bundles.")
set(CPACK_BUILD_CONFIG "${CMAKE_BUILD_TYPE}")

set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.txt")

# These two lines are critical for making productbuild work correctly.
set(CPACK_COMPONENTS_ALL main)
set(CPACK_SET_DESTDIR ON)


# Platform-specific package generators
if(WIN32)
    set(CPACK_GENERATOR "NSIS")
elseif(APPLE)
    set(CPACK_GENERATOR "productbuild")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
else() # Linux
    set(CPACK_GENERATOR "DEB")
endif()

include(CPack)
