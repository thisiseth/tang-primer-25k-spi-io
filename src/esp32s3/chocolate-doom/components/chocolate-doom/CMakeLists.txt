idf_component_register(SRCS "doom_main.c" "doom_misc.c"
                    INCLUDE_DIRS "."
                    REQUIRES fpga_driver
                    LDFRAGMENTS doom.lf opl.lf)

set(ESP32_DOOM 1)
add_compile_definitions(ESP32_DOOM=1)

add_subdirectory(doom)

target_link_libraries(${COMPONENT_LIB} PRIVATE esp-idf-chocolate-doom)