/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtcp_linux.h"
#include "mbedtls/chachapoly.h"
#include "airplay_audio_sender.h"
static int test_sender_create(const airplay_audio_sender_config_t *, airplay_audio_sender_t **);
#define airplay_audio_sender_create test_sender_create
#include "../../platform/audio_input_lubanlite.c"
#undef airplay_audio_sender_create
extern int host_rtthread_fail_start;
extern atomic_bool host_rtthread_hold_entry;
void host_rtthread_join(void);
struct rtcp_audio_capture { unsigned phase; };
static struct host_audio_device mic_device;
static atomic_uint source_opens,source_closes;
static bool stop_during_probe;
static atomic_bool hold_capture, capture_entered, stop_on_open, self_stop;
static atomic_int self_result;
static int test_sender_create(const airplay_audio_sender_config_t *config, airplay_audio_sender_t **out) {
 int result=airplay_audio_sender_create(config,out);
 if(stop_during_probe) {stop_during_probe=false;carplay_audio_input_request_stop();}
 return result;
}
rt_device_t rt_device_find(const char *name) { assert(!strcmp(name,"dmic0"));return &mic_device; }
int rtcp_audio_capture_open(rtcp_audio_capture_t **out) {
 *out=calloc(1,sizeof(**out));++source_opens;
 if(atomic_exchange(&self_stop,false))self_result=carplay_audio_input_stop();
 if(atomic_exchange(&stop_on_open,false))carplay_audio_input_request_stop();
 return 0;
}
int rtcp_audio_capture_read(rtcp_audio_capture_t *c,int16_t *pcm,size_t frames) {
 if(atomic_load(&hold_capture)) {capture_entered=true;while(atomic_load(&hold_capture))rt_thread_mdelay(1);}
 assert(frames==480);for(unsigned i=0;i<480;++i) {int16_t s=(int16_t)((c->phase++%48)*100-2400);pcm[2*i]=pcm[2*i+1]=s;}
 rt_thread_mdelay(10);return 480;
}
void rtcp_audio_capture_close(rtcp_audio_capture_t *c){if(c){++source_closes;free(c);}}
static void run_input(uint64_t bits)
{
 int fd=rtcp_net_linux.udp_open(RTCP_AF_INET,0,false);assert(fd>=0);
 airplay_audio_input_config_t config={.net=&rtcp_net_linux};
 config.peer=(rtcp_addr_t){.family=RTCP_AF_INET,.addr={127,0,0,1},.port=rtcp_net_linux.local_port(fd)};
 config.sender.payload_type=100;memset(config.sender.input_key,0x55,32);
 assert(rtcp_audio_format_parse(bits,&config.sender.format));
 assert(carplay_audio_input_start(&config));
 uint8_t wire[1500],data[1500];rtcp_addr_t source;
 mbedtls_chachapoly_context cipher;mbedtls_chachapoly_init(&cipher);
 assert(mbedtls_chachapoly_setkey(&cipher,config.sender.input_key)==0);
 for(unsigned i=0;i<4;++i) {
  struct rtcp_pollfd p={.fd=fd,.events=RTCP_POLLIN};assert(rtcp_net_linux.poll(&p,1,2000)>0);
  int n=rtcp_net_linux.udp_recvfrom(fd,wire,sizeof(wire),&source);assert(n>36 && n<=1452);
  uint8_t nonce[12]={0};memcpy(nonce+4,wire+n-8,8);assert(nonce[4]==i);
  assert(wire[0]==0x80 && wire[1]==100 && wire[3]==i);
  assert(mbedtls_chachapoly_auth_decrypt(&cipher,(size_t)n-36,nonce,wire+4,8,wire+n-24,wire+12,data)==0);
  if(bits==0x4000)assert(n==996); /* 10 ms at 48k mono */
 }
 carplay_audio_input_mute(true);carplay_audio_input_request_stop();
 assert(carplay_audio_input_stop()==RTCP_OK);host_rtthread_join();
 assert(!atomic_load(&input.alive));
 mbedtls_chachapoly_free(&cipher);rtcp_net_linux.close(fd);
}
int main(void)
{
 self_stop=true;self_result=RTCP_OK;
 run_input(0x4000);
 assert(self_result==RTCP_ESTATE);
#ifdef RTCP_AUDIO_CODECS
 run_input(0x20000000);
#endif
 airplay_audio_input_config_t config={.net=&rtcp_net_linux};
 int fd=rtcp_net_linux.udp_open(RTCP_AF_INET,0,false);assert(fd>=0);
 config.peer=(rtcp_addr_t){.family=RTCP_AF_INET,.addr={127,0,0,1},.port=rtcp_net_linux.local_port(fd)};
 assert(rtcp_audio_format_parse(0x4000,&config.sender.format));
 memset(config.sender.input_key,0x55,32);
 host_rtthread_fail_start=1;
 assert(!carplay_audio_input_start(&config));
 assert(!input.alive && !input.config.net);host_rtthread_fail_start=0;
 assert(!atomic_flag_test_and_set(&input_lifecycle));
 assert(!carplay_audio_input_start(&config));
 assert(carplay_audio_input_stop()==RTCP_ESTATE);
 atomic_flag_clear(&input_lifecycle);
 unsigned before=source_opens;
 stop_during_probe=true;
 assert(carplay_audio_input_start(&config));
 assert(atomic_load(&input.stop));
 for(unsigned i=0;i<3000 && input.alive;++i)rt_thread_mdelay(1);
 assert(!input.alive);
 assert(carplay_audio_input_stop()==RTCP_OK);host_rtthread_join();
 assert(source_opens==before); /* request during validation survives reset */
 stop_on_open=true;
 assert(carplay_audio_input_start(&config));
 for(unsigned i=0;i<3000 && input.alive;++i)rt_thread_mdelay(1);
 assert(!input.alive);host_rtthread_join();
 assert(source_opens==before+1 && source_opens==source_closes);
 assert(carplay_audio_input_stop()==RTCP_OK);

 hold_capture=true;capture_entered=false;
 assert(carplay_audio_input_start(&config));
 for(unsigned i=0;i<3000 && !capture_entered;++i)rt_thread_mdelay(1);
 assert(capture_entered);
 assert(carplay_audio_input_stop()==RTCP_ETIMEDOUT);
 assert(input.alive && input.config.net==config.net && source_opens==source_closes+1);
 assert(!carplay_audio_input_start(&config));
 hold_capture=false;host_rtthread_join();
 assert(carplay_audio_input_stop()==RTCP_OK && !input.config.net);
 for(unsigned cycle=0;cycle<32;++cycle) {
  before=source_opens;host_rtthread_hold_entry=true;
  assert(carplay_audio_input_start(&config));
  carplay_audio_input_request_stop();host_rtthread_hold_entry=false;
  assert(carplay_audio_input_stop()==RTCP_OK);host_rtthread_join();
  assert(source_opens==before && !input.alive && !input.config.net);
 }
 rtcp_net_linux.close(fd);
 assert(source_opens==source_closes);
 puts("actual microphone worker: virtual capture -> voice DSP -> PCM/Opus -> authenticated UDP, stop/restart passed");
 puts("microphone lifecycle: rollback, contention, self-join, startup cancellation, timeout ownership and 32 pre-scheduling stops passed");
}
