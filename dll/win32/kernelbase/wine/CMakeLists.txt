add_definitions(-D__WINESRC__ -D_KERNEL32_ -DWINVER=0x601 -D_WIN32_WINNT=0x601)
add_definitions(
    -D__WINESRC__
    -D_WINE
    -Dwcsnicmp=_wcsnicmp
    -Dwcsicmp=_wcsicmp
    -Dstrnicmp=_strnicmp
    -D_KERNELBASE_)

include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR
   CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Silence GCC/Clang warnings
    add_compile_options(
        -Wno-unused-function
        -Wno-unknown-pragmas
        -Wno-unused-variable
        -Wno-unused-value
        -Wno-unused-function
    )
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>)
endif()

if (MSVC)
    add_compile_options(
        /wd4267 # warning C4267: '=': conversion from 'size_t' to 'unsigned int', possible loss of data
    )
endif()

# Static library for shlwapi
list(APPEND SOURCE_SHLWAPI_STATIC
    path.c
    string.c
)

add_library(kernelbase_shlwapi_static ${SOURCE_SHLWAPI_STATIC})
target_compile_definitions(kernelbase_shlwapi_static PRIVATE WINSHLWAPI= WINPATHCCHAPI= PATHCCH_NO_DEPRECATE=)
target_link_libraries(kernelbase_shlwapi_static ${PSEH_LIB})
add_dependencies(kernelbase_shlwapi_static psdk)

if(NOT MSVC)
    target_compile_options(kernelbase_shlwapi_static PRIVATE -Wno-unused-variable)
    target_compile_options(kernelbase_shlwapi_static PRIVATE -Wno-unused-function)
endif()

list(APPEND SOURCE_STATIC
    #console.c #looks like ReactOS console.c is nothing like wine.
    )

#add_library(kernelbase_static ${SOURCE_STATIC})
#target_link_libraries(kernelbase_static psapi_static ${PSEH_LIB})
#add_dependencies(kernelbase_static psdk)

#if(NOT MSVC)
#    target_compile_options(kernelbase_static PRIVATE -Wno-unused-variable)
#    target_compile_options(kernelbase_static PRIVATE -Wno-unused-function)
#endif()

