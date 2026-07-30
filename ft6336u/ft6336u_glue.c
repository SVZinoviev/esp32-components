/*
 * ft6336u_glue.c - ESP-IDF I2C glue for the platform-agnostic FT6336U driver.
 */

#include "ft6336u_glue.h"

#include "driver/i2c_master.h"
#include "esp_err.h"

#define FT6336_GLUE_TIMEOUT_MS 100

/* Single controller: the driver's interface callbacks carry no context, so the
 * bus/device handles live here. */
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

/* reg pointer write + read, in one transaction. */
static int glue_read(uint8_t dev_addr, uint8_t reg, uint8_t *pdata, size_t len)
{
    (void)dev_addr;
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, pdata, len,
                                                FT6336_GLUE_TIMEOUT_MS);
    return (err == ESP_OK) ? 0 : -1;
}

/* [reg][data...] in one write transaction. */
static int glue_write(uint8_t dev_addr, uint8_t reg, const uint8_t *pdata,
                      size_t len)
{
    (void)dev_addr;
    uint8_t buf[16];
    if (len > sizeof(buf) - 1) {
        return -1;
    }
    buf[0] = reg;
    for (size_t i = 0; i < len; i++) {
        buf[i + 1] = pdata[i];
    }
    esp_err_t err = i2c_master_transmit(s_dev, buf, len + 1,
                                        FT6336_GLUE_TIMEOUT_MS);
    return (err == ESP_OK) ? 0 : -1;
}

int ft6336u_glue_install(struct ft6336_instance *pinstance, int sda_io,
                         int scl_io, uint32_t speed_hz, uint8_t threshold,
                         uint8_t filter)
{
    if (pinstance == NULL) {
        return FT6336_ERROR;
    }

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1,  /* auto-select a free port */
        .sda_io_num = sda_io,
        .scl_io_num = scl_io,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        return FT6336_ERROR;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FT6336_BUS_ADDRESS,
        .scl_speed_hz = speed_hz,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev) != ESP_OK) {
        return FT6336_ERROR;
    }

    struct ft6336_interface iface = {
        .i2c_read = glue_read,
        .i2c_write = glue_write,
        .lock = NULL,
        .unlock = NULL,
    };
    pinstance->address = FT6336_BUS_ADDRESS;
    return ft6336_init(pinstance, &iface, threshold, filter);
}
