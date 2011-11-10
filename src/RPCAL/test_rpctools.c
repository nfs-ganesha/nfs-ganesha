/*****
 * test the rpctools.c sockaddr_t functions.
 */

#include "config.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>  
#include <netinet/in.h>
#include <arpa/inet.h>
#include "rpcal.h"

int fridgethr_get( pthread_t * pthrid, void *(*thrfunc)(void*), void * thrarg )
{
  return 0;
}

void *rpc_tcp_socket_manager_thread(void *Arg)
{
  return NULL;
}


void Fatal(void) 
{
    return;
}

int DisplayLogComponentLevel(log_components_t component,
                             char *function,
                             log_levels_t  level,
                             char *format, ...)
{
    return 0;
}

void GetNameFunction(char *name, int len)
{
}

#define EQUALS(a, b, msg) do {                    \
  if (a != b) {                             \
      printf(msg "\n");                           \
      exit(1);                                    \
    }                                             \
} while(0)

#define CMP(a, b, n, msg) do {               \
  if (strncmp(a, b, n) != 0) {               \
      printf(msg "\n");                           \
      exit(1);                                    \
    }                                             \
} while(0)

sockaddr_t ipv4a;
sockaddr_t ipv4b;
sockaddr_t ipv4c;
sockaddr_t ipv6a;
sockaddr_t ipv6b;
sockaddr_t ipv6c;

void create_ipv4(char * ip, int port, struct sockaddr_in * addr) 
{
    memset(addr, 0, sizeof(struct sockaddr_in));
    addr->sin_family = AF_INET;
    addr->sin_port = port;
    inet_pton(AF_INET, ip, &(addr->sin_addr));
}

void create_ipv6(char * ip, int port, struct sockaddr_in6 * addr) 
{
    memset(addr, 0, sizeof(struct sockaddr_in6));
    addr->sin6_family = AF_INET6;
    addr->sin6_port = port;
    inet_pton(AF_INET6, ip, &(addr->sin6_addr.s6_addr));
}

void init() {
    create_ipv4("10.10.5.1", 2048, (struct sockaddr_in * ) &ipv4a);
    create_ipv4("10.10.5.1", 2049, (struct sockaddr_in * ) &ipv4b);
    create_ipv4("10.10.5.2", 2048, (struct sockaddr_in * ) &ipv4c);

#ifdef _USE_TIRPC
    create_ipv6("2001::1", 2048, (struct sockaddr_in6 *) &ipv6a);
    create_ipv6("2001::1", 2049, (struct sockaddr_in6 *) &ipv6b);
    create_ipv6("2001::f:1", 2048, (struct sockaddr_in6 *) &ipv6c);
#endif    
}

void ipv4check() {
    printf("Value = %lu\n", hash_sockaddr(&ipv4a, 0));
    printf("Value = %lu\n", hash_sockaddr(&ipv4b, 0));
    printf("Value = %lu\n", hash_sockaddr(&ipv4c, 0));
    EQUALS(hash_sockaddr(&ipv4a, IGNORE_PORT), 17107466, "ipv4a doesn't hash as expected");
    EQUALS(hash_sockaddr(&ipv4b, IGNORE_PORT), 17107466, "ipv4b doesn't hash as expected");
    EQUALS(hash_sockaddr(&ipv4c, IGNORE_PORT), 33884682, "ipv4c doesn't hash as expected");
    EQUALS(hash_sockaddr(&ipv4a, CHECK_PORT), 151325194, "ipv4a doesn't hash as expected");
    EQUALS(hash_sockaddr(&ipv4b, CHECK_PORT), 151259658, "ipv4b doesn't hash as expected");
    EQUALS(hash_sockaddr(&ipv4c, CHECK_PORT), 168102410, "ipv4c doesn't hash as expected");
}

void ipv6check() {
    printf("Value = %lu\n", hash_sockaddr(&ipv6a, 0));
    printf("Value = %lu\n", hash_sockaddr(&ipv6b, 0));
    printf("Value = %lu\n", hash_sockaddr(&ipv6c, 0));
    EQUALS(hash_sockaddr(&ipv6a, CHECK_PORT), 150995232, "ipv6a doesn't hash as expected");
    EQUALS(hash_sockaddr(&ipv6b, CHECK_PORT), 151060768, "ipv6b doesn't hash as expected");
    EQUALS(hash_sockaddr(&ipv6c, CHECK_PORT), 150998560, "ipv6c doesn't hash as expected");
}

void ipv4print() {
    char buf[SOCK_NAME_MAX];
    sprint_sockip(&ipv4a, buf, sizeof(buf));
    // printf("Value = %s\n", buf);
    CMP("10.10.5.1", buf, strlen("10.10.5.1"), "ipv4a has the wrong ip value");

    sprint_sockip(&ipv4b, buf, sizeof(buf));
    // printf("Value = %s\n", buf);
    CMP("10.10.5.1", buf, strlen("10.10.5.1"), "ipv4b has the wrong ip value");

    sprint_sockip(&ipv4c, buf, sizeof(buf));
    // printf("Value = %s\n", buf);
    CMP("10.10.5.2", buf, strlen("10.10.5.2"), "ipv4c has the wrong ip value");
}

void ipv6print() {
    char buf[SOCK_NAME_MAX];
    sprint_sockip(&ipv6a, buf, sizeof(buf));
    // printf("Value = %s\n", buf);
    CMP("2001::1", buf, strlen("2001::1"), "ipv6a has the wrong ip value");

    sprint_sockip(&ipv6b, buf, sizeof(buf));
    // printf("Value = %s\n", buf);
    CMP("2001::1", buf, strlen("2001::1"), "ipv6b has the wrong ip value");

    sprint_sockip(&ipv6c, buf, sizeof(buf));
    // printf("Value = %s\n", buf);
    CMP("2001::f:1", buf, strlen("2001::f:1"), "ipv6c has the wrong ip value");
}

void ipv4cmp() {
    EQUALS(cmp_sockaddr(&ipv4a, &ipv4b, IGNORE_PORT), 1, "ipv4a comapred to ipv4b no port");
    EQUALS(cmp_sockaddr(&ipv4a, &ipv4b, CHECK_PORT), 0, "ipv4a comapred to ipv4b with port");
    EQUALS(cmp_sockaddr(&ipv4a, &ipv4c, IGNORE_PORT), 0, "ipv4a comapred to ipv4c no port");
    EQUALS(cmp_sockaddr(&ipv4a, &ipv4c, CHECK_PORT), 0, "ipv4a comapred to ipv4c with port");
}

void ipv6cmp() {
    EQUALS(cmp_sockaddr(&ipv6a, &ipv6b, IGNORE_PORT), 1, "ipv6a comapred to ipv6b no port");
    EQUALS(cmp_sockaddr(&ipv6a, &ipv6b, CHECK_PORT), 0, "ipv6a comapred to ipv6b with port");
    EQUALS(cmp_sockaddr(&ipv6a, &ipv6c, IGNORE_PORT), 0, "ipv6a comapred to ipv6c no port");
    EQUALS(cmp_sockaddr(&ipv6a, &ipv6c, CHECK_PORT), 0, "ipv6a comapred to ipv6c with port");
}

int main()
{
    init();
    ipv4check();
    ipv4print();
    ipv4cmp();

#ifdef _USE_TIRPC
    ipv6check();
    ipv6print();
    ipv6cmp();
#endif

    return 0;
}
