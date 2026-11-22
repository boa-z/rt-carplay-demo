#include <aic_common.h>
#include "rtcp/version.h"
#include "carplay_build_info.h"

#define BUILD_STRINGIFY_VALUE(value) #value
#define BUILD_STRINGIFY(value)       BUILD_STRINGIFY_VALUE(value)

const carplay_build_info_t carplay_build_info = {
    .sdk_version = BUILD_STRINGIFY(LL_VERSION) "." BUILD_STRINGIFY(LL_SUBVERSION) "." BUILD_STRINGIFY(LL_REVISION),
    .rtcarplay_version = RTCP_VERSION_STRING,
    .commit = BUILD_STRINGIFY(RTCARPLAY_BUILD_COMMIT),
    .time_utc = BUILD_STRINGIFY(RTCARPLAY_BUILD_TIME_UTC),
};
