/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <rtthread.h>

#include "carplay_mfi_provider.h"
#include "rtcp/rtcp.h"
#include "rtcp_lubanlite.h"

#if defined(RTCARPLAY_DEMO_MFI_SOFTWARE) || defined(RTCARPLAY_DEMO_MFI_EXTERNAL)
static struct rt_mutex s_lock;
static const rtcp_mfi_provider_t *s_provider;

static int init_provider_lock(void)
{
    return rt_mutex_init(&s_lock, "cpauth", RT_IPC_FLAG_PRIO);
}
INIT_PREV_EXPORT(init_provider_lock);

#if defined(RTCARPLAY_DEMO_MFI_SOFTWARE)
#include "rtcp_rtthread.h"

RT_WEAK int carplay_product_mfi_load(rtcp_mfi_software_config_t *identity)
{
    (void)identity;
    return RTCP_ENOENT;
}

RT_WEAK void carplay_product_mfi_release(rtcp_mfi_software_config_t *identity)
{
    (void)identity;
}

static int provider_random(void *context, uint8_t *output, size_t length)
{
    (void)context;
    return rtcp_lubanlite_random(output, length) ? RTCP_OK : RTCP_EIO;
}

static int provider_create(const rtcp_mfi_provider_t **provider)
{
    rtcp_mfi_software_config_t identity = { 0 };
    int result = carplay_product_mfi_load(&identity);
    if (result == RTCP_OK)
    {
        identity.random = provider_random;
        identity.random_context = NULL;
        identity.os = &rtcp_os_rtthread;
        rtcp_mfi_software_t *software = NULL;
        result = rtcp_mfi_software_create(&identity, &software);
        if (result == RTCP_OK)
            *provider = rtcp_mfi_software_provider(software);
    }
    carplay_product_mfi_release(&identity);
    return result;
}
#else
RT_WEAK int carplay_product_mfi_external(const rtcp_mfi_provider_t **provider)
{
    (void)provider;
    return RTCP_ENOENT;
}

static int provider_create(const rtcp_mfi_provider_t **provider)
{
    int result = carplay_product_mfi_external(provider);
    if (result != RTCP_OK)
        return result;
    if (!*provider || !(*provider)->get_info || !(*provider)->sign || !(*provider)->read_cert)
        return RTCP_EINVAL;
    rtcp_mfi_provider_info_t info;
    result = (*provider)->get_info((*provider)->context, &info);
    if (result != RTCP_OK)
        return result;
    const uint32_t required = RTCP_MFI_CAP_IAP2 | RTCP_MFI_CAP_SAP;
    if (info.kind != RTCP_MFI_PROVIDER_EXTERNAL || (info.capabilities & required) != required ||
        (info.protocol_major != 2U && info.protocol_major != 3U))
        return RTCP_ENOTSUP;
    return RTCP_OK;
}
#endif

int carplay_mfi_provider_get(const rtcp_mfi_provider_t **provider, bool prepare)
{
    (void)prepare;
    *provider = NULL;
    if (rt_mutex_take(&s_lock, RT_WAITING_FOREVER) != RT_EOK)
        return RTCP_EIO;
    int result = RTCP_OK;
    if (!s_provider)
    {
        const rtcp_mfi_provider_t *candidate = NULL;
        result = provider_create(&candidate);
        if (result == RTCP_OK)
            s_provider = candidate;
    }
    *provider = s_provider;
    rt_mutex_release(&s_lock);
    return result;
}
#elif defined(LPKG_RT_CARPLAY_LUBANLITE_MFI)
static const struct rtcp_mfi *hardware_open(int *result)
{
    *result = rtcp_mfi_lubanlite_start(RTCARPLAY_MFI_I2C_BUS, RTCARPLAY_MFI_I2C_ADDR);
    return *result == RTCP_OK ? rtcp_mfi_lubanlite() : NULL;
}

static int hardware_info(void *context, rtcp_mfi_provider_info_t *out)
{
    (void)context;
    int result;
    const struct rtcp_mfi *hardware = hardware_open(&result);
    if (!hardware)
        return result;
    struct rtcp_mfi_info detected;
    result = hardware->info(&detected);
    if (result != RTCP_OK)
        return result;
    *out = (rtcp_mfi_provider_info_t){
        .kind = RTCP_MFI_PROVIDER_HARDWARE,
        .capabilities = RTCP_MFI_CAP_IAP2 | RTCP_MFI_CAP_SAP,
        .protocol_major = detected.id32 == 0x00000300U ? 3U : 2U,
        .certificate_len = detected.cert_len,
    };
    return RTCP_OK;
}

static int hardware_sign(void *context, const uint8_t *challenge, size_t len,
                         uint8_t *sig, size_t cap, size_t *written)
{
    (void)context;
    int result;
    const struct rtcp_mfi *hardware = hardware_open(&result);
    return hardware ? hardware->sign(challenge, len, sig, cap, written) : result;
}

static int hardware_cert(void *context, uint8_t *cert, size_t cap, size_t *written)
{
    (void)context;
    int result;
    const struct rtcp_mfi *hardware = hardware_open(&result);
    return hardware ? hardware->read_cert(cert, cap, written) : result;
}

int carplay_mfi_provider_get(const rtcp_mfi_provider_t **provider, bool prepare)
{
    static const rtcp_mfi_provider_t hardware = {
        .get_info = hardware_info,
        .sign = hardware_sign,
        .read_cert = hardware_cert,
    };
    if (prepare)
        (void)rtcp_mfi_lubanlite_start(RTCARPLAY_MFI_I2C_BUS, RTCARPLAY_MFI_I2C_ADDR);
    *provider = &hardware;
    return RTCP_OK;
}
#else
int carplay_mfi_provider_get(const rtcp_mfi_provider_t **provider, bool prepare)
{
    (void)prepare;
    *provider = NULL;
    return RTCP_ENOTSUP;
}
#endif
