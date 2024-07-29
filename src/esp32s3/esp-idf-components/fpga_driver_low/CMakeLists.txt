idf_component_register(SRCS "fpga_qspi.c" "fpga_api_gpu.c" "fpga_api_io.c"
                    INCLUDE_DIRS "."
					REQUIRES driver)