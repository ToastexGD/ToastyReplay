# Script to fix the precompiled header before each build
set(PCH_FILE "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${PROJECT_NAME}.dir/cmake_pch.hxx")
if(EXISTS "${PCH_FILE}")
    file(READ "${PCH_FILE}" PCH_CONTENT)
    # Always fix it - remove any existing fmod.h include and re-add it correctly
    # First, remove any existing fmod.h line (in case it's malformed)
    string(REGEX REPLACE "#include \"[^\"]*fmod/fmod\\.h\"\n?" "" PCH_CONTENT "${PCH_CONTENT}")
    # Now find Geode.hpp and insert fmod.h before it
    string(FIND "${PCH_CONTENT}" "Geode/Geode.hpp" GEODE_POS)
    if(NOT GEODE_POS EQUAL -1)
        # Find the start of the line (go back to find #include)
        string(SUBSTRING "${PCH_CONTENT}" 0 ${GEODE_POS} BEFORE)
        string(SUBSTRING "${PCH_CONTENT}" ${GEODE_POS} -1 AFTER)
        # Get the Geode SDK path with forward slashes (CMake format)
        file(TO_CMAKE_PATH "${GEODE_SDK_PATH}" GEODE_SDK_CMAKE)
        # Insert the fmod.h include with proper path format
        set(FMOD_LINE "#include \"${GEODE_SDK_CMAKE}/loader/include/Geode/fmod/fmod.h\"")
        set(PCH_CONTENT "${BEFORE}${FMOD_LINE}\n${AFTER}")
        file(WRITE "${PCH_FILE}" "${PCH_CONTENT}")
        message(STATUS "Fixed precompiled header: added fmod.h include")
    endif()
endif()

# Fix Standalones.hpp by adding F_API to FMOD function declarations
# This fixes the C2373 redefinition errors in the GeodeBindings target
set(STANDALONES_FILE "${CMAKE_CURRENT_BINARY_DIR}/bindings/bindings/Geode/binding/Standalones.hpp")
if(EXISTS "${STANDALONES_FILE}")
    file(READ "${STANDALONES_FILE}" STANDALONES_CONTENT)
    
    # Check if already fixed (contains "F_API FMOD_Debug_Initialize")
    if(NOT STANDALONES_CONTENT MATCHES "F_API FMOD_Debug_Initialize")
        # Add F_API to FMOD function declarations that are missing it
        # Pattern: FMOD_RESULT FMOD_FunctionName( -> FMOD_RESULT F_API FMOD_FunctionName(
        string(REGEX REPLACE 
            "(FMOD_RESULT) (FMOD_Debug_Initialize|FMOD_File_GetDiskBusy|FMOD_File_SetDiskBusy|FMOD_Memory_GetStats|FMOD_Memory_Initialize|FMOD_System_Create|FMOD_Thread_SetAttributes)\\("
            "\\1 F_API \\2("
            STANDALONES_CONTENT "${STANDALONES_CONTENT}")
        
        file(WRITE "${STANDALONES_FILE}" "${STANDALONES_CONTENT}")
        message(STATUS "Fixed Standalones.hpp: added F_API to FMOD function declarations")
    endif()
endif()
