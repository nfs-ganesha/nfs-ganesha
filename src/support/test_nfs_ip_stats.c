
#include "rpc.h"
#include "config_parsing.h"
#include "nfs_stat.h"
#include "nfs_ip_stats.h"
#include "stuff_alloc.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "../MainNFSD/nfs_init.h"

hash_table_t * ipstats;
nfs_parameter_t nfs_param;
struct prealloc_pool *ip_stats_pool;

#define EQUALS(a, b, msg, args...) do {             \
  if (a != b) {                             \
      printf(msg "\n", ## args);                          \
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

void nfs_set_ip_stats_param_default()
{

    nfs_param.ip_stats_param.hash_param.index_size = PRIME_IP_STATS;
    nfs_param.ip_stats_param.hash_param.alphabet_length = 10;  /* ipaddr is a numerical decimal value */
    nfs_param.ip_stats_param.hash_param.nb_node_prealloc = NB_PREALLOC_HASH_IP_STATS;
    nfs_param.ip_stats_param.hash_param.hash_func_key = ip_stats_value_hash_func;
    nfs_param.ip_stats_param.hash_param.hash_func_rbt = ip_stats_rbt_hash_func;
    nfs_param.ip_stats_param.hash_param.compare_key = compare_ip_stats;
    nfs_param.ip_stats_param.hash_param.key_to_str = display_ip_stats;
    nfs_param.ip_stats_param.hash_param.val_to_str = display_ip_stats;
    nfs_param.ip_stats_param.hash_param.name = "IP Stats";
    nfs_param.core_param.dump_stats_per_client = 1;

}

void init() 
{
    BuddyInit(NULL);

    nfs_set_ip_stats_param_default();
    ipstats = nfs_Init_ip_stats(nfs_param.ip_stats_param);
    ip_stats_pool = calloc(1, sizeof(struct prealloc_pool));

    MakePool(ip_stats_pool,
             100,//           nfs_param.worker_param.nb_ip_stats_prealloc,
             nfs_ip_stats_t, NULL, NULL);
    NamePool(ip_stats_pool, "IP Stats Cache Pool");

    create_ipv4("10.10.5.1", 2048, (struct sockaddr_in * ) &ipv4a);
    //    create_ipv4("10.10.5.1", 2049, (struct sockaddr_in * ) &ipv4b);
    create_ipv4("10.10.5.2", 2048, (struct sockaddr_in * ) &ipv4c);

#ifdef _USE_TIRPC
    create_ipv6("2001::1", 2048, (struct sockaddr_in6 *) &ipv6a);
    // create_ipv6("2001::1", 2049, (struct sockaddr_in6 *) &ipv6b);
    create_ipv6("2001::f:1", 2048, (struct sockaddr_in6 *) &ipv6c);
#endif    

}

void test_not_found() 
{
    nfs_ip_stats_t * out;
    EQUALS(nfs_ip_stats_get(ipstats, &ipv4a, &out), IP_STATS_NOT_FOUND, "There shouldn't be an ipv4a yet");
    // EQUALS(nfs_ip_stats_get(ipstats, &ipv4b, &out), IP_STATS_NOT_FOUND, "There shouldn't be an ipv4b yet");
    EQUALS(nfs_ip_stats_get(ipstats, &ipv4c, &out), IP_STATS_NOT_FOUND, "There shouldn't be an ipv4c yet");
}

void test_not_found_bc() 
{
    nfs_ip_stats_t * out;
    EQUALS(nfs_ip_stats_get(ipstats, &ipv4a, &out), IP_STATS_SUCCESS, "There should be an ipv4a");
    // EQUALS(nfs_ip_stats_get(ipstats, &ipv4b, &out), IP_STATS_NOT_FOUND, "There shouldn't be an ipv4b yet");
    EQUALS(nfs_ip_stats_get(ipstats, &ipv4c, &out), IP_STATS_NOT_FOUND, "There shouldn't be an ipv4c yet");
}

void test_not_found_c() 
{
    nfs_ip_stats_t * out;
    EQUALS(nfs_ip_stats_get(ipstats, &ipv4a, &out), IP_STATS_SUCCESS, "There should be an ipv4a");
    // EQUALS(nfs_ip_stats_get(ipstats, &ipv4b, &out), IP_STATS_SUCCESS, "There should be an ipv4b");
    EQUALS(nfs_ip_stats_get(ipstats, &ipv4c, &out), IP_STATS_NOT_FOUND, "There shouldn't be an ipv4c yet");
}

void test_not_found_none() 
{
    nfs_ip_stats_t * out;
    EQUALS(nfs_ip_stats_get(ipstats, &ipv4a, &out), IP_STATS_SUCCESS, "There should be an ipv4a");
    // EQUALS(nfs_ip_stats_get(ipstats, &ipv4b, &out), IP_STATS_SUCCESS, "There should be an ipv4b");
    EQUALS(nfs_ip_stats_get(ipstats, &ipv4c, &out), IP_STATS_SUCCESS, "There should be an ipv4c");
}


void test_add() 
{
    int rc = nfs_ip_stats_add(ipstats, &ipv4a, ip_stats_pool);
    EQUALS(rc, IP_STATS_SUCCESS, "Can't add ipv4a, rc = %d", rc);
    test_not_found_bc();

    /* rc = nfs_ip_stats_add(ipstats, &ipv4b, &ip_stats_pool); */
    /* EQUALS(rc, IP_STATS_SUCCESS, "Can't add ipv4b"); */
    /* test_not_found_c(); */

    rc = nfs_ip_stats_add(ipstats, &ipv4c, ip_stats_pool);
    EQUALS(rc, IP_STATS_SUCCESS, "Can't add ipv4c");
    test_not_found_none();
}

int main()
{
    init();
    test_not_found();
    test_add();


    return 0;
}
