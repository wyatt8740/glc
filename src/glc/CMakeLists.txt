
SET(COMMON_HDR common/glc.h
	       common/core.h
	       common/log.h
	       common/state.h
	       common/thread.h
	       common/util.h)
SET(COMMON_SRC common/core.c
	       common/log.c
	       common/state.c
	       common/thread.c
	       common/util.c)

SET(CORE_HDR core/color.h
	     core/copy.h
	     core/file.h
	     core/info.h
	     core/pack.h
	     core/rgb.h
	     core/scale.h
	     core/ycbcr.h)
SET(CORE_SRC core/color.c
	     core/copy.c
	     core/file.c
	     core/info.c
	     core/pack.c
	     core/rgb.c
	     core/scale.c
	     core/ycbcr.c)

SET(CAPTURE_HDR capture/alsa_capture.h
		capture/alsa_hook.h
		capture/audio_capture.h
		capture/gl_capture.h)
SET(CAPTURE_SRC capture/alsa_capture.c
		capture/alsa_hook.c
		capture/audio_capture.c
		capture/gl_capture.c)

SET(PLAY_HDR play/alsa_play.h
	     play/gl_play.h
	     play/demux.h)
SET(PLAY_SRC play/alsa_play.c
	     play/gl_play.c
	     play/demux.c)

SET(EXPORT_HDR export/img.h
	       export/wav.h
	       export/yuv4mpeg.h)
SET(EXPORT_SRC export/img.c
	       export/wav.c
	       export/yuv4mpeg.c)

SET(QUICKLZ_SRC)
SET(LZO_SRC)

MACRO(ADD_GLC_LIBRARY NAME SOURCES LIBRARIES)
  ADD_LIBRARY(${NAME} SHARED ${SOURCES})
  TARGET_LINK_LIBRARIES(${NAME} ${LIBRARIES})
  SET_TARGET_PROPERTIES(${NAME} PROPERTIES
  			OUTPUT_NAME ${NAME}
  			VERSION ${GLC_VER}
  			SOVERSION ${GLC_SOVER})
ENDMACRO(ADD_GLC_LIBRARY)

IF (QUICKLZ)
  ADD_DEFINITIONS(-D__QUICKLZ)
  INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/support/quicklz)
  SET(QUICKLZ_SRC ${PROJECT_SOURCE_DIR}/support/quicklz/quicklz.h
  		  ${PROJECT_SOURCE_DIR}/support/quicklz/quicklz.c)
ENDIF (QUICKLZ)

IF (LZO)
  ADD_DEFINITIONS(-D__MINILZO)
  INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/support/minilzo)
  SET(LZO_SRC ${PROJECT_SOURCE_DIR}/support/minilzo/minilzo.c
  	      ${PROJECT_SOURCE_DIR}/support/minilzo/lzoconf.h
  	      ${PROJECT_SOURCE_DIR}/support/minilzo/lzodefs.h
  	      ${PROJECT_SOURCE_DIR}/support/minilzo/minilzo.h)
ENDIF (LZO)

SET(GLC_CORE_SRC "${COMMON_HDR};${CORE_HDR};${COMMON_SRC};${CORE_SRC};${LZO_SRC};${QUICKLZ_SRC}")
SET(GLC_CORE_LIB m ${PACKETSTREAM_LIBRARY})
ADD_GLC_LIBRARY(glc-core "${GLC_CORE_SRC}" "${GLC_CORE_LIB}")

SET(GLC_CAPTURE_SRC "${COMMON_HDR};${CAPTURE_HDR};${CAPTURE_SRC}")
SET(GLC_CAPTURE_LIB GL dl asound X11 Xxf86vm glc-core)
ADD_GLC_LIBRARY(glc-capture "${GLC_CAPTURE_SRC}" "${GLC_CAPTURE_LIB}")

SET(GLC_PLAY_SRC "${COMMON_HDR};${PLAY_HDR};${PLAY_SRC}")
SET(GLC_PLAY_LIB GL asound X11 glc-core)
ADD_GLC_LIBRARY(glc-play "${GLC_PLAY_SRC}" "${GLC_PLAY_LIB}")

SET(GLC_EXPORT_SRC "${COMMON_HDR};${EXPORT_HDR};${EXPORT_SRC}")
SET(GLC_EXPORT_LIB png glc-core)
ADD_GLC_LIBRARY(glc-export "${GLC_EXPORT_SRC}" "${GLC_EXPORT_LIB}")

IF (UNIX)
  IF (NOT MLIBDIR)
    SET(MLIBDIR "lib")
  ENDIF (NOT MLIBDIR)
  INSTALL(TARGETS glc-core
		  glc-capture
		  glc-play
		  glc-export
  	  LIBRARY DESTINATION ${MLIBDIR})
  IF (HEADERS)
    INSTALL(FILES ${COMMON_HDR}
    		  DESTINATION include/glc/common)
    INSTALL(FILES ${CORE_HDR}
    		  DESTINATION include/glc/core)
    INSTALL(FILES ${CAPTURE_HDR}
    		  DESTINATION include/glc/capture)
    INSTALL(FILES ${PLAY_HDR}
    		  DESTINATION include/glc/play)
    INSTALL(FILES ${EXPORT_HDR}
    		  DESTINATION include/glc/export)
  ENDIF (HEADERS)
ENDIF (UNIX)
