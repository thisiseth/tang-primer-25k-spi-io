idf_component_register(SRCS "main.c" 
                    INCLUDE_DIRS ".")

set(ESP32_QUAKE_PAK_NAME pak0.pak)

get_filename_component(pak_image ../flash_ro/${ESP32_QUAKE_PAK_NAME} ABSOLUTE)
file(SIZE ${pak_image} pak_size)

message(STATUS "Size of ${pak_image} is ${pak_size} bytes")

add_compile_definitions(ESP32_QUAKE_PAK_SIZE=${pak_size})
add_compile_definitions(ESP32_QUAKE_PAK_NAME=\"${ESP32_QUAKE_PAK_NAME}\")

#flashed always - comment out if tired of waiting when fixing stuff and reflashing
#fatfs_create_spiflash_image(storage ../flash_rw FLASH_IN_PROJECT)
#esptool_py_flash_to_partition(flash pak ${pak_image})