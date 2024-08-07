idf_component_register(SRCS "doom_main.c" 
                    INCLUDE_DIRS ".")

set(ESP32_DOOM 1)

#add_subdirectory(fluidsynth-2.2.0)
add_subdirectory(libsamplerate-0.2.2)
add_subdirectory(doom)
