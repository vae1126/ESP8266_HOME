# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/ait-tch/esp/ESP8266_RTOS_SDK/tools/kconfig"
  "/home/ait-tch/esp/Project/tcp_refactored/build/kconfig_bin"
  "/home/ait-tch/esp/Project/tcp_refactored/build/mconf-idf-prefix"
  "/home/ait-tch/esp/Project/tcp_refactored/build/mconf-idf-prefix/tmp"
  "/home/ait-tch/esp/Project/tcp_refactored/build/mconf-idf-prefix/src/mconf-idf-stamp"
  "/home/ait-tch/esp/Project/tcp_refactored/build/mconf-idf-prefix/src"
  "/home/ait-tch/esp/Project/tcp_refactored/build/mconf-idf-prefix/src/mconf-idf-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/ait-tch/esp/Project/tcp_refactored/build/mconf-idf-prefix/src/mconf-idf-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/ait-tch/esp/Project/tcp_refactored/build/mconf-idf-prefix/src/mconf-idf-stamp${cfgdir}") # cfgdir has leading slash
endif()
