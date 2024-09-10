find_package(ZLIB REQUIRED)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
	find_package(unofficial-minizip CONFIG REQUIRED)
else()
	include(FindPkgConfig)
	pkg_check_modules(minizip minizip REQUIRED)
endif()

add_library(FileSystem
  "include/os/FileSystem.hpp"
  "src/FileSystem.cpp"

  "include/os/DataStream.hpp"
  "src/MemoryFile.hpp"
  "src/MemoryFile.cpp"
  "src/File.cpp"
  "src/File.hpp"

  "include/os/ZipArchive.hpp"
  "src/ZipArchive.cpp"
  "src/InZipFile.hpp"
  "src/InZipFile.cpp"

  "include/os/Serialization.hpp"
)
target_include_directories(FileSystem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows" OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(FileSystem
  	PRIVATE unofficial::minizip::minizip
  	PUBLIC StringUtility
	)

else()
        target_link_libraries(FileSystem
  	PRIVATE ${minizip_LIBRARIES}
  	PUBLIC StringUtility
	)

endif()


set_target_properties(FileSystem PROPERTIES FOLDER "Framework/OS")
set_warning_level(FileSystem)

enable_profiler(FileSystem PRIVATE)
