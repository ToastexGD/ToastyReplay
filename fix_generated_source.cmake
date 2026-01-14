# Script to fix GeneratedSource.cpp to include fmod.h before other includes
# This is run as a PRE_BUILD command, so we modify the file in place
if(EXISTS "${GENERATED_SOURCE}")
    file(READ "${GENERATED_SOURCE}" SOURCE_CONTENT)
    set(FMOD_INCLUDE "#include <Geode/fmod/fmod.h>")
    
    # Check if fmod.h is already included at the top (after any leading whitespace/newlines)
    if(NOT SOURCE_CONTENT MATCHES "(^|\n)[ \t]*#include[ \t]*<Geode/fmod/fmod\\.h>")
        # Remove any existing fmod.h include (in case it's in the wrong place)
        string(REGEX REPLACE "#include[ \t]*<Geode/fmod/fmod\\.h>\n?" "" SOURCE_CONTENT "${SOURCE_CONTENT}")
        
        # Find the first non-empty line (skip leading whitespace/newlines)
        string(REGEX MATCH "^[ \t\n]*" LEADING_WHITESPACE "${SOURCE_CONTENT}")
        string(LENGTH "${LEADING_WHITESPACE}" WHITESPACE_LEN)
        
        # Insert fmod.h include at the beginning (after leading whitespace)
        string(SUBSTRING "${SOURCE_CONTENT}" 0 ${WHITESPACE_LEN} BEFORE)
        string(SUBSTRING "${SOURCE_CONTENT}" ${WHITESPACE_LEN} -1 AFTER)
        set(SOURCE_CONTENT "${BEFORE}${FMOD_INCLUDE}\n${AFTER}")
        
        file(WRITE "${GENERATED_SOURCE}" "${SOURCE_CONTENT}")
        message(STATUS "Fixed GeneratedSource.cpp: added fmod.h include at the top")
    endif()
endif()
