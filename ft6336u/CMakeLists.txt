# ESP-IDF wrapper component for the platform-agnostic FT6336U driver.
# Compiles the driver core from ../../ic-drivers/ft6336u plus an I2C glue layer.
set(FT6336_CORE "${CMAKE_CURRENT_LIST_DIR}/../../ic-drivers/ft6336u")

idf_component_register(SRCS "ft6336u_glue.c" "${FT6336_CORE}/ft6336u.c"
                    INCLUDE_DIRS "." "${FT6336_CORE}"
                    REQUIRES driver)
