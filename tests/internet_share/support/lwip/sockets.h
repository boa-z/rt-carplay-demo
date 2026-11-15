#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <unistd.h>
#define lwip_socket test_socket
#define lwip_setsockopt test_setsockopt
#define lwip_getsockopt test_getsockopt
#define lwip_bind test_bind
#define lwip_ioctl test_ioctl
#define lwip_select test_select
#define lwip_connect test_connect
#define lwip_send test_send
#define lwip_recv test_recv
#define lwip_close test_close
int test_socket(int,int,int);
int test_setsockopt(int,int,int,const void *,socklen_t);
int test_getsockopt(int,int,int,void *,socklen_t *);
int test_bind(int,const struct sockaddr *,socklen_t);
int test_ioctl(int,long,void *);
int test_select(int,fd_set *,fd_set *,fd_set *,struct timeval *);
int test_connect(int,const struct sockaddr *,socklen_t);
ssize_t test_send(int,const void *,size_t,int);
ssize_t test_recv(int,void *,size_t,int);
int test_close(int);
