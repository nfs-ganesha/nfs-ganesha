/*                                                                              */
/* Copyright (C) 2001 International Business Machines                           */
/* All rights reserved.                                                         */
/*                                                                              */
/* This file is part of the GPFS user library.                                  */
/*                                                                              */
/* Redistribution and use in source and binary forms, with or without           */
/* modification, are permitted provided that the following conditions           */
/* are met:                                                                     */
/*                                                                              */
/*  1. Redistributions of source code must retain the above copyright notice,   */
/*     this list of conditions and the following disclaimer.                    */
/*  2. Redistributions in binary form must reproduce the above copyright        */
/*     notice, this list of conditions and the following disclaimer in the      */
/*     documentation and/or other materials provided with the distribution.     */
/*  3. The name of the author may not be used to endorse or promote products    */
/*     derived from this software without specific prior written                */
/*     permission.                                                              */
/*                                                                              */
/* THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR         */
/* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES    */
/* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.      */
/* IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, */
/* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, */
/* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;  */
/* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,     */
/* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR      */
/* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF       */
/* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                   */
/*                                                                              */
/* %Z%%M%       %I%  %W% %G% %U% */
/*
 *  Library calls for GPFS interfaces
 */
#ifndef _h_lwe_types
#define _h_lwe_types

/* LWE Event Types */
#define LWE_EVENT_UNKNOWN        0x0000  /* "Uknown event" */
#define LWE_EVENT_FILEOPEN_READ  0x0001  /* Open for Read Only -  EVENT 'OPEN_READ' */
#define LWE_EVENT_FILEOPEN_WRITE 0x0010  /* Open with Writing privileges - EVEN 'OPEN_WRITE' */
#define LWE_EVENT_FILECLOSE      0x0002  /* "File Close Event" */
#define LWE_EVENT_FILEREAD       0x0004  /* "File Read Event" */
#define LWE_EVENT_FILEWRITE      0x0008  /* "File Write Event" */
#define LWE_EVENT_FILEDESTROY    0x0020  /* File is being destroyed */
#define LWE_EVENT_FILEEVICT      0x0040  /* OpenFile object is being evicted from memory */
#define LWE_EVENT_BUFFERFLUSH    0x0080  /* Data buffer is being written to disk */
#define LWE_EVENT_POOLTHRESHOLD  0x0100  /* Storage pool exceeded defined utilization */

/* LWE event resposne type */
typedef enum
{
  LWE_RESP_PENDING  = 0,  /* "Response Unknown" */
  LWE_RESP_CONTINUE = 1,  /* "Response Continue" */
  LWE_RESP_ABORT    = 2,  /* "Response ABORT" */
  LWE_RESP_DONTCARE = 3   /* "Response DONTCARE" */
} lwe_resp_t;

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
