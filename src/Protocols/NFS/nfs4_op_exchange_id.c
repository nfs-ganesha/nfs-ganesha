/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file nfs4_op_exchange_id.c
 * @brief The NFS4_OP_EXCHANGE_ID operation
 */

#include "config.h"
#include <pthread.h>
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "sal_functions.h"
#include "nfs_creds.h"

int get_raddr(SVCXPRT *xprt)
{
	struct sockaddr_storage *ss = xprt->xp_ltaddr.buf;
	int addr = 0;

	if (ss == NULL)
		return addr;

	switch (ss->ss_family) {
	case AF_INET6:
		{
			void *ab;
			ab = &(((struct sockaddr_in6 *)ss)->sin6_addr.
			       s6_addr[12]);
			addr = ntohl(*(uint32_t *) ab);
		}
		break;
	case AF_INET:
		addr = ntohl(((struct sockaddr_in *)ss)->sin_addr.s_addr);

		break;
	default:
		break;
	}

	return addr;
}

/**
 * @brief The NFS4_OP_EXCHANGE_ID operation
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 * @return per RFC5661, p. 364
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_exchange_id(struct nfs_argop4 *op, compound_data_t *data,
			struct nfs_resop4 *resp)
{
	nfs_client_record_t *client_record;
	nfs_client_id_t *conf;
	nfs_client_id_t *unconf;
	sockaddr_t client_addr;
	int rc;
	int len;
	char *temp;
	bool update;
	EXCHANGE_ID4args * const arg_EXCHANGE_ID4 =
	    &op->nfs_argop4_u.opexchange_id;
	EXCHANGE_ID4res * const res_EXCHANGE_ID4 =
	    &resp->nfs_resop4_u.opexchange_id;
	EXCHANGE_ID4resok * const res_EXCHANGE_ID4_ok =
	    (&resp->nfs_resop4_u.opexchange_id.EXCHANGE_ID4res_u.eir_resok4);
	uint32_t pnfs_flags;
	in_addr_t server_addr = 0;

	resp->resop = NFS4_OP_EXCHANGE_ID;

	if (data->minorversion == 0)
		return res_EXCHANGE_ID4->eir_status = NFS4ERR_INVAL;

	if ((arg_EXCHANGE_ID4->
	     eia_flags & ~(EXCHGID4_FLAG_SUPP_MOVED_REFER |
			   EXCHGID4_FLAG_SUPP_MOVED_MIGR |
			   EXCHGID4_FLAG_BIND_PRINC_STATEID |
			   EXCHGID4_FLAG_USE_NON_PNFS |
			   EXCHGID4_FLAG_USE_PNFS_MDS |
			   EXCHGID4_FLAG_USE_PNFS_DS |
			   EXCHGID4_FLAG_UPD_CONFIRMED_REC_A |
			   EXCHGID4_FLAG_CONFIRMED_R)) != 0)
		return res_EXCHANGE_ID4->eir_status = NFS4ERR_INVAL;

	copy_xprt_addr(&client_addr, data->req->rq_xprt);

	/**
	 * @todo Look into this again later, if no exports support
	 * pNFS, then we shouldn't claim to support it.
	 */

	pnfs_flags = arg_EXCHANGE_ID4->eia_flags & EXCHGID4_FLAG_MASK_PNFS;
	if (pnfs_flags == 0) {
		pnfs_flags |=
		    EXCHGID4_FLAG_USE_PNFS_MDS | EXCHGID4_FLAG_USE_PNFS_DS;
	}

	update = (arg_EXCHANGE_ID4->eia_flags &
		  EXCHGID4_FLAG_UPD_CONFIRMED_REC_A) != 0;

	server_addr = get_raddr(data->req->rq_xprt);

	/* Do we already have one or more records for client id (x)? */
	client_record =
	    get_client_record(arg_EXCHANGE_ID4->eia_clientowner.co_ownerid.
			      co_ownerid_val,
			      arg_EXCHANGE_ID4->eia_clientowner.co_ownerid.
			      co_ownerid_len,
			      pnfs_flags,
			      server_addr);

	if (client_record == NULL) {
		/* Some major failure */
		LogCrit(COMPONENT_CLIENTID, "EXCHANGE_ID failed");
		res_EXCHANGE_ID4->eir_status = NFS4ERR_SERVERFAULT;
		return res_EXCHANGE_ID4->eir_status;
	}

	/*
	 * The following checks are based on RFC5661
	 *
	 * This attempts to implement the logic described in
	 * 18.35.4. IMPLEMENTATION
	 */

	pthread_mutex_lock(&client_record->cr_mutex);

	conf = client_record->cr_confirmed_rec;

	if (conf != NULL) {
		/* Need a reference to the confirmed record for below */
		inc_client_id_ref(conf);
	}

	if (conf != NULL && !update) {
		/* EXCHGID4_FLAG_UPD_CONFIRMED_REC_A not set
		 *
		 * Compare the client credentials, but don't compare
		 * the client address.  Doing so interferes with
		 * trunking and the ability of a client to reconnect
		 * after being assigned a new address.
		 */
		if (!nfs_compare_clientcred(&conf->cid_credential,
					    &data->credential)) {
			pthread_mutex_lock(&conf->cid_mutex);
			if (!valid_lease(conf) || !client_id_has_state(conf)) {
				pthread_mutex_unlock(&conf->cid_mutex);

				/* CASE 3, client collisions, old
				 * clientid is expired
				 *
				 * Expire clientid and release our reference.
				 */
				nfs_client_id_expire(conf);
				dec_client_id_ref(conf);
				conf = NULL;
			} else {
				pthread_mutex_unlock(&conf->cid_mutex);
				/* CASE 3, client collisions, old
				 * clientid is not expired
				 */

				res_EXCHANGE_ID4->eir_status =
				    NFS4ERR_CLID_INUSE;

				/* Release our reference to the
				 * confirmed clientid.
				 */
				dec_client_id_ref(conf);
				goto out;
			}
		} else if (memcmp(arg_EXCHANGE_ID4->eia_clientowner.co_verifier,
				  conf->cid_incoming_verifier,
				  NFS4_VERIFIER_SIZE) == 0) {
			/* CASE 2, Non-Update on Existing Client ID
			 *
			 * Return what was last returned without
			 * changing any refcounts
			 */

			unconf = conf;
			res_EXCHANGE_ID4_ok->eir_flags |=
			    EXCHGID4_FLAG_CONFIRMED_R;
			goto return_ok;
		} else {
			/* CASE 5, client restart */
			/** @todo FSF: expire old clientid? */
		}
	} else if (conf != NULL) {
		/* EXCHGID4_FLAG_UPD_CONFIRMED_REC_A set */
		if (memcmp(arg_EXCHANGE_ID4->eia_clientowner.co_verifier,
			   conf->cid_incoming_verifier,
			   NFS4_VERIFIER_SIZE) == 0) {
			if (!nfs_compare_clientcred(&conf->cid_credential,
						    &data->credential)
			    || !cmp_sockaddr(&conf->cid_client_addr,
					     &client_addr,
					     true)) {
				/* CASE 9, Update but wrong principal */
				res_EXCHANGE_ID4->eir_status = NFS4ERR_PERM;
			} else {
				/* CASE 6, Update */
				/** @todo: this is not implemented,
				    the things it updates aren't even
				    tracked */
				LogMajor(COMPONENT_CLIENTID,
					 "EXCHANGE_ID Update not supported");
				res_EXCHANGE_ID4->eir_status = NFS4ERR_NOTSUPP;
			}
		} else {
			/* CASE 8, Update but wrong verifier */
			res_EXCHANGE_ID4->eir_status = NFS4ERR_NOT_SAME;
		}

		/* Release our reference to the confirmed clientid. */
		dec_client_id_ref(conf);

		goto out;
	} else if (conf == NULL && update) {
		res_EXCHANGE_ID4->eir_status = NFS4ERR_NOENT;
		goto out;
	}

	/* At this point, no matter what the case was above, we should
	 * remove any pre-existing unconfirmed record.
	 */

	unconf = client_record->cr_unconfirmed_rec;

	if (unconf != NULL) {
		/* CASE 4, replacement of unconfirmed record
		 *
		 * Delete the unconfirmed clientid record
		 * unhash the clientid record
		 */
		remove_unconfirmed_client_id(unconf);
	}

	/* Now we can proceed to build the new unconfirmed record. We
	 * have determined the clientid and setclientid_confirm values
	 * above.
	 */

	unconf = create_client_id(0,
				  client_record,
				  &client_addr,
				  &data->credential,
				  data->minorversion);

	if (unconf == NULL) {
		/* Error already logged, return */
		res_EXCHANGE_ID4->eir_status = NFS4ERR_RESOURCE;
		goto out;
	}

	unconf->cid_create_session_sequence = 1;

	glist_init(&unconf->cid_cb.v41.cb_session_list);

	memcpy(unconf->cid_incoming_verifier,
	       arg_EXCHANGE_ID4->eia_clientowner.co_verifier,
	       NFS4_VERIFIER_SIZE);

	if (gethostname(unconf->cid_server_owner,
			sizeof(unconf->cid_server_owner)) == -1) {
		/* Free the clientid record and return */
		free_client_id(unconf);
		res_EXCHANGE_ID4->eir_status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	snprintf(unconf->cid_server_scope,
		 sizeof(unconf->cid_server_scope),
		 "%s_NFS-Ganesha",
		 unconf->cid_server_owner);

	LogDebug(COMPONENT_CLIENTID, "Serving IP %s", unconf->cid_server_scope);

	rc = nfs_client_id_insert(unconf);

	if (rc != CLIENT_ID_SUCCESS) {
		/* Record is already freed, return. */
		res_EXCHANGE_ID4->eir_status = clientid_error_to_nfsstat(rc);

		goto out;
	}

 return_ok:

	/* Build the reply */
	res_EXCHANGE_ID4_ok->eir_clientid = unconf->cid_clientid;
	res_EXCHANGE_ID4_ok->eir_sequenceid =
	    unconf->cid_create_session_sequence;

	res_EXCHANGE_ID4_ok->eir_flags |= client_record->cr_pnfs_flags;

	res_EXCHANGE_ID4_ok->eir_state_protect.spr_how = SP4_NONE;

	len = strlen(unconf->cid_server_owner);
	temp = gsh_malloc(len);

	if (temp == NULL) {
		/** @todo FSF: not the best way to handle this but
		    keeps from crashing */
		len = 0;
	} else
		memcpy(temp, unconf->cid_server_owner, len);

	res_EXCHANGE_ID4_ok->eir_server_owner.so_major_id.so_major_id_len = len;
	res_EXCHANGE_ID4_ok->eir_server_owner.so_major_id.so_major_id_val =
	    temp;

	res_EXCHANGE_ID4_ok->eir_server_owner.so_minor_id = 0;

	len = strlen(unconf->cid_server_scope);
	temp = gsh_malloc(len);
	if (temp == NULL) {
		/** @todo FSF: not the best way to handle this but
		    keeps from crashing */
		len = 0;
	} else
		memcpy(temp, unconf->cid_server_scope, len);

	res_EXCHANGE_ID4_ok->eir_server_scope.eir_server_scope_len = len;
	res_EXCHANGE_ID4_ok->eir_server_scope.eir_server_scope_val = temp;

	res_EXCHANGE_ID4_ok->eir_server_impl_id.eir_server_impl_id_len = 0;
	res_EXCHANGE_ID4_ok->eir_server_impl_id.eir_server_impl_id_val = NULL;

	res_EXCHANGE_ID4->eir_status = NFS4_OK;

 out:

	pthread_mutex_unlock(&client_record->cr_mutex);

	/* Release our reference to the client record */
	dec_client_record_ref(client_record);

	return res_EXCHANGE_ID4->eir_status;
}

/**
 * @brief free memory alocated for nfs4_op_exchange_id result
 *
 * @param[in,out] resp Pointer to nfs4_op results
 */
void nfs4_op_exchange_id_Free(nfs_resop4 *res)
{
	EXCHANGE_ID4res *resp = &res->nfs_resop4_u.opexchange_id;

	if (resp->eir_status == NFS4_OK) {
		if (resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_scope.
		    eir_server_scope_val != NULL)
			gsh_free(resp->EXCHANGE_ID4res_u.eir_resok4.
				 eir_server_scope.eir_server_scope_val);
		if (resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_owner.
		    so_major_id.so_major_id_val != NULL)
			gsh_free(resp->EXCHANGE_ID4res_u.eir_resok4.
				 eir_server_owner.so_major_id.so_major_id_val);
		if (resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_impl_id.
		    eir_server_impl_id_val != NULL)
			gsh_free(resp->EXCHANGE_ID4res_u.eir_resok4.
				 eir_server_impl_id.eir_server_impl_id_val);
	}
}
