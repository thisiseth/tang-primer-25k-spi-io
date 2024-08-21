if (ESP32_DOOM)
    add_library(opl STATIC
                                opl_internal.h
                opl.c           opl.h
                #opl_linux.c
                #opl_obsd.c
                opl_queue.c     opl_queue.h
                #opl_sdl.c
                #opl_timer.c     opl_timer.h
                #opl_win32.c
                #ioperm_sys.c    ioperm_sys.h
                )

    if(1)
        target_sources(opl PRIVATE
                       woody_opl.c     woody_opl.h
                       opl_esp32_woody.c)

        target_compile_options(opl PRIVATE "-Wno-dangling-pointer" "-Wno-array-bounds")
    else()
        target_sources(opl PRIVATE
                       opl3.c          opl3.h
                       opl_esp32_nuked.c)
    endif()


    target_include_directories(opl
                            INTERFACE "."
                            PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/../")

    target_link_libraries(opl esp32_system idf::freertos idf::fpga_driver)
    
else()

    add_library(opl STATIC
                opl_internal.h
                opl.c           opl.h
                opl_linux.c
                opl_obsd.c
                opl_queue.c     opl_queue.h
                opl_sdl.c
                opl_timer.c     opl_timer.h
                opl_win32.c
                ioperm_sys.c    ioperm_sys.h
                opl3.c          opl3.h)
    target_include_directories(opl
                            INTERFACE "."
                            PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/../")

    target_link_libraries(opl SDL2::SDL2)
    if(ENABLE_SDL2_MIXER)
        target_link_libraries(opl SDL2_mixer::SDL2_mixer)
    endif()

endif()