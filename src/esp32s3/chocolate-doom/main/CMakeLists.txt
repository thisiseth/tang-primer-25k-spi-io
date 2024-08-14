idf_component_register(SRCS "main.c" 
                    INCLUDE_DIRS ".")

fatfs_create_spiflash_image(storage ../flash_data FLASH_IN_PROJECT)
