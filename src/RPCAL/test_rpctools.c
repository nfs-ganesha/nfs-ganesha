/*****
 * test the rpctools.c sockaddr_t functions.
 */

#include "config.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>  
#include <netinet/in.h>
#include <arpa/inet.h>
#include "rpc.h"

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
    create_ipv6("2001::1/64", 2048, (struct sockaddr_in6 *) &ipv6a);
    create_ipv6("2001::1/64", 2049, (struct sockaddr_in6 *) &ipv6b);
    create_ipv6("2001::f:1/64", 2048, (struct sockaddr_in6 *) &ipv6c);
#endif    
}

void ipv4check() {
    printf("Value = %lu\n", hash_sockaddr(&ipv4a));
    printf("Value = %lu\n", hash_sockaddr(&ipv4b));
    printf("Value = %lu\n", hash_sockaddr(&ipv4c));
    printf("Value = %lu\n", hash_sockaddr(&ipv6a));
    printf("Value = %lu\n", hash_sockaddr(&ipv6b));
    printf("Value = %lu\n", hash_sockaddr(&ipv6c));
}

int main()
{
    init();
    ipv4check();
}
