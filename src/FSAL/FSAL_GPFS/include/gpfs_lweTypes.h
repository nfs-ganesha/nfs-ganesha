/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* avs_rhrz src/avs/fs/mmfs/ts/util/gpfs_lweTypes.h 1.3                   */
/*                                                                        */
/* Licensed Materials - Property of IBM                                   */
/*                                                                        */
/* Restricted Materials of IBM                                            */
/*                                                                        */
/* COPYRIGHT International Business Machines Corp. 2011                   */
/* All Rights Reserved                                                    */
/*                                                                        */
/* US Government Users Restricted Rights - Use, duplication or            */
/* disclosure restricted by GSA ADP Schedule Contract with IBM Corp.      */
/*                                                                        */
/* IBM_PROLOG_END_TAG                                                     */
#ifndef _h_lwe_types
#define _h_lwe_types

/* LWE Event Types */
#define LWE_EVENT_UNKNOWN    0x0000   /* "Uknown event" */
#define LWE_EVENT_FILEOPEN_READ  0x0001   /* Open for Read Only -  EVENT 'OPEN_READ' */
#define LWE_EVENT_FILEOPEN_WRITE 0x0010   /* Open with Writing privileges - EVEN 'OPEN_WRITE' */
#define LWE_EVENT_FILECLOSE  0x0002   /* "File Close Event" */
#define LWE_EVENT_FILEREAD   0x0004   /* "File Read Event" */
#define LWE_EVENT_FILEWRITE  0x0008   /* "File Write Event" */

/* LWE event resposne type */
typedef enum
{
  LWE_RESP_PENDING  = 0,  /* "Response Unknown" */
  LWE_RESP_CONTINUE = 1,  /* "Response Continue" */
  LWE_RESP_ABORT    = 2,  /* "Response ABORT" */
  LWE_RESP_DONTCARE = 3   /* "Response DONTCARE" */
}lwe_resp_t;

#define lwe_event_type unsigned int
#define lwe_event_token unsigned long long
#define lwe_token_t unsigned long long
#define lwe_sessid_t unsigned int

/* lwe event structure, for external interfance */
typedef struct lwe_event_s {
  int              eventLen;        /* offset 0 */
  lwe_event_type   eventType;       /* offset 4 */
  lwe_event_token  eventToken;      /* offset 8 <--- Must on DWORD */
  int              isSync;          /* offset 16 */
  int              parmLen;         /* offset 20 */
  char*            parmP;           /* offset 24 <-- Must on DWORD */
} lwe_event_t;

#define MAX_LWE_SESSIONS 1024
#define MAX_LWE_EVENTS   1024
#define MAX_LWESESSION_INFO_LEN 100

#define LWE_NO_SESSION 0
#define LWE_EV_NOWAIT 1

#endif /* _h_lwe_types */
