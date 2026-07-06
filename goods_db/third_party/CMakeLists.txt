# Third-party libraries (symlinked from BusTub)

# fmt - formatting library
add_subdirectory(fmt ${CMAKE_BINARY_DIR}/fmt)
set(FMT_LIB fmt::fmt PARENT_SCOPE)

# murmur3 - hash function (header-only basically)
# We don't build it separately, just use the header

# Google Test
if(ENABLE_GTEST)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(googletest ${CMAKE_BINARY_DIR}/googletest)
endif()
