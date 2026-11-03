# Install script for directory: C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/pockettracker-sdl")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/Debug/ogg.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/Release/ogg.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/MinSizeRel/ogg.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/RelWithDebInfo/ogg.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/ogg" TYPE FILE FILES
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/include/ogg/config_types.h"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/include/ogg/ogg.h"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/include/ogg/os_types.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Ogg/OggTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Ogg/OggTargets.cmake"
         "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/CMakeFiles/Export/dee6fd410a50d06b294b496f57355584/OggTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Ogg/OggTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Ogg/OggTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Ogg" TYPE FILE FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/CMakeFiles/Export/dee6fd410a50d06b294b496f57355584/OggTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Ogg" TYPE FILE FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/CMakeFiles/Export/dee6fd410a50d06b294b496f57355584/OggTargets-debug.cmake")
  endif()
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Ogg" TYPE FILE FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/CMakeFiles/Export/dee6fd410a50d06b294b496f57355584/OggTargets-minsizerel.cmake")
  endif()
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Ogg" TYPE FILE FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/CMakeFiles/Export/dee6fd410a50d06b294b496f57355584/OggTargets-relwithdebinfo.cmake")
  endif()
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Ogg" TYPE FILE FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/CMakeFiles/Export/dee6fd410a50d06b294b496f57355584/OggTargets-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Ogg" TYPE FILE FILES
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/OggConfig.cmake"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/OggConfigVersion.cmake"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/ogg.pc")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/doc/libogg/html" TYPE FILE FILES
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/framing.html"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/index.html"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/oggstream.html"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/ogg-multiplex.html"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/fish_xiph_org.png"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/multiplex1.png"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/packets.png"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/pages.png"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/stream.png"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/vorbisword2.png"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/white-ogg.png"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/white-xifish.png"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/rfc3533.txt"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/rfc5334.txt"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/skeleton.html"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/doc/libogg/html" TYPE DIRECTORY FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/ogg/doc/libogg")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/ogg/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
