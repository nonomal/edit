cmake_minimum_required(VERSION 3.25)

# Enable CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
cmake_policy(SET CMP0141 NEW)

project(tui LANGUAGES C)

add_executable(
        tui
        src/arena.c
        src/buffer.c
        src/helpers.c
        src/icu.c
        src/input.c
        src/loc.c
        src/main.c
        src/os_win32.c
        src/tui.c
        src/tui.exe.manifest
        src/ucd.c
        src/vt.c
)

set_target_properties(
        tui
        PROPERTIES
        C_STANDARD 23
        CXX_STANDARD 20
)

if (MSVC)
    # Enable hot reload for Debug builds.
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<CONFIG:Debug>,EditAndContinue,ProgramDatabase>")

    # We want to always use the DLL runtime, even for Release builds.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    # MSVC currently doesn't have a switch for C23 and CMake only supports C17 for MSVC.
    set(CMAKE_C23_STANDARD_COMPILE_OPTION "/std:clatest")

    foreach (lang C CXX)
        # CMake by default manually specifies a list of libs for linking.
        # Why? Just use the default imports.
        #set("CMAKE_${lang}_STANDARD_LIBRARIES" "")

        string(
                CONCAT
                "CMAKE_${lang}_FLAGS"
                # Allow the source code to identify the language version.
                "/Zc:__STDC__ /Zc:__cplusplus "
                # Eww, who needs RTTI?
                "/GR- "
                # Avoids putting inline functions into .obj files.
                # Reduces their size sometimes significantly.
                "/Zc:inline "
                # > For a new project, it may be best to use /W4 in all compilations.
                # > This option helps ensure the fewest possible hard-to-find code defects.
                "/W4 /wd4100 "
                # MSVC is still living in the 80s. Tell it to wake up.
                "/utf-8 "
                # Deterministic compilation? Great!
                "/experimental:deterministic "
                # Enables new, neat console output diagnostics.
                "/diagnostics:caret"
        )

        string(
                CONCAT
                "CMAKE_${lang}_FLAGS_DEBUG"
                # Produce a PDB file in a format that supports the Edit and Continue feature.
                "/Zi "
        )

        foreach (mode RELEASE RELWITHDEBINFO)
            string(
                    CONCAT
                    "CMAKE_${lang}_FLAGS_${mode}"
                    # Turn optimizations on.
                    "/O2 "
                    # Produce a separate PDB file.
                    "/Zi "
                    # NDEBUG is a standard C/C++ precompiler flag for Release mode.
                    "/DNDEBUG "
                    # This will go hand in hand with the linker instructions below.
                    # Currently disabled until I decide if I want to nuke the CRT imports or not.
                    #"/DNODEFAULTLIB"
            )
        endforeach ()
    endforeach ()

    foreach (mode RELEASE RELWITHDEBINFO)
        string(
                CONCAT
                "CMAKE_EXE_LINKER_FLAGS_${mode}"
                # Ensure we get a nice monolithic PDB.
                "/DEBUG:FULL "
                # /INCREMENTAL:NO and /LTCG are important for small binaries.
                "/INCREMENTAL:NO /LTCG "
                # /DEBUG disables /OPT:REF and /OPT:ICF which is bad for the binary size.
                "/OPT:REF,ICF "
                # This ensures our binary gets a checksum.
                "/RELEASE "
                # Remove the default CRT, vcruntime, etc. imports.
                # We want tight control over what gets imported so that it is tiny and runs everywhere.
                "/NODEFAULTLIB "
                # ucrt provides the C runtime functions (printf, etc.)
                "ucrt.lib "
                # Confusingly, vcruntime provides memcpy and memset. God knows why it's not in ucrt.
                "vcruntime.lib "
                # msvcrt provides the /GS (Buffer Security Check) support code and __isa_available_init.
                "msvcrt.lib "
                # Provides the exports for the subset of Win32 APIs
                # that are common to all Windows 10 devices, and later.
                "OneCore.lib"
        )
    endforeach ()
elseif (WIN32)
    # The clang-cl distribution from Visual Studio currently doesn't support /Zc:__STDC__.
    # I couldn't find a different workaround.
    target_compile_definitions(tui PRIVATE __STDC__)
    target_compile_options(tui PRIVATE -Wall -Werror -Wno-unused-function)
endif ()
