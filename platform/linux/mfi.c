#define _DEFAULT_SOURCE
#include "linux_demo.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mbedtls/platform_util.h"
#include "rtcp/mfi_software.h"
#include "rtcp_linux.h"

#ifdef CARPLAY_MFI_HARDWARE
#include <linux/i2c-dev.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include "mfi2c.h"
#endif

struct demo_mfi
{
#ifdef CARPLAY_MFI_HARDWARE
    int bus_fd;
    pthread_mutex_t bus_lock;
    rtcp_mfi2c_t chip;
    rtcp_mfi_provider_t hardware;
    rtcp_mfi_provider_info_t hardware_info;
#endif
    rtcp_mfi_software_t *software;
};

#ifdef CARPLAY_MFI_HARDWARE
static int write_register(void *context, uint8_t reg, const uint8_t *bytes, size_t length)
{
    demo_mfi_t *mfi = context;
    uint8_t packet[1 + RTCP_MFI_CERT_MAX];
    if (length >= sizeof(packet))
        return RTCP_EINVAL;
    packet[0] = reg;
    if (length)
        memcpy(packet + 1, bytes, length);
    return write(mfi->bus_fd, packet, length + 1) == (ssize_t)(length + 1) ? RTCP_OK : RTCP_EIO;
}

static void delay(void *context, unsigned ms)
{
    (void)context;
    rtcp_os_linux.sleep_ms(ms);
}

static int read_register(void *context, uint8_t reg, uint8_t *bytes, size_t length,
                         int retries, int delay_ms)
{
    demo_mfi_t *mfi = context;
    for (int attempt = 0; attempt < retries; ++attempt)
    {
        if (write_register(context, reg, NULL, 0) == RTCP_OK)
        {
            delay(context, (unsigned)delay_ms);
            if (read(mfi->bus_fd, bytes, length) == (ssize_t)length)
                return RTCP_OK;
        }
        else
            delay(context, (unsigned)delay_ms);
    }
    return RTCP_EIO;
}

static int info(void *context, rtcp_mfi_provider_info_t *output)
{
    const demo_mfi_t *mfi = context;
    *output = mfi->hardware_info;
    return RTCP_OK;
}

static int sign(void *context, const uint8_t *challenge, size_t length, uint8_t *signature,
                size_t capacity, size_t *written)
{
    demo_mfi_t *mfi = context;
    pthread_mutex_lock(&mfi->bus_lock);
    int result = rtcp_mfi2c_sign(&mfi->chip, challenge, length, signature, capacity, written);
    pthread_mutex_unlock(&mfi->bus_lock);
    return result;
}

static int certificate(void *context, uint8_t *der, size_t capacity, size_t *written)
{
    demo_mfi_t *mfi = context;
    pthread_mutex_lock(&mfi->bus_lock);
    int result = rtcp_mfi2c_read_cert(&mfi->chip, der, capacity, written);
    pthread_mutex_unlock(&mfi->bus_lock);
    return result;
}

static int hardware_open(demo_mfi_t *mfi, const char *device)
{
    static const struct rtcp_mfi2c_bus operations = { write_register, read_register, delay };
    mfi->bus_fd = open(device, O_RDWR | O_CLOEXEC);
    if (mfi->bus_fd < 0 || ioctl(mfi->bus_fd, I2C_SLAVE, RTCP_MFI2C_ADDR_DEFAULT) < 0)
        return RTCP_EIO;
    struct rtcp_mfi_info detected;
    rtcp_mfi2c_init(&mfi->chip, &operations, mfi, RTCP_MFI2C_ADDR_DEFAULT);
    int result = rtcp_mfi2c_probe(&mfi->chip, &detected);
    if (result != RTCP_OK)
        return result;
    if (detected.id32 != 0x00000200U && detected.id32 != 0x00000300U && detected.cert_len != 908U)
        return RTCP_ENOTSUP;
    mfi->hardware_info = (rtcp_mfi_provider_info_t){
        .kind = RTCP_MFI_PROVIDER_HARDWARE,
        .capabilities = RTCP_MFI_CAP_IAP2 | RTCP_MFI_CAP_SAP,
        .protocol_major = mfi->chip.auth_major,
        .certificate_len = detected.cert_len,
    };
    mfi->hardware = (rtcp_mfi_provider_t){
        .context = mfi,
        .get_info = info,
        .sign = sign,
        .read_cert = certificate,
    };
    return RTCP_OK;
}
#else
static int hardware_open(demo_mfi_t *mfi, const char *device)
{
    (void)mfi;
    (void)device;
    return RTCP_ENOTSUP;
}
#endif

static int load_file(const char *path, uint8_t *buffer, size_t capacity, size_t *length, bool private)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return errno == ENOENT ? RTCP_ENOENT : RTCP_EIO;
    int result = RTCP_EINVAL;
    struct stat attributes;
    if (fstat(fd, &attributes) == 0 && S_ISREG(attributes.st_mode) &&
        attributes.st_size > 0 && (uintmax_t)attributes.st_size <= capacity &&
        (!private || (attributes.st_uid == geteuid() && (attributes.st_mode & 0077) == 0)))
    {
        size_t used = 0;
        size_t expected = (size_t)attributes.st_size;
        while (used < expected)
        {
            ssize_t count = read(fd, buffer + used, expected - used);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                break;
            used += (size_t)count;
        }
        uint8_t extra;
        ssize_t count;
        do
            count = read(fd, &extra, sizeof(extra));
        while (count < 0 && errno == EINTR);
        if (used == expected && count == 0)
        {
            *length = used;
            result = RTCP_OK;
        }
        else
            result = RTCP_EIO;
    }
    close(fd);
    return result;
}

static int software_open(demo_mfi_t *mfi, const demo_options_t *options)
{
    uint8_t cert[RTCP_MFI_WIRE_CERT_MAX];
    uint8_t key[RTCP_MFI_SOFTWARE_KEY_MAX];
    rtcp_mfi_software_config_t config = {
        .certificate = cert,
        .private_key = key,
        .protocol_major = options->mfi_protocol,
        .os = &rtcp_os_linux,
    };
    int result = load_file(options->mfi_cert, cert, sizeof(cert), &config.certificate_len, false);
    if (result == RTCP_OK)
        result = load_file(options->mfi_key, key, sizeof(key), &config.private_key_len, true);
    if (result == RTCP_OK)
        result = rtcp_mfi_software_create(&config, &mfi->software);
    mbedtls_platform_zeroize(key, sizeof(key));
    return result;
}

int demo_mfi_open(const demo_options_t *options, demo_mfi_t **out)
{
    bool software = options->mfi_mode == DEMO_MFI_SOFTWARE ||
                    (options->mfi_mode == DEMO_MFI_AUTO && options->mfi_cert);
    demo_mfi_t *mfi = calloc(1, sizeof(*mfi));
    if (!mfi)
        return RTCP_ENOMEM;
#ifdef CARPLAY_MFI_HARDWARE
    mfi->bus_fd = -1;
    if (pthread_mutex_init(&mfi->bus_lock, NULL) != 0)
    {
        free(mfi);
        return RTCP_EIO;
    }
#endif
    int result = software ? software_open(mfi, options) : hardware_open(mfi, options->i2c);
    if (result != RTCP_OK)
    {
        fprintf(stderr, "carplay: MFi %s provider unavailable (%d)\n",
                software ? "software" : "hardware", result);
        demo_mfi_close(mfi);
        return result;
    }
    const rtcp_mfi_provider_t *provider = demo_mfi_provider(mfi);
    rtcp_mfi_provider_info_t metadata;
    result = provider->get_info(provider->context, &metadata);
    if (result != RTCP_OK)
    {
        demo_mfi_close(mfi);
        return result;
    }
    fprintf(stderr, "carplay: MFi %s provider ready (protocol %u)\n",
            software ? "software" : "hardware", (unsigned)metadata.protocol_major);
    *out = mfi;
    return RTCP_OK;
}

const rtcp_mfi_provider_t *demo_mfi_provider(const demo_mfi_t *mfi)
{
    if (!mfi)
        return NULL;
    if (mfi->software)
        return rtcp_mfi_software_provider(mfi->software);
#ifdef CARPLAY_MFI_HARDWARE
    return &mfi->hardware;
#else
    return NULL;
#endif
}

void demo_mfi_close(demo_mfi_t *mfi)
{
    if (!mfi)
        return;
    rtcp_mfi_software_destroy(mfi->software);
#ifdef CARPLAY_MFI_HARDWARE
    if (mfi->bus_fd >= 0)
        close(mfi->bus_fd);
    pthread_mutex_destroy(&mfi->bus_lock);
#endif
    free(mfi);
}
