if(EXISTS "${GENERATED_SOURCE}")
    file(READ "${GENERATED_SOURCE}" SOURCE_CONTENT)
    set(FMOD_INCLUDE "#include <Geode/fmod/fmod.h>")
    
    if(NOT SOURCE_CONTENT MATCHES "(^|\n)[ \t]*#include[ \t]*<Geode/fmod/fmod\\.h>")
        string(REGEX REPLACE "#include[ \t]*<Geode/fmod/fmod\\.h>\n?" "" SOURCE_CONTENT "${SOURCE_CONTENT}")
        
        string(REGEX MATCH "^[ \t\n]*" LEADING_WHITESPACE "${SOURCE_CONTENT}")
        string(LENGTH "${LEADING_WHITESPACE}" WHITESPACE_LEN)
        
        string(SUBSTRING "${SOURCE_CONTENT}" 0 ${WHITESPACE_LEN} BEFORE)
        string(SUBSTRING "${SOURCE_CONTENT}" ${WHITESPACE_LEN} -1 AFTER)
        set(SOURCE_CONTENT "${BEFORE}${FMOD_INCLUDE}\n${AFTER}")
        
        file(WRITE "${GENERATED_SOURCE}" "${SOURCE_CONTENT}")
        message(STATUS "Fixed GeneratedSource.cpp: added fmod.h include at the top")
    endif()
endif()
