# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles\\lstlib_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\lstlib_autogen.dir\\ParseCache.txt"
  "lstlib_autogen"
  )
endif()
