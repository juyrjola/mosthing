#include <mgos_config.h>
#include <mgos_timers.h>

#include <RH_NRF24.h>
#include <RHReliableDatagram.h>
#include "radiohead.h"

// ESP8266
#if 1
#define CHIP_ENABLE_GPIO 4
#define SLAVE_SELECT_GPIO 15
#define MY_ADDRESS 2
#define OTHER_ADDRESS 1
#endif

static RH_NRF24 *driver;
static RHHardwareSPI *hard_spi;
static RHReliableDatagram *manager;

static void config_driver(void)
{
    int val;

    driver->init();
    val = driver->spiReadRegister(RH_NRF24_REG_00_CONFIG);
    val &= ~RH_NRF24_PWR_UP;
    driver->spiWriteRegister(RH_NRF24_REG_00_CONFIG, val);
    mgos_msleep(10);
    val |= RH_NRF24_PWR_UP;
    driver->spiWriteRegister(RH_NRF24_REG_00_CONFIG, val);
    mgos_msleep(10);

    driver->setChannel(mgos_sys_config_get_radiohead_channel());
    driver->setRF(RH_NRF24::DataRate2Mbps, RH_NRF24::TransmitPower0dBm);
    // Mask all other IRQs except RX_DR
    val = driver->spiReadRegister(RH_NRF24_REG_00_CONFIG);
    driver->spiWriteRegister(RH_NRF24_REG_00_CONFIG, val | RH_NRF24_MASK_TX_DS | RH_NRF24_MASK_MAX_RT);
}

static void poll_manager(void *arg)
{
    static int counter = 0;
    uint8_t buf[32];
    int i;

#if 1
    printf("%02x\n", driver->spiReadRegister(0x00));
    if (driver->available()) {
        uint8_t buf[RH_NRF24_MAX_MESSAGE_LEN];
        uint8_t len = sizeof(buf);
        if (driver->recv(buf, &len))
            printf("Got message: %s\n", (const char *) buf);
        else
            printf("Recv failed\n");
    }
#endif
#if 1
    if (1) {
        int msg = 0;

        driver->setModeRx();
        mgos_msleep(1);
        for (i = 0; i < 200; i++) {
            if (driver->available()) {
                uint8_t buf[RH_NRF24_MAX_MESSAGE_LEN];
                uint8_t len = sizeof(buf);
                msg = 1;
                if (driver->recv(buf, &len))
                    printf("Got message: %s\n", (const char *) buf);
                else
                    printf("Recv failed\n");
            }
            mgos_msleep(1);
        }
        if (!msg)
            printf("no msg\n");
    }
#endif
#if 0
    driver->setModeRx();
#endif
    mgos_set_timer(100 + rand() % 100, 0, poll_manager, NULL);
}

static void handle_rx(void)
{
    uint8_t buf[48];
    uint8_t len = sizeof(buf) - 1;
    uint8_t from;

#if 1
    if (driver->available()) {
        uint8_t buf[RH_NRF24_MAX_MESSAGE_LEN];
        uint8_t len = sizeof(buf);
        if (driver->recv(buf, &len))
            printf("Got message: %s\n", (const char *) buf);
        else
            printf("Recv failed\n");
    } else {
        printf("no msg available\n");
        driver->setModeRx();
    }
    return;
#endif

    if (!manager->available())
        return;

    if (manager->recvfromAck(buf, &len, &from)) {
        buf[len] = '\0';
        printf("Got msg from client %d: %s\n", from, buf);
        sprintf((char *) buf, "pong");
        if (!manager->sendtoWait(buf, strlen((char *) buf) + 1, from)) {
            printf("sendToWait failed\n");
        } else
            printf("sendToWait succeeded\n");
    } else {
        printf("recvFromAck failed\n");
    }
}

static void poll_rx(void *arg)
{
    handle_rx();
    (void) arg;
}

static void nrf24_irq_handler(int pin, void *arg)
{
    int status = driver->spiReadRegister(RH_NRF24_REG_07_STATUS);
    int gpio = mgos_sys_config_get_radiohead_device_irq_gpio();

    printf("a. gpio %d 0x%02x\n", mgos_gpio_read(gpio), status);
    if (status & RH_NRF24_RX_DR)
        handle_rx();
    status = driver->spiReadRegister(RH_NRF24_REG_07_STATUS);
    if (status & RH_NRF24_RX_DR)
        driver->spiWriteRegister(RH_NRF24_REG_07_STATUS, RH_NRF24_RX_DR);
    printf("b. gpio %d 0x%02x\n", mgos_gpio_read(gpio), driver->spiReadRegister(RH_NRF24_REG_07_STATUS));
    driver->setModeRx();
    printf("c. gpio %d 0x%02x\n", mgos_gpio_read(gpio), driver->spiReadRegister(RH_NRF24_REG_07_STATUS));
}

static void test_tx(void *arg)
{
    int server_addr = mgos_sys_config_get_radiohead_sensor_report_address();
    static int counter = 0;
    uint8_t buf[48];

#if 1
    if (1) {
        char msg[15];
        sprintf(msg, "zing%d", counter++);
        if (driver->send((const uint8_t *) msg, strlen(msg) + 1)) {
            bool ret = driver->waitPacketSent();
            if (ret)
                printf("packet sent (%d)\n", counter);
            else {
                printf("waitpacketsent failed?!\n");
                driver->printRegisters();
                config_driver();
            }
        } else
            printf("unable to send\n");
    }
#endif
#if 0
    sprintf((char *) buf, "ping%d", counter++);
    printf("sending to server at %d\n", server_addr);
    if (manager->sendtoWait(buf, strlen((char *) buf) + 1, server_addr)) {
        uint8_t len = sizeof(buf) - 1;
        uint8_t from;
        if (manager->recvfromAckTimeout(buf, &len, 5000, &from)) {
            buf[sizeof(buf)-1] = '\0';
            printf("Got reply from server %d: %s\n", from, buf);
        } else {
            printf("recvfromAck failed\n");
        }
        printf("sendto succeeded\n");
    } else {
        printf("sendto failed\n");
    }
#endif
    mgos_set_timer(100 + rand() % 100, 0, test_tx, NULL);
}

static int8_t radiohead_initialized = 0;

int radiohead_init(void)
{
    int ce_gpio = mgos_sys_config_get_radiohead_device_ce_gpio();
    int ss_gpio = mgos_sys_config_get_radiohead_device_ss_gpio();
    int irq_gpio = mgos_sys_config_get_radiohead_device_irq_gpio();
    int address = mgos_sys_config_get_radiohead_address();

    if (!radiohead_is_configured())
        return -1;
    if (radiohead_initialized)
        return -1;

    hard_spi = new RHHardwareSPI();
    driver = new RH_NRF24(ce_gpio, ss_gpio, *hard_spi);
    manager = new RHReliableDatagram(*driver, address);

    if (!manager->init()) {
        LOG(LL_ERROR, ("Manager initialization failed"));
        free(driver);
        free(hard_spi);
        return -1;
    }
    config_driver();
    if (irq_gpio >= 0) {
        mgos_gpio_set_mode(irq_gpio, MGOS_GPIO_MODE_INPUT);
        mgos_gpio_set_pull(irq_gpio, MGOS_GPIO_PULL_UP);
        mgos_gpio_set_int_handler(irq_gpio, MGOS_GPIO_INT_EDGE_NEG, nrf24_irq_handler, NULL);
        driver->setModeRx();
        mgos_gpio_enable_int(irq_gpio);
    }

    LOG(LL_INFO, ("RadioHead initialized for device address %d (CE GPIO %d, SS GPIO %d, IRQ GPIO %d)",
                  address, ce_gpio, ss_gpio, irq_gpio));

    if (mgos_sys_config_get_radiohead_sensor_report_address() >= 0)
        mgos_set_timer(200, 0, test_tx, NULL);

    radiohead_initialized = 1;

    return 0;
}

static int8_t radiohead_configured = -1;

int radiohead_is_configured(void)
{
    int addr;

    if (radiohead_configured >= 0)
        return radiohead_configured;
    if (!mgos_sys_config_get_radiohead_enable())
        goto not_enabled;
    addr = mgos_sys_config_get_radiohead_address();
    if (addr < 0) {
        LOG(LL_ERROR, ("radiohead.address not set in config"));
        goto not_enabled;
    }
    if (mgos_sys_config_get_radiohead_device_ce_gpio() < 0 ||
        mgos_sys_config_get_radiohead_device_ss_gpio() < 0) {
        LOG(LL_ERROR, ("radiohead RF device GPIOs not set in config"));
        goto not_enabled;
    }

    radiohead_configured = 1;
    return true;
not_enabled:
    radiohead_configured = 0;
    return false;
}

int radiohead_is_initialized(void)
{
    return radiohead_initialized;
}

struct rh_message {
    int server_addr;
    unsigned int msg_len;
    uint8_t msg[];
};

static void send_message(void *arg)
{
    struct rh_message *rh_msg = (struct rh_message *) arg;

    LOG(LL_DEBUG, ("Sending RH message with %d bytes to addr %d",
                   rh_msg->msg_len, rh_msg->server_addr));
    if (manager->sendtoWait(rh_msg->msg, rh_msg->msg_len, rh_msg->server_addr))
        LOG(LL_ERROR, ("Unable to send message (sendtoWait failed)"));
    free(rh_msg);
}

int radiohead_send_sensor_report(const void *msg, unsigned int msg_len)
{
    struct rh_message *rh_msg;
    int server_addr;

    if (!radiohead_is_initialized()) {
        LOG(LL_ERROR, ("RH not initialized, unable to send sensor report"));
        return -1;
    }
    server_addr = mgos_sys_config_get_radiohead_sensor_report_address();
    if (server_addr < 0) {
        LOG(LL_ERROR, ("RH server address not configured"));
        return -1;
    }

    rh_msg = (struct rh_message *) malloc(sizeof(*rh_msg) + msg_len);
    rh_msg->server_addr = server_addr;
    rh_msg->msg_len = msg_len;
    memcpy(rh_msg->msg, msg, msg_len);
    if (!mgos_invoke_cb(send_message, rh_msg, false)) {
        free(rh_msg);
        return -1;
    }

    return 0;
}
