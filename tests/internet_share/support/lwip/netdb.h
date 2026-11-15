#include <netdb.h>
#define lwip_getaddrinfo test_getaddrinfo
#define lwip_freeaddrinfo test_freeaddrinfo
int test_getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
void test_freeaddrinfo(struct addrinfo *);
