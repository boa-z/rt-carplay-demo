#define _DEFAULT_SOURCE
#include "linux_demo.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "airplay_mfi_sap.h"
#include "mfi2c.h"
#include "rtcp_linux.h"

static int bus_fd = -1;
static rtcp_mfi2c_t chip;
static pthread_mutex_t bus_lock = PTHREAD_MUTEX_INITIALIZER;

static int write_register(void *context, uint8_t reg, const uint8_t *bytes, size_t length)
{
    (void)context;
    uint8_t packet[1 + RTCP_MFI_CERT_MAX];
    if (length >= sizeof(packet))
        return RTCP_EINVAL;
    packet[0] = reg;
    if (length)
        memcpy(packet + 1, bytes, length);
    return write(bus_fd, packet, length + 1) == (ssize_t)(length + 1) ? RTCP_OK : RTCP_EIO;
}

static void delay(void *context, unsigned ms)
{
    (void)context;
    rtcp_os_linux.sleep_ms(ms);
}

static int read_register(void *context, uint8_t reg, uint8_t *bytes, size_t length,
                         int retries, int delay_ms)
{
    for (int attempt = 0; attempt < retries; ++attempt)
    {
        if (write_register(context, reg, NULL, 0) == RTCP_OK)
        {
            delay(context, (unsigned)delay_ms);
            if (read(bus_fd, bytes, length) == (ssize_t)length)
                return RTCP_OK;
        }
        else
            delay(context, (unsigned)delay_ms);
    }
    return RTCP_EIO;
}

static int info(struct rtcp_mfi_info *output)
{
    pthread_mutex_lock(&bus_lock);
    int result = rtcp_mfi2c_probe(&chip, output);
    pthread_mutex_unlock(&bus_lock);
    return result;
}

static int sign(const uint8_t *challenge, size_t length, uint8_t *signature,
                size_t capacity, size_t *written)
{
    pthread_mutex_lock(&bus_lock);
    int result = rtcp_mfi2c_sign(&chip, challenge, length, signature, capacity, written);
    pthread_mutex_unlock(&bus_lock);
    return result;
}

static int certificate(uint8_t *der, size_t capacity, size_t *written)
{
    pthread_mutex_lock(&bus_lock);
    int result = rtcp_mfi2c_read_cert(&chip, der, capacity, written);
    pthread_mutex_unlock(&bus_lock);
    return result;
}

const struct rtcp_mfi demo_mfi = { info, sign, certificate };

static int random_bytes(uint8_t *output, size_t length, void *context)
{
    (void)context;
    return rtcp_os_linux.random(output, length) ? RTCP_OK : RTCP_EIO;
}

int demo_mfi_open(const char *device)
{
    static const struct rtcp_mfi2c_bus operations = { write_register, read_register, delay };
    bus_fd = open(device, O_RDWR | O_CLOEXEC);
    if (bus_fd < 0)
        return RTCP_EIO;
    struct rtcp_mfi_info detected;
    rtcp_mfi2c_init(&chip, &operations, NULL, RTCP_MFI2C_ADDR_DEFAULT);
    if (ioctl(bus_fd, I2C_SLAVE, RTCP_MFI2C_ADDR_DEFAULT) == 0 && info(&detected) == RTCP_OK)
    {
        airplay_mfi_sap_provider_t provider = {
            .mfi = &demo_mfi,
            .protocol_major = chip.auth_major,
            .random = random_bytes,
        };
        if (airplay_mfi_sap_set_provider(&provider))
            return RTCP_OK;
    }
    demo_mfi_close();
    return RTCP_EIO;
}

void demo_mfi_close(void)
{
    airplay_mfi_sap_clear_provider();
    if (bus_fd >= 0)
        close(bus_fd);
    bus_fd = -1;
}
