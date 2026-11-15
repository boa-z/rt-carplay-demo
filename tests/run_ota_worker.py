#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual OTA application with real SHA256 and modeled RTOS/HTTP/flash."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
sdk = root.parents[2] / "packages/third-party/rt-carplay"
headers = {
    "rtthread.h": r'''
#pragma once
#include <stdint.h>
#include <stdlib.h>
struct rt_mutex { int locked; };
struct test_thread; typedef struct test_thread *rt_thread_t;
#define RT_EOK 0
#define RT_ERROR 1
#define RT_ENOMEM 12
#define RT_EBUSY 16
#define RT_EINVAL 22
#define RT_WAITING_FOREVER (-1)
#define RT_IPC_FLAG_PRIO 1
#define INIT_APP_EXPORT(fn)
#define MSH_CMD_EXPORT_ALIAS(fn, ...) static void *fn##_used __attribute__((used)) = (void *)&fn;
int rt_mutex_init(struct rt_mutex *, const char *, int);
int rt_mutex_take(struct rt_mutex *, int);
int rt_mutex_release(struct rt_mutex *);
int rt_kprintf(const char *, ...);
rt_thread_t rt_thread_create(const char *, void (*)(void *), void *, unsigned, unsigned, unsigned);
int rt_thread_startup(rt_thread_t);
int rt_thread_delete(rt_thread_t);
void rt_thread_mdelay(int);
#define rt_malloc malloc
#define rt_free free
''',
    "rthw.h": "void rt_hw_cpu_reset(void);\n",
    "ota.h": "int ota_init(void); void ota_deinit(void); int ota_shard_download_fun(char *, int);\n",
    "env.h": "int fw_env_open(void); const char *fw_getenv(const char *); void fw_env_close(void);\n",
    "absystem_os.h": "int aic_upgrade_end(void);\n",
    "webclient.h": r'''
#pragma once
#define WEBCLIENT_HEADER_BUFSZ 2048
struct webclient_session { int is_tls, content_length; unsigned offset; int manifest; };
struct webclient_session *webclient_session_create(unsigned);
void webclient_set_timeout(struct webclient_session *, int);
int webclient_get(struct webclient_session *, const char *);
int webclient_read(struct webclient_session *, void *, unsigned long);
int webclient_content_length_get(struct webclient_session *);
void webclient_close(struct webclient_session *);
''',
    "lwip/netif.h": r'''
#pragma once
#include <stdint.h>
struct ip4 { uint32_t addr; };
struct netif { struct ip4 ip, gw; int up, link; };
extern struct netif *netif_default;
struct netif *netif_find(const char *);
#define netif_is_up(n) ((n)->up)
#define netif_is_link_up(n) ((n)->link)
#define netif_ip4_addr(n) (&(n)->ip)
#define netif_ip4_gw(n) (&(n)->gw)
#define ip4_addr_isany_val(a) ((a).addr == 0)
''',
    "lwip/tcpip.h": "extern int core_lock;\n#define LOCK_TCPIP_CORE() (++core_lock)\n#define UNLOCK_TCPIP_CORE() (--core_lock)\n",
}
source = r'''
#include <assert.h>
#include <string.h>
static char stage_path[256];
#define RTCARPLAY_OTA_STAGE stage_path
#define RTCARPLAY_OTA_VERSION_URL "https://example.test/version.txt"
#define RTCARPLAY_OTA_URL "https://example.test/ota.cpio"
#define RTCARPLAY_BUILD_COMMIT aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
#include OTA_SOURCE
struct test_thread { void (*entry)(void *); void *arg; };
static rt_thread_t pending;
static int create_fail, startup_fail, resets, env_reads, flash_init, flash_deinit, flash_bytes, commits;
static int active, read_fail, short_body, corrupt, http_error, tls = 1, wrong_length, flash_fail, commit_fail, flush_fail, activate_after_write;
static unsigned char package[12288];
static char manifest[512];
static struct netif wired = {{1}, {1}, 1, 1}, other;
struct netif *netif_default = &wired;
int core_lock;
struct netif *netif_find(const char *n) { assert(core_lock && !strcmp(n,"e0")); return &wired; }
int rt_mutex_init(struct rt_mutex *m,const char *n,int f) { (void)n;(void)f;m->locked=0;return 0; }
int rt_mutex_take(struct rt_mutex *m,int w) { (void)w;assert(!m->locked);m->locked=1;return 0; }
int rt_mutex_release(struct rt_mutex *m) { assert(m->locked);m->locked=0;return 0; }
int rt_kprintf(const char *s,...) { (void)s;return 0; }
rt_thread_t rt_thread_create(const char *n,void (*entry)(void *),void *a,unsigned s,unsigned p,unsigned t) {
    (void)n;(void)s;(void)p;(void)t;if(create_fail)return NULL;
    rt_thread_t thread=malloc(sizeof(*thread)); assert(thread); *thread=(struct test_thread){entry,a};return thread;
}
int rt_thread_startup(rt_thread_t t) { if(startup_fail)return -1;assert(!pending);pending=t;return 0; }
int rt_thread_delete(rt_thread_t t) { free(t);return 0; }
void rt_thread_mdelay(int ms) { assert(ms==400); }
void rt_hw_cpu_reset(void) { ++resets; }
int fw_env_open(void) { ++env_reads;return 0; }
const char *fw_getenv(const char *name) {
    if (!strcmp(name,"upgrade_available")) return commits && !flush_fail ? "1" : "0";
    if (strstr(name,"_next")) return commits && !flush_fail ? "B" : "A";
    return "A";
}
void fw_env_close(void) {}
bool carplay_app_is_running(void) { return active; }
int carplay_app_session_state(rtcp_receiver_state_t *s) { memset(s,0,sizeof(*s));s->transport_connected=active;return 0; }
int ota_init(void) { ++flash_init;return 0; }
void ota_deinit(void) { ++flash_deinit; }
int ota_shard_download_fun(char *b,int n) { assert(!memcmp(b,package+flash_bytes,(size_t)n));if(flash_fail)return -1;flash_bytes+=n;if(activate_after_write)active=1;return 0; }
int aic_upgrade_end(void) { ++commits;assert(flash_bytes==sizeof(package));return commit_fail ? -1 : 0; }
struct webclient_session *webclient_session_create(unsigned n) { (void)n;return calloc(1,sizeof(struct webclient_session)); }
void webclient_set_timeout(struct webclient_session *s,int n) { (void)s;assert(n==60000); }
int webclient_get(struct webclient_session *s,const char *url) {
    s->manifest=strstr(url,"version.txt")!=NULL;s->is_tls=tls;
    s->content_length=s->manifest?(int)strlen(manifest):(int)sizeof(package)+(wrong_length?1:0);return http_error?500:200;
}
int webclient_read(struct webclient_session *s,void *out,unsigned long cap) {
    carplay_ota_status_t snapshot;carplay_ota_status(&snapshot);assert(snapshot.busy);
    assert(carplay_ota_check()==-RT_EBUSY);assert(carplay_ota_install(snapshot.remote_commit)==-RT_EBUSY);
    if(read_fail && !s->manifest)return -1;
    size_t total=s->manifest?strlen(manifest):sizeof(package)-(short_body?1:0);
    if(s->offset==total)return 0;
    size_t n=total-s->offset;if(n>cap)n=cap;if(n>73)n=73;
    memcpy(out,(s->manifest?(unsigned char *)manifest:package)+s->offset,n);
    if(corrupt && !s->manifest)((unsigned char *)out)[0]^=1;
    s->offset+=(unsigned)n;return (int)n;
}
int webclient_content_length_get(struct webclient_session *s) { return s->content_length; }
void webclient_close(struct webclient_session *s) { free(s); }
static void run(void) { assert(pending);rt_thread_t t=pending;pending=NULL;t->entry(t->arg);free(t);assert(!worker_active); }
static void setup(void) {
    assert(!pending && !worker_active); status.state=CARPLAY_OTA_IDLE;
    create_fail=startup_fail=resets=flash_init=flash_deinit=flash_bytes=commits=0;
    active=read_fail=short_body=corrupt=http_error=wrong_length=flash_fail=commit_fail=flush_fail=activate_after_write=0;tls=1;netif_default=&wired;
    unsigned char sha[32];char hex[65];memset(package,0x5a,sizeof(package));
    assert(!mbedtls_sha256_ret(package,sizeof(package),sha,0));
    for(unsigned i=0;i<32;++i)snprintf(hex+2*i,3,"%02x",sha[i]);
    snprintf(manifest,sizeof(manifest),"version=1.0.0\ncommit=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\nboard=d21x_demo128-nand-carplay\nsize=%u\nsha256=%s\n",(unsigned)sizeof(package),hex);
}
static void available(void) { assert(!carplay_ota_check());assert(carplay_ota_check()==-RT_EBUSY);run();assert(status.state==CARPLAY_OTA_AVAILABLE); }
int main(int argc,char **argv) {
    assert(argc==2);snprintf(stage_path,sizeof(stage_path),"%s/stage.cpio",argv[1]);
    assert(!ota_service_init());carplay_ota_status_t out;carplay_ota_status(&out);assert(!env_reads);
    setup();create_fail=1;assert(carplay_ota_check()==-RT_ENOMEM && !worker_active);
    setup();startup_fail=1;assert(carplay_ota_check()==-RT_ERROR && !worker_active);
    setup();netif_default=&other;assert(!carplay_ota_check());run();assert(status.state==CARPLAY_OTA_FAILED);
    setup();strcat(manifest,"commit=cccccccccccccccccccccccccccccccccccccccc\n");assert(!carplay_ota_check());run();assert(status.state==CARPLAY_OTA_FAILED);
    const char *invalid_sizes[] = {"4294979584", "8388609", "0", "-1"};
    for (unsigned i=0;i<sizeof(invalid_sizes)/sizeof(invalid_sizes[0]);++i) {
        setup();char *size=strstr(manifest,"size=");char tail[100];strcpy(tail,strchr(size,'\n'));
        snprintf(size,sizeof(manifest)-(size-manifest),"size=%s%s",invalid_sizes[i],tail);
        assert(!carplay_ota_check());run();assert(status.state==CARPLAY_OTA_FAILED && !flash_init);
    }
    setup();available();assert(carplay_ota_install("cccccccccccccccccccccccccccccccccccccccc")==-RT_EINVAL);
    for(unsigned mode=0;mode<11;++mode) {
        setup();available();
        active=mode==0;read_fail=mode==1;short_body=mode==2;corrupt=mode==3;wrong_length=mode==4;
        tls=mode!=5;flash_fail=mode==6;commit_fail=mode==7;flush_fail=mode==9;
        activate_after_write=mode==10;
        assert(!carplay_ota_install(status.remote_commit));assert(carplay_ota_check()==-RT_EBUSY);run();
        if(mode<6)assert(!flash_init && !commits && !resets);
        if(mode==6)assert(flash_init==1 && flash_deinit==1 && !commits && !resets);
        if(mode==7)assert(commits==1 && !resets && status.state==CARPLAY_OTA_FAILED);
        if(mode==9)assert(commits==1 && !resets && status.state==CARPLAY_OTA_FAILED);
        if(mode==10)assert(flash_init==1 && flash_deinit==1 && flash_bytes==4096 && !commits && !resets);
        if(mode==8)assert(commits==1 && resets==1 && flash_deinit==1 && status.state==CARPLAY_OTA_REBOOTING);
        else assert(status.state==CARPLAY_OTA_FAILED);
        assert(fopen(stage_path,"rb")==NULL);
    }
    puts("Actual OTA worker: task reservation, target binding, e0 route, strict manifest, EOF/error/hash/length/TLS failures, inactive write/commit failures and success passed; real SHA256, modeled flash only");
}
'''
with tempfile.TemporaryDirectory(prefix="carplay-ota-") as directory:
    path = Path(directory)
    for name, text in headers.items():
        header = path / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text(text)
    (path / "test.c").write_text(source)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-g",
                    '-DOTA_SOURCE="' + str(root / "platform/ota_lubanlite.c") + '"',
                    '-DMBEDTLS_CONFIG_FILE="mbedtls_rtcp_host.h"',
                    *["-I" + str(p) for p in (path, root / "app", sdk / "include", sdk / "config",
                                              sdk / "third_party/mbedtls/include")],
                    str(path / "test.c"), str(sdk / "build/mbedtls/libmbedcrypto.a"),
                    "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test"), directory], check=True)
