# Script to fix Standalones.hpp by adding F_API to FMOD function declarations
set(STANDALONES_FILE "${CMAKE_CURRENT_BINARY_DIR}/bindings/bindings/Geode/binding/Standalones.hpp")
if(EXISTS "${STANDALONES_FILE}")
    file(READ "${STANDALONES_FILE}" CONTENT)
    
    # Check if already fixed (contains "F_API FMOD_Debug_Initialize")
    if(NOT CONTENT MATCHES "F_API FMOD_Debug_Initialize")
        # Add F_API to FMOD function declarations that are missing it
        # Pattern: FMOD_RESULT FMOD_FunctionName( -> FMOD_RESULT F_API FMOD_FunctionName(
        string(REGEX REPLACE 
            "(FMOD_RESULT) (FMOD_Debug_Initialize|FMOD_File_GetDiskBusy|FMOD_File_SetDiskBusy|FMOD_Memory_GetStats|FMOD_Memory_Initialize|FMOD_System_Create|FMOD_Thread_SetAttributes)\\("
            "\\1 F_API \\2("
            CONTENT "${CONTENT}")
        
        file(WRITE "${STANDALONES_FILE}" "${CONTENT}")
        message(STATUS "Fixed Standalones.hpp: added F_API to FMOD function declarations")
    endif()
endif()
