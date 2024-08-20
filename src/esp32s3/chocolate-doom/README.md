before generating sdkconfig - sdkconfig.defaults.* is added in top-level CMakeLists.txt, edit to select your chip

wad file to flash path is in main/CMakeLists.txt, by default is flashed on idf.py flash (takes time, comment out if not needed)

/flash_rw contains files that are flashed as a FATFS image, so if you want to edit the config you can do it there