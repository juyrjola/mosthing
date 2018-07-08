#include <mgos.h>
#include <common/cs_crc32.h>
#include "rfreport.h"


static inline void write_uint32(uint8_t **buf, uint32_t val)
{
    uint8_t *p = *buf;

    *p++ = val >> 24;
    *p++ = val >> 16;
    *p++ = val >> 8;
    *p++ = val;
    *buf = p;
}

static inline void write_uint16(uint8_t **buf, uint16_t val)
{
    uint8_t *p = *buf;

    *p++ = val >> 8;
    *p++ = val;
    *buf = p;
}

static inline uint32_t read_uint32(const uint8_t **buf)
{
    const uint8_t *p = *buf;
    uint32_t val;

    val = (*p++) << 24;
    val |= (*p++) << 16;
    val |= (*p++) << 8;
    val |= *p++;
    *buf = p;

    return val;
}

static inline uint16_t read_uint16(const uint8_t **buf)
{
    const uint8_t *p = *buf;
    uint16_t val;

    val = (*p++) << 8;
    val |= *p++;
    *buf = p;

    return val;
}

void rf_report_encode_msg(uint16_t sensor_id, unsigned int n_observations,
                          const struct rf_sensor_observation *observations,
                          uint8_t **buf_out, unsigned int *buf_len)
{
    uint32_t crc;
    uint8_t *p;
    int len, i;

    len = sizeof(struct rf_sensor_report) + n_observations * sizeof(struct rf_sensor_observation);
    p = *buf_out = malloc(len);
    *buf_len = (uint32_t) len;

    *p++ = (RF_PROTOCOL_V1 << 4) | n_observations;
    *p++ = 0; // padding
    write_uint16(&p, sensor_id);

    for (i = 0; i < (int) n_observations; i++) {
        const struct rf_sensor_observation *obs = observations + i;
        uint32_t val;

        *p++ = obs->phenomenon;
        *p++ = obs->value_type << 4;
        val = (uint32_t) obs->value.int_val;
        write_uint32(&p, val);
    }

    printf("encode: calculating crc for bytes: ");
    for (i = 0; i < len - 4; i++)
        printf("%02x ", (*buf_out)[i]);
    printf("\n");
    crc = cs_crc32(0, *buf_out, len - 4);
    write_uint32(&p, crc);
    assert((p - *buf_out) == len);
}

int rf_report_decode_msg(const uint8_t *buf, unsigned int buf_len,
                         struct rf_sensor_report **out)
{
    const uint8_t *p;
    struct rf_sensor_report *rep;
    uint32_t msg_crc, crc;
    int proto_ver, n_observations, msg_len, i;

    if (buf_len < 1)
        return -1;
    p = buf;
    proto_ver = *p >> 4;
    if (proto_ver != RF_PROTOCOL_V1) {
        LOG(LL_ERROR, ("invalid RF report protocol version: %d", proto_ver));
    }
    n_observations = *p++ & 0x0f;
    msg_len = sizeof(struct rf_sensor_report) + n_observations * sizeof(struct rf_sensor_observation);
    if ((int) buf_len < msg_len) {
        LOG(LL_ERROR, ("incoming RF report message too short (%d bytes, need %d)", buf_len, msg_len));
        return -1;
    }
    rep = (struct rf_sensor_report *) malloc(msg_len);
    rep->proto_ver = proto_ver;
    rep->n_observations = n_observations;
    p++; // padding
    rep->sensor_id = read_uint16(&p);
    for (i = 0; i < n_observations; i++) {
        struct rf_sensor_observation *obs = rep->observations + i;

        obs->phenomenon = *p++;
        obs->value_type = *p++ >> 4;
        obs->value.int_val = (int32_t) read_uint32(&p);
    }

    crc = cs_crc32(0, buf, p - buf);
    printf("decode: calculating crc for bytes: ");
    for (i = 0; i < p - buf; i++)
        printf("%02x ", buf[i]);
    printf("\n");
    msg_crc = read_uint32(&p);
    if (crc != msg_crc) {
        LOG(LL_ERROR, ("CRC32 check failed (expected 0x%08x, got 0x%08x)", crc, msg_crc));
        free(rep);
        return -1;
    }
    rep->crc = crc;

    *out = rep;

    return 0;       
}



void rfreport_test(void)
{
    struct rf_sensor_observation obs[2];
    struct rf_sensor_report *out;
    int i;
    uint8_t *buf;
    unsigned int buf_len;

    obs[0].phenomenon = RF_PHENOMENON_TEMPERATURE;
    obs[0].value_type = RF_VALUE_FLOAT;
    obs[0].value.float_val = 23.1;

    obs[1].phenomenon = RF_PHENOMENON_HUMIDITY;
    obs[1].value_type = RF_VALUE_FLOAT;
    obs[1].value.float_val = 78.9231;

    buf_len = 0;
    rf_report_encode_msg(0x1234, 2, obs, &buf, &buf_len);

    for (i = 0; i < (int) buf_len; i++) {
        printf("%02x ", buf[i]);
    }
    printf("\n");

    if (rf_report_decode_msg(buf, buf_len, &out) < 0)
        return;
    printf("temp %f, hum %f\n", out->observations[0].value.float_val, out->observations[1].value.float_val);
    free(out);
    free(buf);
}
