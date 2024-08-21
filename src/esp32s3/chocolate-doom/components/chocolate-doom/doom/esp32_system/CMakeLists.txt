add_library(esp32_system STATIC 
            esp32_mixer.c       esp32_mixer.h)

target_include_directories(esp32_system
            INTERFACE ".")

target_link_libraries(esp32_system idf::freertos idf::fpga_driver)