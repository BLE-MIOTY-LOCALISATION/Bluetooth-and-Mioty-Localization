# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Users/ortiz/.pico-sdk/sdk/2.0.0/tools/pioasm"
  "D:/BLE-STACK-THESIS/mioty-ble/build/pioasm"
  "D:/BLE-STACK-THESIS/mioty-ble/build/pioasm-install"
  "D:/BLE-STACK-THESIS/mioty-ble/build/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/tmp"
  "D:/BLE-STACK-THESIS/mioty-ble/build/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src/pioasmBuild-stamp"
  "D:/BLE-STACK-THESIS/mioty-ble/build/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src"
  "D:/BLE-STACK-THESIS/mioty-ble/build/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src/pioasmBuild-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/BLE-STACK-THESIS/mioty-ble/build/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src/pioasmBuild-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/BLE-STACK-THESIS/mioty-ble/build/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src/pioasmBuild-stamp${cfgdir}") # cfgdir has leading slash
endif()
