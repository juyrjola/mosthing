#include "mgos.h"
#include "mgos_i2c.h"
#include "common/cs_dbg.h"

#define SHT21_I2C_ADDR 0x40

#define TRIGGER_T_MEASUREMENT_HM 0xE3   // command trig. temp meas. hold master
#define TRIGGER_RH_MEASUREMENT_HM 0xE5  // command trig. hum. meas. hold master
#define TRIGGER_T_MEASUREMENT_NHM 0xF3  // command trig. temp meas. no hold master
#define TRIGGER_RH_MEASUREMENT_NHM 0xF5 // command trig. hum. meas. no hold master
#define USER_REGISTER_W 0xE6            // command writing user register
#define USER_REGISTER_R 0xE7            // command reading user register
#define SOFT_RESET 0xFE                 // command soft reset

static struct mgos_i2c *global_i2c;

static const uint16_t POLYNOMIAL = 0x131;  // P(x)=x^8+x^5+x^4+1 = 100110001

void sht21_poll(void)
{
    int ret;
    uint8_t data[6] = {0xfa, 0x0f};

    printf("sht21_poll\n");
    ret = mgos_i2c_write(global_i2c, SHT21_I2C_ADDR, data, 2, true);
    if (ret) {
        LOG(LL_ERROR, ("i2c_write: %d", ret));
        return;
    }
    memset(data, 0, sizeof(data));
    ret = mgos_i2c_read(global_i2c, SHT21_I2C_ADDR, data, 6, true);
    if (ret) {
        LOG(LL_ERROR, ("i2c read: %d", ret));
        return;
    }
    for (int i = 0; i < 6; i++) {
        printf("%02x ", data[i]);
    }
    printf("\n");
}

bool sht21_init(struct mgos_i2c *i2c)
{
    global_i2c = i2c;
    return true;
}
