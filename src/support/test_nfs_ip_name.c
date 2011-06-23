
#include "rpc.h"
#include "config_parsing.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "config_parsing.h"
#include "stuff_alloc.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "../MainNFSD/nfs_init.h"
#include "nfs23.h"

#define MOUNT_PROGRAM 100005
nfs_parameter_t nfs_param;

char out[MAXHOSTNAMELEN];

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
char name4a[MAXHOSTNAMELEN];
char name4c[MAXHOSTNAMELEN];
sockaddr_t ipv6a;
sockaddr_t ipv6b;
sockaddr_t ipv6c;
char name6a[MAXHOSTNAMELEN];
char name6c[MAXHOSTNAMELEN];

void *rpc_tcp_socket_manager_thread(void *Arg)
{
  return NULL;
}

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

void create_svc_req(struct svc_req *req, rpcvers_t ver, rpcprog_t prog, rpcproc_t proc)
{
    memset(req, 0, sizeof(struct svc_req));
    req->rq_prog = prog;
    req->rq_vers = ver;
    req->rq_proc = proc;
}

void nfs_set_ip_name_param_default()
{
    nfs_param.ip_name_param.hash_param.index_size = PRIME_IP_NAME;
    nfs_param.ip_name_param.hash_param.alphabet_length = 10;   /* ipaddr is a numerical decimal value */
    nfs_param.ip_name_param.hash_param.nb_node_prealloc = NB_PREALLOC_HASH_IP_NAME;
    nfs_param.ip_name_param.hash_param.hash_func_key = ip_name_value_hash_func;
    nfs_param.ip_name_param.hash_param.hash_func_rbt = ip_name_rbt_hash_func;
    nfs_param.ip_name_param.hash_param.compare_key = compare_ip_name;
    nfs_param.ip_name_param.hash_param.key_to_str = display_ip_name_key;
    nfs_param.ip_name_param.hash_param.val_to_str = display_ip_name_val;
    nfs_param.ip_name_param.hash_param.name = "IP Name";
    nfs_param.ip_name_param.expiration_time = IP_NAME_EXPIRATION;
    strncpy(nfs_param.ip_name_param.mapfile, "", MAXPATHLEN);

    nfs_param.core_param.dump_stats_per_client = 1;

}

void init() 
{
    BuddyInit(NULL);

    nfs_set_ip_name_param_default();
    nfs_Init_ip_name(nfs_param.ip_name_param);

    create_ipv4("127.0.0.1", 2048, (struct sockaddr_in * ) &ipv4a);
    //    create_ipv4("10.10.5.1", 2049, (struct sockaddr_in * ) &ipv4b);
    create_ipv4("127.0.0.2", 2048, (struct sockaddr_in * ) &ipv4c);

#ifdef _USE_TIRPC
    create_ipv6("::1", 2048, (struct sockaddr_in6 *) &ipv6a);
    // create_ipv6("2001::1", 2049, (struct sockaddr_in6 *) &ipv6b);
    create_ipv6("fe00::0", 2048, (struct sockaddr_in6 *) &ipv6c);
#endif    

}

void test_not_found() 
{
    EQUALS(nfs_ip_name_get(&ipv4a, out), IP_NAME_NOT_FOUND, "There shouldn't be an ipv4a yet");
    // EQUALS(nfs_ip_name_get(ipname, &ipv4b, &out), IP_NAME_NOT_FOUND, "There shouldn't be an ipv4b yet");
    EQUALS(nfs_ip_name_get(&ipv4c, out), IP_NAME_NOT_FOUND, "There shouldn't be an ipv4c yet");
}

void test_not_found_bc() 
{
    EQUALS(nfs_ip_name_get(&ipv4a, out), IP_NAME_SUCCESS, "There should be an ipv4a");
    // EQUALS(nfs_ip_name_get(ipname, &ipv4b, &out), IP_NAME_NOT_FOUND, "There shouldn't be an ipv4b yet");
    EQUALS(nfs_ip_name_get(&ipv4c, out), IP_NAME_NOT_FOUND, "There shouldn't be an ipv4c yet");
}

void test_not_found_c() 
{
    EQUALS(nfs_ip_name_get(&ipv4a, out), IP_NAME_SUCCESS, "There should be an ipv4a");
    // EQUALS(nfs_ip_name_get(ipname, &ipv4b, &out), IP_NAME_SUCCESS, "There should be an ipv4b");
    EQUALS(nfs_ip_name_get(&ipv4c, out), IP_NAME_NOT_FOUND, "There shouldn't be an ipv4c yet");
}

void test_not_found_none() 
{
    EQUALS(nfs_ip_name_get(&ipv4a, out), IP_NAME_SUCCESS, "There should be an ipv4a");
    // EQUALS(nfs_ip_name_get(ipname, &ipv4b, &out), IP_NAME_SUCCESS, "There should be an ipv4b");
    EQUALS(nfs_ip_name_get(&ipv4c, out), IP_NAME_SUCCESS, "There should be an ipv4c");
}


void test_add() 
{
    int rc = nfs_ip_name_add(&ipv4a, name4a);
    EQUALS(rc, IP_NAME_SUCCESS, "Can't add ipv4a, rc = %d", rc);
    test_not_found_bc();

    /* rc = nfs_ip_name_add(ipname, &ipv4b, &ip_name_pool); */
    /* EQUALS(rc, IP_NAME_SUCCESS, "Can't add ipv4b"); */
    /* test_not_found_c(); */

    rc = nfs_ip_name_add(&ipv4c, name4c);
    EQUALS(rc, IP_NAME_SUCCESS, "Can't add ipv4c");
    test_not_found_none();
}


// check that counts look right, including a check on something we didn't set so that it's actually removed correctly
void test_get() 
{
    
}

// remove then re-add ipv4c to test the removal path
void test_remove() 
{
    int rc;
    rc = nfs_ip_name_remove(&ipv4c);
    test_not_found_c();
    EQUALS(rc, IP_NAME_SUCCESS, "Can't remove ipv4c");

    rc = nfs_ip_name_remove(&ipv4c);
    test_not_found_c();
    EQUALS(rc, IP_NAME_NOT_FOUND, "Can't remove ipv4c");

    rc = nfs_ip_name_add(&ipv4c, name4c);
    EQUALS(rc, IP_NAME_SUCCESS, "Can't add ipv4c");
    test_not_found_none();
}

// The IPv6 versions of all of the tests
//
//

void test_not_found_6() 
{
    EQUALS(nfs_ip_name_get(&ipv6a, out), IP_NAME_NOT_FOUND, "There shouldn't be an ipv6a yet");
    // EQUALS(nfs_ip_name_get(ipname, &ipv6b, &out), IP_NAME_NOT_FOUND, "There shouldn't be an ipv6b yet");
    EQUALS(nfs_ip_name_get(&ipv6c, out), IP_NAME_NOT_FOUND, "There shouldn't be an ipv6c yet");
}

void test_not_found_bc_6() 
{
    EQUALS(nfs_ip_name_get(&ipv6a, out), IP_NAME_SUCCESS, "There should be an ipv6a");
    // EQUALS(nfs_ip_name_get(ipname, &ipv6b, &out), IP_NAME_NOT_FOUND, "There shouldn't be an ipv6b yet");
    EQUALS(nfs_ip_name_get(&ipv6c, out), IP_NAME_NOT_FOUND, "There shouldn't be an ipv6c yet");
}

void test_not_found_c_6() 
{
    EQUALS(nfs_ip_name_get(&ipv6a, out), IP_NAME_SUCCESS, "There should be an ipv6a");
    // EQUALS(nfs_ip_name_get(ipname, &ipv6b, &out), IP_NAME_SUCCESS, "There should be an ipv6b");
    EQUALS(nfs_ip_name_get(&ipv6c, out), IP_NAME_NOT_FOUND, "There shouldn't be an ipv6c yet");
}

void test_not_found_none_6() 
{
    EQUALS(nfs_ip_name_get(&ipv6a, out), IP_NAME_SUCCESS, "There should be an ipv6a");
    // EQUALS(nfs_ip_name_get(ipname, &ipv6b, &out), IP_NAME_SUCCESS, "There should be an ipv6b");
    EQUALS(nfs_ip_name_get(&ipv6c, out), IP_NAME_SUCCESS, "There should be an ipv6c");
}


void test_add_6() 
{
    int rc = nfs_ip_name_add(&ipv6a, name6a);
    EQUALS(rc, IP_NAME_SUCCESS, "Can't add ipv6a, rc = %d", rc);
    test_not_found_bc_6();

    /* rc = nfs_ip_name_add(ipname, &ipv6b, &ip_name_pool); */
    /* EQUALS(rc, IP_NAME_SUCCESS, "Can't add ipv6b"); */
    /* test_not_found_c_6(); */

    rc = nfs_ip_name_add(&ipv6c, name6c);
    EQUALS(rc, IP_NAME_SUCCESS, "Can't add ipv6c");
    test_not_found_none_6();
}


// check that counts look right, including a check on something we didn't set so that it's actually removed correctly
void test_get_6() 
{
    
}

// remove then re-add ipv6c to test the removal path
void test_remove_6() 
{
    int rc;
    rc = nfs_ip_name_remove(&ipv6c);
    test_not_found_c_6();
    EQUALS(rc, IP_NAME_SUCCESS, "Can't remove ipv6c");

    rc = nfs_ip_name_remove(&ipv6c);
    test_not_found_c_6();
    EQUALS(rc, IP_NAME_NOT_FOUND, "Can't remove ipv6c");

    rc = nfs_ip_name_add(&ipv6c, name6c);
    EQUALS(rc, IP_NAME_SUCCESS, "Can't add ipv6c");
    test_not_found_none_6();
}

//

int main()
{
    int i;

    init();
    test_not_found();
    test_add();
    test_get();
    for (i = 0; i < 5; i++) {
        test_remove();
    }

#ifdef _USE_TIRPC
    test_not_found_6();
    test_add_6();
    test_get_6();
    for (i = 0; i < 5; i++) {
        test_remove_6();
    }
#endif

    return 0;
}
