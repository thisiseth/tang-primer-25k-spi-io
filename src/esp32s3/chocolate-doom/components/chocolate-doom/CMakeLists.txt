idf_component_register(SRCS "doom_main.c" "doom_misc.c"
                    INCLUDE_DIRS "."
                    REQUIRES fpga_driver
                    LDFRAGMENTS doom.lf opl.lf)

set(ESP32_DOOM 1)
add_compile_definitions(ESP32_DOOM=1)

option(LIBSAMPLERATE_ENABLE_SINC_FAST_CONVERTER "Enable Fastest Sinc Interpolator converter" OFF)
option(LIBSAMPLERATE_ENABLE_SINC_MEDIUM_CONVERTER "Enable Medium Sinc Interpolator converter" OFF)
option(LIBSAMPLERATE_ENABLE_SINC_BEST_CONVERTER "Enable Best Sinc Interpolator converter" OFF)

add_subdirectory(libsamplerate-0.2.2)
add_subdirectory(doom)

target_link_libraries(${COMPONENT_LIB} PRIVATE esp-idf-chocolate-doom)