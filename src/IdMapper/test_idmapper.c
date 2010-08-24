/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include <sys/resource.h>       /* for having setrlimit */
#include <signal.h>             /* for sigaction */
#ifdef _USE_GSSRPC
#include <gssapi/gssapi.h>
#ifdef HAVE_KRB5
#include <gssapi/gssapi_krb5.h> /* For krb5_gss_register_acceptor_identity */
#endif                          /* HAVE_KRB5 */
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/pmap_clnt.h>
#endif
#include "log_functions.h"
#include "stuff_alloc.h"
#include "fsal.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_file_handle.h"
#include "nfs_exports.h"
#include "nfs_tools.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "config_parsing.h"
#include "SemN.h"

nfs_parameter_t nfs_param;

int idmap_computer_hash_value(char *name, uint32_t * phashval)
{
  char padded_name[PWENT_MAX_LEN];
  uint32_t computed_value = 0;
  unsigned int i = 0;
  unsigned int offset = 0;
  uint64_t extract = 0;
  uint64_t sum = 0;
  uint64_t i1;
  uint64_t i2;
  uint64_t i3;
  uint64_t i4;
  uint64_t i5;
  uint64_t i6;
  uint64_t i7;
  uint64_t i8;
  uint64_t l;

  if(name == NULL || phashval == NULL)
    return CLIENT_ID_INVALID_ARGUMENT;

  memset(padded_name, 0, PWENT_MAX_LEN);

  /* Copy the string to the padded one */
  for(i = 0; i < strnlen(name, PWENT_MAX_LEN); padded_name[i] = name[i], i++) ;

  LogTest("%s \n", padded_name);

  /* For each 9 character pack:
   *   - keep the 7 first bit (the 8th is often 0: ascii string)
   *   - pack 7x9 bit to 63 bits using xor
   *   - xor the last 8th bit to a single 0 , or-ed with the rest
   * Proceeding with the next 9 bytes pack will produce a new value that is xored with the
   * one of the previous iteration */

  for(offset = 0; offset < PWENT_MAX_LEN; offset += 8)
    {
      /* input name is ascii string, remove 8th bit on each byte, not significant */
      i1 = padded_name[offset + 0];
      i2 = (padded_name[offset + 1]) << 8;
      i3 = (padded_name[offset + 2]) << 16;
      i4 = (padded_name[offset + 3]) << 24;
      i5 = (padded_name[offset + 4]) << 32;
      i6 = (padded_name[offset + 5]) << 40;
      i7 = (padded_name[offset + 6]) << 48;
      i8 = (padded_name[offset + 7]) << 56;

      sum = (uint64_t) padded_name[offset + 0] +
          (uint64_t) padded_name[offset + 1] +
          (uint64_t) padded_name[offset + 2] +
          (uint64_t) padded_name[offset + 3] +
          (uint64_t) padded_name[offset + 4] +
          (uint64_t) padded_name[offset + 5] +
          (uint64_t) padded_name[offset + 6] + (uint64_t) padded_name[offset + 7];


      LogTest("|%llx |%llx |%llx |%llx |%llx |%llx |%llx |%llx |%llx | = ",
             i1, i2, i3, i4, i5, i6, i7, i8);

      /* Get xor combibation of all the 8h bit */
      l = (padded_name[offset + 0]) ^
          (padded_name[offset + 1]) ^
          (padded_name[offset + 2]) ^
          (padded_name[offset + 3]) ^
          (padded_name[offset + 4]) ^
          (padded_name[offset + 5]) ^
          (padded_name[offset + 6]) ^ (padded_name[offset + 7]);

      extract = i1 ^ i2 ^ i3 ^ i4 ^ i5 ^ i6 ^ i7 ^ i8 | l;

      LogTest("%llx ", extract);

      computed_value ^= extract;
      computed_value ^= sum;

      LogTest(",%x\n  ", computed_value);
    }

  if(computed_value > 0x00000000FFFFFFFFLL)
    computed_value = (computed_value >> 32) ^ (computed_value & 0x00000000FFFFFFFFLL);

  LogTest("===>%x\n", computed_value);

  *phashval = computed_value;

  return CLIENT_ID_SUCCESS;
}                               /* idmap_computer_hash_value */

main(int argc, char *argv[])
{
  SetDefaultLogging("TEST");
  SetNamePgm("test_buddy");

  char name[30];
  uint32_t valeur;
  int i;

  if(argc == 1)
    exit(0);

  for(i = 1; i < argc; i++)
    {
      strncpy(name, argv[i], 30);

      idmap_computer_hash_value(name, &valeur);
      LogTest("%s %x\n", name, valeur);
    }
}
