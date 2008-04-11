PROJECT(glc)

SET(GLC_SOVER 0)
SET(GLC_VER 0.5.4)

IF (NOT CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE "Release")
ENDIF (NOT CMAKE_BUILD_TYPE)

OPTION(QUICKLZ
       "QuickLZ support"
       ON)
OPTION(LZO
       "LZO support"
       ON)
OPTION(BINARIES
       "Build and install glc-capture and glc-play"
       ON)
OPTION(HEADERS
       "Install headers"
       ON)
OPTION(HOOK
       "Build and install glc-hook"
       ON)
OPTION(SCRIPTS
       "Install sample scripts."
       OFF)

SUBDIRS(src)

IF (SCRIPTS)
  IF (UNIX)
    INSTALL(FILES scripts/capture.sh
    		  scripts/encode.sh
    		  scripts/play.sh
    		  DESTINATION share/glc)
  ENDIF (UNIX)
ENDIF (SCRIPTS)
