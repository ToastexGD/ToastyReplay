set(PCH_FILE "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${PROJECT_NAME}.dir/cmake_pch.hxx")
if(EXISTS "${PCH_FILE}")
    file(READ "${PCH_FILE}" PCH_CONTENT)
    string(REGEX REPLACE "#include \"[^\"]*fmod/fmod\\.h\"\n?" "" PCH_CONTENT "${PCH_CONTENT}")

    string(FIND "${PCH_CONTENT}" "Geode/Geode.hpp" GEODE_POS)
    if(NOT GEODE_POS EQUAL -1)
        string(SUBSTRING "${PCH_CONTENT}" 0 ${GEODE_POS} BEFORE)
        string(SUBSTRING "${PCH_CONTENT}" ${GEODE_POS} -1 AFTER)
        file(TO_CMAKE_PATH "${GEODE_SDK_PATH}" GEODE_SDK_CMAKE)
        set(FMOD_LINE "#include \"${GEODE_SDK_CMAKE}/loader/include/Geode/fmod/fmod.h\"")
        set(PCH_CONTENT "${BEFORE}${FMOD_LINE}\n${AFTER}")
        file(WRITE "${PCH_FILE}" "${PCH_CONTENT}")
        message(STATUS "Fixed precompiled header: added fmod.h include")
    endif()
endif()

set(STANDALONES_FILE "${CMAKE_CURRENT_BINARY_DIR}/bindings/bindings/Geode/binding/Standalones.hpp")
if(EXISTS "${STANDALONES_FILE}")
    file(READ "${STANDALONES_FILE}" STANDALONES_CONTENT)
    
    if(NOT STANDALONES_CONTENT MATCHES "F_API FMOD_Debug_Initialize")
        string(REGEX REPLACE 
            "(FMOD_RESULT) (FMOD_Debug_Initialize|FMOD_File_GetDiskBusy|FMOD_File_SetDiskBusy|FMOD_Memory_GetStats|FMOD_Memory_Initialize|FMOD_System_Create|FMOD_Thread_SetAttributes)\\("
            "\\1 F_API \\2("
            STANDALONES_CONTENT "${STANDALONES_CONTENT}")
        
        file(WRITE "${STANDALONES_FILE}" "${STANDALONES_CONTENT}")
        message(STATUS "Fixed Standalones.hpp: added F_API to FMOD function declarations")
    endif()
endif()
