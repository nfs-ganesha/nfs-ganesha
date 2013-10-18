/*
  Copyright (c) 2000 The Regents of the University of Michigan.
  All rights reserved.

  Copyright (c) 2000 Dug Song <dugsong@UMICH.EDU>.
  All rights reserved, all wrongs reversed.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.
  3. Neither the name of the University nor the names of its
     contributors may be used to endorse or promote products derived
     from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  Id: svc_auth_gss.c,v 1.28 2002/10/15 21:29:36 kwc Exp
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "ganesha_rpc.h"
#ifdef HAVE_HEIMDAL
#include <gssapi.h>
#define gss_nt_service_name GSS_C_NT_HOSTBASED_SERVICE
#else
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_generic.h>
#endif

#include "nfs_core.h"
#include "log.h"

/**
 * @brief Convert GSSAPI status to a string
 *
 * @param[out] outmsg    Output string
 * @param[in]  tag       Tag
 * @param[in]  maj_stat  GSSAPI major status
 * @param[in]  min_stat  GSSAPI minor status
 */

void log_sperror_gss(char *outmsg, OM_uint32 maj_stat, OM_uint32 min_stat)
{
	OM_uint32 smin;
	gss_buffer_desc msg;
	gss_buffer_desc msg2;
	int msg_ctx = 0;

	if (gss_display_status
	    (&smin, maj_stat, GSS_C_GSS_CODE, GSS_C_NULL_OID, &msg_ctx,
	     &msg) != GSS_S_COMPLETE) {
		sprintf(outmsg, "untranslatable error");
		return;
	}

	if (gss_display_status
	    (&smin, min_stat, GSS_C_MECH_CODE, GSS_C_NULL_OID, &msg_ctx,
	     &msg2) != GSS_S_COMPLETE) {
		gss_release_buffer(&smin, &msg);
		sprintf(outmsg, "%s : untranslatable error", (char *)msg.value);
		return;
	}

	sprintf(outmsg, "%s : %s ", (char *)msg.value, (char *)msg2.value);

	gss_release_buffer(&smin, &msg);
	gss_release_buffer(&smin, &msg2);
}

const char *str_gc_proc(rpc_gss_proc_t gc_proc)
{
	switch (gc_proc) {
	case RPCSEC_GSS_DATA:
		return "RPCSEC_GSS_DATA";
	case RPCSEC_GSS_INIT:
		return "RPCSEC_GSS_INIT";
	case RPCSEC_GSS_CONTINUE_INIT:
		return "RPCSEC_GSS_CONTINUE_INIT";
	case RPCSEC_GSS_DESTROY:
		return "RPCSEC_GSS_DESTROY";
	}

	return "unknown";
}
