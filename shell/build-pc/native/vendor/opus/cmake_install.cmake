# Install script for directory: C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/opus

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
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/opus/Debug/opus.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/opus/Release/opus.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/opus/MinSizeRel/opus.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/opus/RelWithDebInfo/opus.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/opus" TYPE FILE FILES
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/opus/include/opus.h"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/opus/include/opus_defines.h"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/opus/include/opus_multistream.h"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/opus/include/opus_projection.h"
    "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/native/vendor/opus/include/opus_types.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/Jordan Solans/Documents/GitHub/pockettracker - Copie (2)/pockettracker - Copie/shell/build-pc/native/vendor/opus/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
