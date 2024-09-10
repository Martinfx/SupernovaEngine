if(CMAKE_SYSTEM_NAME STREQUAL "Windows" OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
	find_package(Ogg CONFIG REQUIRED)
	find_package(Vorbis CONFIG REQUIRED)
else()
	include(FindPkgConfig)
	find_path(OGG_INCLUDE_DIR ogg/ogg.h)
	find_library(OGG_LIBRARY NAMES ogg)
	find_library(VORBIS_LIBRARY NAMES vorbis)
	set(VORBIS_LIBRARIES ${VORBISENC_LIBRARY} ${VORBISFILE_LIBRARY} ${VORBIS_LIBRARY} ${OGG_LIBRARY})
endif()

#find_package(Ogg CONFIG REQUIRED)
#find_package(Vorbis CONFIG REQUIRED)

add_library(VorbisDecoder "VorbisDecoder.hpp" "VorbisDecoder.cpp")
target_include_directories(VorbisDecoder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_SYSTEM_NAME STREQUAL "Windows" OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
target_link_libraries(VorbisDecoder
  PUBLIC AudioDecoder
  PRIVATE Ogg::ogg Vorbis::vorbis Vorbis::vorbisenc Vorbis::vorbisfile
)
else()
  target_include_directories(VorbisDecoder PRIVATE /usr/local/include/)
  target_link_libraries(VorbisDecoder
  PUBLIC AudioDecoder
  PRIVATE ${VORBISENC_LIBRARY} ${VORBISFILE_LIBRARY} ${VORBIS_LIBRARY} ${OGG_LIBRARY})
endif()

set_target_properties(VorbisDecoder PROPERTIES FOLDER "Framework/Audio/Decoders")
set_warning_level(VorbisDecoder)
