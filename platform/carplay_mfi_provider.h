/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CARPLAY_MFI_PROVIDER_H
#define CARPLAY_MFI_PROVIDER_H

#include <stdbool.h>

#include "rtcp/mfi.h"

/* Shared by the Receiver and both transports for the product lifetime.
 * A configured software/external provider is retained after successful loading.
 * Prepare allows the transport worker to try the chip before a phone
 * requests authentication. A failed probe is retried by provider callbacks. */
int carplay_mfi_provider_get(const rtcp_mfi_provider_t **provider, bool prepare);

#ifdef RTCARPLAY_DEMO_MFI_SOFTWARE
#include "rtcp/mfi_software.h"

/* Fill the certificate/key fields with borrowed data in memory. The release
 * hook retires those buffers when create returns, including on failure.
 * The Demo supplies its own TRNG and OS operations. */
int carplay_product_mfi_load(rtcp_mfi_software_config_t *identity);
void carplay_product_mfi_release(rtcp_mfi_software_config_t *identity);
#endif

#ifdef RTCARPLAY_DEMO_MFI_EXTERNAL
/* Return an initialized, serialized provider valid for the product lifetime.
 * Classic CarPlay requires both IAP2 and SAP capabilities. */
int carplay_product_mfi_external(const rtcp_mfi_provider_t **provider);
#endif

#endif
