#ifndef SHT21_H
#define SHT21_H

#include "mgos_i2c.h"

extern bool sht21_init(struct mgos_i2c *i2c);
extern void sht21_poll(void);

#endif
