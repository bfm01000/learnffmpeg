# ──────────────────────────────────────────────────────────────────────────────
# FindFFmpeg.cmake — wrapper around pkg-config for FFmpeg components
# ──────────────────────────────────────────────────────────────────────────────

include(FindPackageHandleStandardArgs)

macro(_ffmpeg_find_component _comp _header _lib)
  pkg_check_modules(PC_${_comp} QUIET lib${_comp})
  find_path(${_comp}_INCLUDE_DIR
    NAMES ${_header}
    HINTS ${PC_${_comp}_INCLUDE_DIRS}
  )
  find_library(${_comp}_LIBRARY
    NAMES ${_lib}
    HINTS ${PC_${_comp}_LIBRARY_DIRS}
  )
  if(${_comp}_INCLUDE_DIR AND ${_comp}_LIBRARY)
    set(${_comp}_FOUND TRUE)
    set(${_comp}_LIBRARIES ${${_comp}_LIBRARY})
    set(${_comp}_INCLUDE_DIRS ${${_comp}_INCLUDE_DIR})
    if(NOT TARGET FFmpeg::${_comp})
      add_library(FFmpeg::${_comp} UNKNOWN IMPORTED)
      set_target_properties(FFmpeg::${_comp} PROPERTIES
        IMPORTED_LOCATION ${${_comp}_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${${_comp}_INCLUDE_DIR}
      )
    endif()
  endif()
  mark_as_advanced(${_comp}_INCLUDE_DIR ${_comp}_LIBRARY)
endmacro()

_ffmpeg_find_component(avformat   libavformat/avformat.h avformat)
_ffmpeg_find_component(avcodec    libavcodec/avcodec.h   avcodec)
_ffmpeg_find_component(avutil     libavutil/avutil.h     avutil)
_ffmpeg_find_component(swscale    libswscale/swscale.h   swscale)
_ffmpeg_find_component(swresample libswresample/swresample.h swresample)
_ffmpeg_find_component(avfilter   libavfilter/avfilter.h avfilter)

find_package_handle_standard_args(FFmpeg
  REQUIRED_VARS
    avformat_LIBRARY avformat_INCLUDE_DIR
    avcodec_LIBRARY  avcodec_INCLUDE_DIR
    avutil_LIBRARY   avutil_INCLUDE_DIR
)
