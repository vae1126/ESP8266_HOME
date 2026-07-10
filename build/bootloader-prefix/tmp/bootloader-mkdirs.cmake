# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/ait-tch/esp/ESP8266_RTOS_SDK/components/bootloader/subproject"
  "/home/ait-tch/esp/Project/tcp_refactored/build/bootloader"
  "/home/ait-tch/esp/Project/tcp_refactored/build/bootloader-prefix"
  "/home/ait-tch/esp/Project/tcp_refactored/build/bootloader-prefix/tmp"
  "/home/ait-tch/esp/Project/tcp_refactored/build/bootloader-prefix/src/bootloader-stamp"
  "/home/ait-tch/esp/Project/tcp_refactored/build/bootloader-prefix/src"
  "/home/ait-tch/esp/Project/tcp_refactored/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/ait-tch/esp/Project/tcp_refactored/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/ait-tch/esp/Project/tcp_refactored/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
