#ifndef CARPLAY_BUILD_INFO_H
#define CARPLAY_BUILD_INFO_H

typedef struct
{
    const char *sdk_version;
    const char *rtcarplay_version;
    const char *commit;
    const char *time_utc;
} carplay_build_info_t;

extern const carplay_build_info_t carplay_build_info;

#endif
