idf_component_register(SRCS "main.c" 
                    INCLUDE_DIRS ".")

set(ESP32_DOOM_WAD_NAME doom1.wad)

get_filename_component(wad_image ../flash_ro/${ESP32_DOOM_WAD_NAME} ABSOLUTE)
file(SIZE ${wad_image} wad_size)

message(STATUS "Size of ${wad_image} is ${wad_size} bytes")

add_compile_definitions(ESP32_DOOM_WAD_SIZE=${wad_size})
add_compile_definitions(ESP32_DOOM_WAD_NAME=\"${ESP32_DOOM_WAD_NAME}\")

#flashed always - comment out if tired of waiting when fixing stuff and reflashing
fatfs_create_spiflash_image(storage ../flash_rw FLASH_IN_PROJECT)
#esptool_py_flash_to_partition(flash wads ${wad_image})