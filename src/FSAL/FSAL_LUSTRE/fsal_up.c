/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/**
 *
 * \file    fsal_up.c
 * \brief   Contains materials to use changelog based Upcalls in
 *          the FSAL_LUSTRE.
 *
 */
#define FSAL_INTERNAL_C
#include "config.h"

#include <libgen.h> /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <mntent.h>
#include <unistd.h> /* glibc uses <sys/fsuid.h> */
#include <netdb.h>
#include <attr/xattr.h>

#ifdef USE_FSAL_LUSTRE_UP
#define HAVE_CHANGELOG_EXT_JOBID 1

#include "abstract_mem.h"
#include "fsal.h"
#include "fsal_up.h"
#include "fsal_handle.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "lustre_methods.h"
#include <lcap_client.h>

#ifndef LPX64
#define LPX64   "%#llx"
static inline bool fid_is_zero(const lustre_fid *fid)
{
	return fid->f_seq == 0 && fid->f_oid == 0;
}
#endif

#define DFID_NOBRACE    LPX64":0x%x:0x%x"

#define FLUSH_REQ_COUNT 10000
#define LEN_MESSAGE 1024
#define JOBID_LEN LUSTRE_JOBID_LENGTH


static int lustre_invalidate_entry(struct lustre_filesystem *lustre_fs,
				   const struct fsal_up_vector *event_func,
				   lustre_fid *fid)
{
	struct gsh_buffdesc key;
	struct lustre_file_handle handle;
	uint32_t upflags;
	int rc;

	handle.fid = *fid;
	handle.fsdev = makedev(lustre_fs->fs->fsid.major,
			       lustre_fs->fs->fsid.minor);
	key.addr = &handle;
	key.len = sizeof(handle);
	upflags = CACHE_INODE_INVALIDATE_ATTRS |
		  CACHE_INODE_INVALIDATE_CONTENT;
	rc = event_func->invalidate_close(lustre_fs->fs->fsal,
					  event_func,
					  &key,
					  upflags);

	return rc;
}

static int lustre_changelog_upcall(struct lustre_filesystem *lustre_fs,
				   const struct fsal_up_vector *event_func,
				   struct changelog_rec *rec)
{
	struct changelog_ext_jobid *jid;
	struct changelog_ext_rename *rnm;
	/* changelog are displayed with the format of "lfs changelog" */
	char format_chgl[] =
	  "%llu %02d%-5s %02d:%02d:%02d.%06d %04d.%02d.%02d 0x%x %s t="DFID;
	char message[LEN_MESSAGE];
	char message2[LEN_MESSAGE];

	struct tm ts;
	time_t secs;
	int rc;

	secs = rec->cr_time >> 30;
	gmtime_r(&secs, &ts);

	if (rec->cr_flags & CLF_JOBID)
		jid = changelog_rec_jobid(rec);
	else
		return -1;

	if (rec->cr_flags & CLF_RENAME)
		rnm = changelog_rec_rename(rec);
		snprintf(message, LEN_MESSAGE, format_chgl,
			rec->cr_index,
			rec->cr_type,
			changelog_type2str(rec->cr_type),
			ts.tm_hour,
			ts.tm_min,
			ts.tm_sec,
			(int)(rec->cr_time & ((1 << 30) - 1)),
			ts.tm_year + 1900,
			ts.tm_mon + 1,
			ts.tm_mday,
			rec->cr_flags & CLF_FLAGMASK,
			jid->cr_jobid,
			PFID(&rec->cr_tfid));

	if (rec->cr_namelen)
		snprintf(message2, LEN_MESSAGE,
			 " p="DFID" %.*s",
			 PFID(&rec->cr_pfid),
			 rec->cr_namelen,
			 changelog_rec_name(rec));
	else
		message2[0] = '\0';

	strncat(message, message2, LEN_MESSAGE);
	LogFullDebug(COMPONENT_FSAL_UP, "%s", message);

	switch (rec->cr_type) {
	case CL_CREATE:
	case CL_MKDIR:
	case CL_HARDLINK:
	case CL_SOFTLINK:
	case CL_MKNOD:
	case CL_UNLINK:
	case CL_RMDIR:
		/* invalidate parent entry */
		rc = lustre_invalidate_entry(lustre_fs,
					     event_func,
					     &rec->cr_pfid);
		if (rc)
			LogDebug(COMPONENT_FSAL,
				 "Could not invalidate fid="DFID,
				 PFID(&rec->cr_pfid));
		break;
	case CL_RENAME:
		/* invalidate parent entry
		 * and target entry */
		rc = lustre_invalidate_entry(lustre_fs,
					     event_func,
					     &rnm->cr_spfid);
		if (rc)
			LogDebug(COMPONENT_FSAL,
				 "Could not invalidate fid="DFID,
				 PFID(&rnm->cr_spfid));

		rc = lustre_invalidate_entry(lustre_fs,
					     event_func,
					     &rec->cr_pfid);
		if (rc)
			LogDebug(COMPONENT_FSAL,
				 "Could not invalidate fid="DFID,
				 PFID(&rec->cr_pfid));

		rc = lustre_invalidate_entry(lustre_fs,
					     event_func,
					     &rec->cr_tfid);
		if (rc)
			LogDebug(COMPONENT_FSAL,
				 "Could not invalidate fid="DFID,
				 PFID(&rec->cr_tfid));

		break;
	case CL_ATIME:
	case CL_MTIME:
	case CL_CTIME:
	case CL_SETATTR:
		/* invalidate target entry */
		rc = lustre_invalidate_entry(lustre_fs,
					     event_func,
					     &rec->cr_tfid);
		if (rc)
			LogDebug(COMPONENT_FSAL,
				 "Could not invalidate fid="DFID,
				 PFID(&rec->cr_tfid));
		break;
	default:
		/* untracked record type */
		break;
	}
	return 0;
}

void *LUSTREFSAL_UP_Thread(void *Arg)
{
	const struct fsal_up_vector *event_func;
	struct lustre_filesystem *lustre_fs = Arg;
	struct lcap_cl_ctx *ctx = NULL;
	struct changelog_rec *rec;
	struct changelog_ext_jobid *jid;
	struct changelog_ext_rename *rnm;
	int flags = LCAP_CL_DIRECT|LCAP_CL_JOBID;
	int rc;
	long long last_idx = 0LL;
	long long managed_idx = 0LL;
	unsigned int req_count = 0;
	/* For wanting of a llapi call to get this information */
	/* @todo: use information form fsal_filesystem here */
	const char mdtname[] = "lustre-MDT0000";
	const char chlg_reader[] = "cl1";
	char my_jobid[JOBID_LEN];

	/* Compute my jobid */
	snprintf(my_jobid, JOBID_LEN, "%s.%d", exec_name, getuid());

	/* get the FSAL_UP vector */
	event_func = lustre_fs->up_ops;

	if (event_func == NULL) {
		LogFatal(COMPONENT_FSAL_UP,
			 "FSAL up vector does not exist. Can not continue.");
		gsh_free(Arg);
		return NULL;
	}
	LogFullDebug(COMPONENT_FSAL_UP,
		     "Initializing callback thread for %s MDT=%s my_jobid=%s",
		     lustre_fs->fsname, mdtname, my_jobid);


	/* Wait for 2 seconds, until the rest of the server starts */
	sleep(2);

	/* Main loop */
	last_idx = 0LL;
	managed_idx = 0LL;
	while (true) {
		/* open changelog reading channel in lcap */
		rc = lcap_changelog_start(&ctx, flags, mdtname, last_idx);
		if (rc) {
			LogFatal(COMPONENT_FSAL_UP,
				 "could not read changelog, lcap_changelog_start:(%d,%s)",
				 rc, strerror(-rc));
			return NULL;
		}

		while ((rc = lcap_changelog_recv(ctx, &rec)) == 0) {

			if (rec->cr_flags & CLF_JOBID)
				jid = changelog_rec_jobid(rec);
			else
				break;
			if (rec->cr_index > managed_idx) {
				managed_idx = rec->cr_index;
				last_idx = rec->cr_index;
				req_count += 1;

				/* If jobid is an empty string, skip it */
				if (jid->cr_jid[0] == '\0') {
					rc = lcap_changelog_free(ctx, &rec);
					if (rc)
						LogFatal(COMPONENT_FSAL_UP,
							 "lcap_changelog_free: %d,%s\n",
							 rc, strerror(-rc));
					continue;
				}
				/* Do not care for records generated
				 * by Ganesha's activity */
				if (!strcmp(jid->cr_jobid, my_jobid)) {
					rc = lcap_changelog_free(ctx, &rec);
					if (rc)
						LogFatal(COMPONENT_FSAL_UP,
							 "lcap_changelog_free: %d,%s\n",
							 rc, strerror(-rc));
					continue;
				}

				rc = lustre_changelog_upcall(lustre_fs,
							     event_func,
							     rec);
				if (rc)
					LogMajor(COMPONENT_FSAL,
						   "error occurred when dealing with a changelog record");

				rc = lcap_changelog_free(ctx, &rec);
				if (rc)
					LogFatal(COMPONENT_FSAL_UP,
						 "lcap_changelog_free: %d,%s\n",
						 rc, strerror(-rc));
				}
			}

		if (req_count > FLUSH_REQ_COUNT) {
				rc = lcap_changelog_clear(ctx,
							  mdtname,
							  chlg_reader,
							  last_idx);
				if (rc)
					LogDebug(COMPONENT_FSAL_UP,
					 "lcap_changelog_clear() exited with status %d, %s",
					 rc, strerror(-rc));
				else
					LogDebug(COMPONENT_FSAL_UP,
						 "changelog records cleared");
				req_count = 0;
		}

		/* clear si req_count > 0 et eof sans avoir vu de records */

		if (rc < 0)
			LogDebug(COMPONENT_FSAL_UP,
				 "lcap_changelog_recv() loop exited with status %d, %s",
				 rc, strerror(-rc));

		/* Close changelog file */
		rc = lcap_changelog_fini(ctx);
		if (rc)
			LogFatal(COMPONENT_FSAL_UP,
				 "lcap_changelog_fini: %d,%s\n",
				 rc, strerror(-rc));
		last_idx = 0LL;

		/* Sleep for one second to avoid too aggressive polling
		 * on LUSTRE changelogs */
		sleep(1);
	}
	return NULL;
}
#endif
