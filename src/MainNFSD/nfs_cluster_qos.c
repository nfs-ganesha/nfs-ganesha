// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2025, IBM . All rights reserved.
 * Author: Naresh Chillarege<Naresh.Chillarege@ibm.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.  see <http://www.gnu.org/licenses/
 *
 * ---------------------------------------
 */

#include "nfs_core.h"
#include "cqos.h"
#include "nfs_cluster_qos.h"
#include "nfs_qos.h"

#define CQOS_MAX_RETRIES 1

/* If there is no I/O for UNSUB time, then unsubscribe */
#define CQOS_UNSUB_TIME (1 * USEC_IN_SEC)
#define CQOS_USEC_IN_MSEC 1000

/*
 * In the message, cqos_ops field 12 bits are used for denoting op_type
 *
 * From Lower bits, 0,1,2 bits are used for export read/write/combined bw.
 *                  3,4,5 bits are used for export read/write/combined iops.
 *                  6,7,8 bits are used for client read/write/combined bw
 *                  9,10,11 bits are used for client read/write/combined iops.
 */

#define RW_EXP_BITS 3
#define RW_CLI_BITS (RW_EXP_BITS << 6)
#define RW_IOP_EXP_BITS (RW_EXP_BITS << 3)
#define RW_IOP_CLI_BITS (RW_EXP_BITS << 9)

unsigned int cqos_initialized;
static const struct timespec msg_tout = {0, 180000000};

/*
 * This is global glist which holds all ceph nodes addresses
 * in the cluster
 *
 * Each node in this glist has fd, rpc client handle to send msgs
 * It also has flag to denote if this IP addr is of local system
 *
 */
struct glist_head cqos_hosts = GLIST_HEAD_INIT(cqos_hosts);


/**********************************************************************
 ***************** APIs FOR CLUSTER QOS RECV MSGS *********************
 *********************************************************************/

/**
 * This function compares the node to be inserted/removed in avltree,
 * and the node which is already existing in tree.
 *
 * @param [in] First of the two nodes to be compared.
 * @param [in] Second node for comparison.
 * @return -1, 0, 1, based on first node value comapared to second one.
 * returns -2 for invalid inputs.
 */
int cqos_addr_cmpf(const struct avltree_node *lhs,
		   const struct avltree_node *rhs)
{
	struct cqos_nodes_info *lk, *rk;

	lk = avltree_container_of(lhs, struct cqos_nodes_info, cqos_avl_node);
	rk = avltree_container_of(rhs, struct cqos_nodes_info, cqos_avl_node);

	return sockaddr_cmp(&lk->node_addr, &rk->node_addr, true);
}

/**
 * This function searches for given key in avl tree
 *
 * @param [in] root of the avl tree
 * @param [in] key to be searched in tree.
 *
 * @return corresponding node of the key in tree, NULL if it doesn't exist.
 */
static cqos_nodes_info_t *cqos_avl_node_lookup(struct avltree *cqos_subnodes,
					       const struct avltree_node *key)
{
	struct avltree_node *node =
		avltree_inline_lookup(key, cqos_subnodes, cqos_addr_cmpf);

	if (node != NULL)
		return avltree_container_of(node, cqos_nodes_info_t,
					    cqos_avl_node);
	else
		return NULL;
}

/**
 * This function inserts a new node with the given key.
 *
 * @param [in] root of the avl tree
 * @param [in] key to be inserted in tree.
 *
 * if the node already existing with given key, it doesn't do anything.
 */
static void cqos_avl_node_insert(struct avltree *cqos_subnodes,
				 struct avltree_node *key)
{
	struct avltree_node *node;

	node = avltree_inline_insert(key, cqos_subnodes, cqos_addr_cmpf);

	assert(node == NULL);
}

/**
 * This function removess the node with the given key.
 *
 * @param [in] root of the avl tree
 * @param [in] key to be removed from tree.
 */
static void cqos_avl_node_remove(struct avltree *cqos_subnodes,
				 struct avltree_node *key)
{
	avltree_remove(key, cqos_subnodes);
}

/**********************************************************************
 ***************** APIs FOR CLUSTER QOS RECV MSGS *********************
 *********************************************************************/

/**
 * This function finally updates the iops consumption value received from
 * other nodes of the cluster.
 *
 * @param [in] QoS bucket where value needs to be updated.
 * @param [in] iops value received from other node.
 */
static void update_iops_from_remote_node(qos_bucket_t *bucket,
					 uint64_t iops_val)
{
	uint64_t last_time = bucket->iops_ldct;
	uint64_t ctime = get_time_in_usec();
	uint64_t timeout = 0;
	uint64_t rtime = (iops_val * (USEC_IN_SEC / bucket->max_iops_allowed));
	uint64_t cal_time = last_time + IOPS_DELAY_USEC + rtime;

	if (ctime < cal_time) {
		bucket->iops_ldct = bucket->iops_ldct + rtime;
	} else {
		bucket->iops_ldct = ctime + rtime;
	}
	timeout = bucket->iops_ldct;

	if (timeout <= ctime)
		bucket->iops_consumed += iops_val;
}

/**
 * This function finally updates the bandwidth consumption received from
 * other nodes of the cluster.
 *
 * @param [in] QoS bucket where value needs to be updated.
 * @param [in] bandwidth value received from other node.
 */
static void update_bw_from_remote_node(qos_bucket_t *bucket, uint64_t bw_val)
{
	uint64_t last_time = bucket->bw_ldct;
	uint64_t ctime = get_time_in_usec();
	/* Microseconds required to meet bandwidth */
	uint64_t rtime = ((bw_val * USEC_IN_SEC) / bucket->max_bw_allowed);
	uint64_t cal_time = last_time + rtime;
	uint64_t grace_time = last_time + rtime + BW_EXPORT_FU_IO;

	if (ctime < (cal_time)) {
		bucket->bw_ldct = last_time + rtime;
	} else {
		if (ctime < grace_time) {
			bucket->bw_ldct += rtime;
		} else {
			bucket->bw_ldct = ctime + rtime;
		}
	}
}

/**
 * This function reads bw/iops values from other node message,
 * based on optype, classtype
 *
 * @param [in] bucket Pointer to the qos_bucket_t representing the bucket
 * @param [in] op_type to denote read/write
 * @param [in] class_type to mention type of class, export/client
 * @param [in] cqos_msg message with bw/iops values from other node.
 */
static void cqos_update_bw_iops_values(qos_bucket_t *bucket,
				       qos_op_type_t op_type,
				       qos_class_type_t class_type,
				       cluster_qos_msg *cqos_msg)
{
	uint64_t bytes = 0;
	cluster_qos_msg *msg = cqos_msg;

	if (bucket == NULL)
		return;

	/* Update bandwidth values if subscribed */
	if (bucket->bw_subscribed && class_type == QOS_EXPORT) {
		bytes = ((op_type == QOS_READ) ? msg->export_rbw
					       : msg->export_wbw);
	} else if (bucket->bw_subscribed && class_type == QOS_CLIENT) {
		bytes = ((op_type == QOS_READ) ? msg->client_rbw
					       : msg->client_wbw);
	}

	if (bytes != 0)
		update_bw_from_remote_node(bucket, bytes);

	bytes = 0;
	/* Update iops values if subscribed */
	if (bucket->iops_subscribed && class_type == QOS_EXPORT) {
		bytes = ((op_type == QOS_READ) ? msg->export_riops
					       : msg->export_wiops);
	} else if (bucket->iops_subscribed && class_type == QOS_CLIENT) {
		bytes = ((op_type == QOS_READ) ? msg->client_riops
					       : msg->client_wiops);
	}

	if (bytes != 0)
		update_iops_from_remote_node(bucket, bytes);
}

/**
 * This function adds subscribed node to the avl tree, if the node is
 * already exists in the tree, then it sets the bit for subscripred op
 * for the node. Subscribed op could be export read/write or client
 * read/write, for both bw/iops.
 *
 * @param [in] class Pointer to the qos_class_t of the export/client qos class
 * @param [in] cqos_msg message with bw/iops values from other node.
 */
static void add_subscribed_optype_to_node(qos_class_t *class_ptr,
					  cluster_qos_msg *cluster_qos_msg)
{
	cqos_nodes_info_t key;
	cqos_nodes_info_t *node_info = NULL;

	memset(&key, 0, sizeof(cqos_nodes_info_t));
	memcpy(&key.node_addr, &cluster_qos_msg->node_addr, sizeof(sockaddr_t));

	PTHREAD_MUTEX_lock(&class_ptr->lock);
	node_info = cqos_avl_node_lookup(&class_ptr->cqos_subnodes,
					 &key.cqos_avl_node);
	if (node_info == NULL) {
		/* If the node doesn't exist, add node to avl tree */
		node_info = gsh_calloc(1, sizeof(*node_info), MEM_COMP_QOS);
		memcpy(&node_info->node_addr, &cluster_qos_msg->node_addr,
		       sizeof(sockaddr_t));
		node_info->subscribed_ops = cluster_qos_msg->cqos_ops;
		cqos_avl_node_insert(&class_ptr->cqos_subnodes,
				     &node_info->cqos_avl_node);
	} else {
		/* If the node already exists, set the subscribed op to node */
		node_info->subscribed_ops |= cluster_qos_msg->cqos_ops;
	}
	PTHREAD_MUTEX_unlock(&class_ptr->lock);
}

/**
 * This function gets the buckets for read/write and calls the function
 * which sets bw/iops values published by other nodes.
 *
 * @param [in] class Pointer to the qos_class_t of the export/client qos class
 * @param [in] cqos_msg message with bw/iops values from other node.
 * @param [in] class type export/client.
 */
static void add_published_bw_iops_values(qos_class_t *class_ptr,
					 cluster_qos_msg *cluster_qos_msg,
					 qos_class_type_t class_type)
{
	qos_bucket_t *bucket;

	if (!class_ptr)
		return;

	add_subscribed_optype_to_node(class_ptr, cluster_qos_msg);


	if (class_ptr->bw_enabled)
		bucket = qos_get_bw_bucket(class_ptr, QOS_READ);
	else
		bucket = qos_get_iops_bucket(class_ptr, QOS_READ);

	PTHREAD_MUTEX_lock(&bucket->lock);
	cqos_update_bw_iops_values(bucket, QOS_READ, class_type,
				   cluster_qos_msg);
	PTHREAD_MUTEX_unlock(&bucket->lock);

	if (class_ptr->bw_enabled)
		bucket = qos_get_bw_bucket(class_ptr, QOS_WRITE);
	else
		bucket = qos_get_iops_bucket(class_ptr, QOS_WRITE);

	PTHREAD_MUTEX_lock(&bucket->lock);
	cqos_update_bw_iops_values(bucket, QOS_WRITE, class_type,
				   cluster_qos_msg);
	PTHREAD_MUTEX_unlock(&bucket->lock);
}

/**
 * This function gets called when published message received from
 * other nodes. It checks if qos_class exists for this export
 *
 * @param [in] gsh_export pointer for which published message received.
 * @param [in] cqos_msg message with bw/iops values from other node.
 */
static void cqos_recv_publish_pe(struct gsh_export *export,
				 cluster_qos_msg *cluster_qos_msg)
{
	if (!export->qos_class) {
		return;
	}
	add_published_bw_iops_values(export->qos_class, cluster_qos_msg,
				     QOS_EXPORT);
}

/**
 * This function gets called when published message received from
 * other nodes. It checks if qos_class exists for this client
 *
 * @param [in] gsh_client pointer for which published message received.
 * @param [in] cqos_msg message with bw/iops values from other node.
 */
static void cqos_recv_publish_pc(struct gsh_client *client,
				 cluster_qos_msg *cluster_qos_msg)
{
	if (!client->qos_class) {
		return;
	}
	add_published_bw_iops_values(client->qos_class, cluster_qos_msg,
				     QOS_CLIENT);
}

/**
 * This function gets called when subscribed  message received from
 * other nodes. It checks if qos_class exists for this export
 *
 * @param [in] gsh_export pointer for which subscribe message received.
 * @param [in] cqos_msg message with subscribe op info from other node.
 */
static void cqos_recv_subscribe_pe(struct gsh_export *export,
				   cluster_qos_msg *cluster_qos_msg)
{
	if (!export->qos_class) {
		return;
	}
	add_subscribed_optype_to_node(export->qos_class, cluster_qos_msg);
}

/**
 * This function gets called when subscribed  message received from
 * other nodes. It checks if qos_class exists for this client
 *
 * @param [in] gsh_client pointer for which subscribe message received.
 * @param [in] cqos_msg message with subscribe op info from other node.
 */
static void cqos_recv_subscribe_pc(struct gsh_client *client,
				   cluster_qos_msg *cluster_qos_msg)
{
	if (!client->qos_class) {
		return;
	}
	add_subscribed_optype_to_node(client->qos_class, cluster_qos_msg);
}

/**
 * This function gets called when unsubscribe  message received from
 * other nodes.
 *
 * @param [in] class pointer for which subscribe message received.
 * @param [in] cqos_msg message with unsubscribe op info from other node.
 */
static void unsubscribe_optype_from_node(qos_class_t *class_ptr,
					 cluster_qos_msg *cluster_qos_msg)
{
	cqos_nodes_info_t key;
	cqos_nodes_info_t *node_info = NULL;

	memset(&key, 0, sizeof(cqos_nodes_info_t));
	memcpy(&key.node_addr, &cluster_qos_msg->node_addr, sizeof(sockaddr_t));

	PTHREAD_MUTEX_lock(&class_ptr->lock);
	node_info = cqos_avl_node_lookup(&class_ptr->cqos_subnodes,
					 &key.cqos_avl_node);
	if (node_info == NULL) {
		PTHREAD_MUTEX_unlock(&class_ptr->lock);
		return;
	} else {
		node_info->subscribed_ops &= ~cluster_qos_msg->cqos_ops;
	}

	/* if node is not subscribed to any channel, delete it */

	if (node_info->subscribed_ops == 0) {
		cqos_avl_node_remove(&class_ptr->cqos_subnodes,
				     &node_info->cqos_avl_node);
	}
	PTHREAD_MUTEX_unlock(&class_ptr->lock);
}


/**
 * This function gets called when subscribed  message received from
 * other nodes. It checks if qos_class exists for this client
 *
 * @param [in] gsh_client pointer for which unsubscribe message received.
 * @param [in] cqos_msg message with unsubscribed op info from other node.
 */
static void cqos_recv_unsubscribe_pc(struct gsh_client *client,
				     cluster_qos_msg *cluster_qos_msg)
{
	if (!client->qos_class) {
		return;
	}
	unsubscribe_optype_from_node(client->qos_class, cluster_qos_msg);
}

/**
 * This function gets called when unsubscribed  message received from
 * other nodes. It checks if qos_class exists for this export
 *
 * @param [in] gsh_export pointer for which unsubscribe message received.
 * @param [in] cqos_msg message with unsubscribed op info from other node.
 */
static void cqos_recv_unsubscribe_pe(struct gsh_export *export,
				     cluster_qos_msg *cluster_qos_msg)
{
	if (!export->qos_class) {
		return;
	}
	unsubscribe_optype_from_node(export->qos_class, cluster_qos_msg);
}

/**
 * This function process cluster qos message when perexport or
 * pepc mode is enabled. It checks what is command type i.e
 * publish/subscribe/unsubscribe
 *
 * @param [in] cqos_msg message from other nodes in cluster
 */
static void cluster_qos_process_pe(cluster_qos_msg *cluster_qos_msg)
{
	struct gsh_export *export;

	export = get_gsh_export(cluster_qos_msg->export_id);
	if (export == NULL) {
		LogDebug(COMPONENT_QOS, "export details not found for id:%d",
			cluster_qos_msg->export_id);
		return;
	}

	if (cluster_qos_msg->cqos_cmd == CQOS_PUBLISH) {
		cqos_recv_publish_pe(export, cluster_qos_msg);
	} else if (cluster_qos_msg->cqos_cmd == CQOS_SUBSCRIBE) {
		cqos_recv_subscribe_pe(export, cluster_qos_msg);
	} else if (cluster_qos_msg->cqos_cmd == CQOS_UNSUBSCRIBE) {
		cqos_recv_unsubscribe_pe(export, cluster_qos_msg);
	}
}

/**
 * This function process cluster qos message when perclient
 * mode is enabled. It checks what is command type i.e
 * publish/subscribe/unsubscribe
 *
 * @param [in] cqos_msg message from other nodes in cluster
 */
static void cluster_qos_process_pc(cluster_qos_msg *cluster_qos_msg)
{
	struct gsh_client *client;

	client = get_gsh_client(&cluster_qos_msg->client_addr, true);

	if (client == NULL) {
		LogDebug(COMPONENT_QOS, "Given client details not found");
		return;
	}

	if (cluster_qos_msg->cqos_cmd == CQOS_PUBLISH) {
		cqos_recv_publish_pc(client, cluster_qos_msg);
	} else if (cluster_qos_msg->cqos_cmd == CQOS_SUBSCRIBE) {
		cqos_recv_subscribe_pc(client, cluster_qos_msg);
	} else if (cluster_qos_msg->cqos_cmd == CQOS_UNSUBSCRIBE) {
		cqos_recv_unsubscribe_pc(client, cluster_qos_msg);
	}
}

/**
 * This function is entry point for processing the received cqos
 * message. It check what is the qos mode enabled, and processes
 * accordingly
 *
 * @param [in] cqos_msg message from other nodes in cluster
 */
void cluster_qos_process(cluster_qos_msg *cluster_qos_msg)
{
	if (g_qos_config->qos_type == QOS_NOT_ENABLED ||
	    g_qos_config->enable_qos == 0) {
		return;
	} else if (g_qos_config->qos_type == QOS_PER_EXPORT_ENABLED) {
		cluster_qos_process_pe(cluster_qos_msg);
	} else if (g_qos_config->qos_type == QOS_PER_CLIENT_ENABLED) {
		cluster_qos_process_pc(cluster_qos_msg);
	} else if (g_qos_config->qos_type == QOS_PEREXPORT_PERCLIENT_ENABLED) {
		/*
		 * In case of PePc type, to avoid cluster qos traffic
		 * we support only Per_Export mode in cqos even if standalone
		 * QoS is configured with PePc.
		 */
		LogDebug(COMPONENT_QOS,
			"Cqos doesn't support PePc mode, enabled PerExport");
		cluster_qos_process_pe(cluster_qos_msg);
	} else {
		LogDebug(COMPONENT_QOS, " INVALID QOS_TYPE:%d",
			 g_qos_config->qos_type);
	}
}


/**********************************************************************
 ***************** APIs FOR CLUSTER QOS SEND MSGS *********************
 *********************************************************************/

/**
 * This function checks if the socket to destination system
 * is ready to send the data. It will ensure tcp connection
 * still exists and ready to send message
 */
static bool cqos_ensure_evchan(CLIENT *clnt)
{
	SVCXPRT *xprt;
	int code;

	if (clnt == NULL)
		return false;

	xprt = clnt_vc_get_client_xprt(clnt);
	if (xprt == NULL)
		return false;

	code = svc_rqst_evchan_reg(0, xprt, SVC_RQST_FLAG_NONE);
	if (code != 0) {
		LogCrit(COMPONENT_QOS, "CQOS: evchan registration failed (%d)",
			code);
		return false;
	}

	return true;
}


/**
 * This function creates TCP socket and connects to other node whose address
 * is given.
 *
 * @param [in] address of the node to be connected.
 * @return file descripor connected to client.
 *         -1 if socket creation fails or connect fails.
 */
static int cqos_create_socket(sockaddr_t *sockaddr)
{
	int r = -1;
	int fd = -1;
	struct sockaddr_in *in1 = (struct sockaddr_in *)sockaddr;
	struct sockaddr_in6 *in2 = (struct sockaddr_in6 *)sockaddr;

	switch (sockaddr->ss_family) {
	case AF_INET: {
		fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		in1->sin_port = htons(nfs_param.core_param.port[P_CQOS]);
		r = connect(fd, (struct sockaddr *)in1,
			    sizeof(struct sockaddr));
		if (!r)
			goto done;
		close(fd);
		break;
	}
	case AF_INET6: {
		fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
		in2->sin6_port = htons(nfs_param.core_param.port[P_CQOS]);
		r = connect(fd, (struct sockaddr *)in2,
			    sizeof(struct sockaddr));
		if (!r)
			goto done;
		close(fd);
		break;
	}
	default:
		break;
	}
	return -1;
done:
	return fd;
}

/**
 * This function creates rpc client handle for given fd.
 *
 * @param [in]  fd which is already connected to remote node.
 * @return      rpc client handle.
 *              NULL if rpc client handle can't be created.
 */
static CLIENT *cqos_create_rpc_client(int fd)
{
	char *err;
	CLIENT *clnt = NULL;

	if (fd < 0) {
		LogCrit(COMPONENT_QOS, "CQOS: Invalid Fd to create client");
		return NULL;
	}

	struct sockaddr_storage ss;

	struct netbuf raddr = { .buf = &ss, .len = sizeof(ss) };

	clnt = clnt_vc_ncreatef(fd, &raddr, CQOSPROG, CQOS_VERS, 0, 0,
				CLNT_CREATE_FLAG_CLOSE);
	if (CLNT_FAILURE(clnt)) {
		err = rpc_sperror(&clnt->cl_error, "failed");
		LogCrit(COMPONENT_QOS, "%s", err);
		free_sperror(err);
		return NULL;
	}

	if (clnt_control(clnt, CLSET_FD_NCLOSE, NULL)) {
		LogCrit(COMPONENT_QOS,
				"CQOS: Unable to set fd don't close option");
	}

	return clnt;
}


/**
 * This function is a callback function, will be called once
 * the RPC message has been sent. It will clear the message
 * request created.
 */
static void cqos_rpc_call_process(struct clnt_req *cc)
{
	char *err;
	cqos_ceph_nodes_t *node;
	struct glist_head *glist;

	if (cc->cc_error.re_status == RPC_SUCCESS)
		return;

	err = rpc_sperror(&cc->cc_error, "failed");
	LogCrit(COMPONENT_QOS, "CQOS: Sending RPCmsg failed %s", err);
	free_sperror(err);

	/*
	 * We try to resend bandwidth usage in case of failure.
	 * If underlying TCP connection is gone, we need to
	 * connect again to remote node and try to send message.
	 */
	glist_for_each(glist, &cqos_hosts) {
		node = glist_entry(glist, cqos_ceph_nodes_t, node_list);
		if (node->clnt == cc->cc_clnt) {
			LogCrit(COMPONENT_QOS,
				"CQOS: Found a node with client handle");
			CLNT_DESTROY(node->clnt);
			close(node->fd);
			node->clnt = NULL;
			node->fd = cqos_create_socket(&node->node_addr);
			if (node->fd >= 0)
				node->clnt = cqos_create_rpc_client(node->fd);
			if (node->clnt != NULL)
				cc->cc_clnt = node->clnt;
		}
	}

	if (cc->cc_refreshes-- > 0 &&
	    clnt_req_refresh(cc) == RPC_SUCCESS) {
		LogCrit(COMPONENT_QOS, "CQOS: retry Sending RPC msg");
		/*
		 * These cluster qos messages are for sending bandwidth usage
		 * in a given time interval. We retry only once sending the
		 * message. After timeout these values becomes irrelavant.
		 */
		cc->cc_error.re_status = CLNT_CALL_ONCE(cc);
		return;
	}
}

/* Free RPC callback request context */
static void cqos_rpc_call_free(struct clnt_req *cc, size_t unused,
			const char *file, int line, const char *function)
{
	gsh_free(cc, MEM_COMP_QOS);
}

/**
 * This function fills the request to be sent using input message
 * structure, and then calls request setup to send message.
 * It doesn't wait for reply from other node as cluster qos doesn't
 * reply to message. It is one way message.
 *
 * @param [in] clnt pointer generated by rpc, for given fd.
 * @param [in] cluster qos message to be sent to other nodes.
 * @return true if message sent, false if msg can't be sent.
 */

static bool cqos_send_rpc_msg(CLIENT *clnt, cluster_qos_msg msg)
{
	struct clnt_req *cc;
	char *err;

	if (clnt == NULL)
		return false;

	cc = gsh_malloc(sizeof(*cc), MEM_COMP_QOS);

	clnt_req_fill(cc, clnt, authnone_ncreate(), 1,
		     (xdrproc_t)xdr_cluster_qos_msg, &msg, (xdrproc_t)xdr_void,
		     NULL);
	cc->cc_free_cb = cqos_rpc_call_free;

	cc->cc_error.re_status = clnt_req_setup(cc, msg_tout);
	if (cc->cc_error.re_status != RPC_SUCCESS) {
		err = rpc_sperror(&cc->cc_error, "failed");
		LogCrit(COMPONENT_QOS, "clnt req setup failed %s", err);
		free_sperror(err);
		clnt_req_release(cc);
		return false;
	}

	if (!cqos_ensure_evchan(clnt)) {
		LogCrit(COMPONENT_QOS,
			"CQOS: evchan missing for CLNT_CALL_BACK");
		clnt_req_release(cc);
		return false;
	}

	cc->cc_process_cb = cqos_rpc_call_process;

	cc->cc_error.re_status = CLNT_CALL_BACK(cc);
	if (cc->cc_error.re_status != RPC_SUCCESS) {
		err = rpc_sperror(&cc->cc_error, "failed");
		LogCrit(COMPONENT_QOS, "Sending RPC msg failed %s", err);
		free_sperror(err);
		clnt_req_release(cc);
		return false;
	}

	return true;
}

/**
 * This function tries to send message to other nodes if valid fd and rpc
 * client handle exists. If fd and rpc client handle doesn't exist, it
 * creates them and sends message.
 *
 * If message sending fails, it has a retry mechanism.
 *
 * The fd and rpc client handle are stored globally in glist for
 * each node, and will be used whenever msg needs to be sent to that node.
 *
 * It is a persistent TCP connection, so we need not create fd, handle
 * everytime.
 *
 * @param [in] Address of file descriptor
 * @param [in] Address of rpc client handle pointer
 * @param [in] address of the node to which msg needs to be sent
 * @param [in] cqos message that needs to be sent
 */
static void cqos_process_send_msg(int *fd, CLIENT **clnt, sockaddr_t sockaddr,
				  cluster_qos_msg pubsub_msg)
{
	int retries = 0;

retry:
	if ((*fd >= 0) && (*clnt != NULL)) {
		if (!cqos_send_rpc_msg(*clnt, pubsub_msg)) {
			if (*clnt != NULL)
				CLNT_DESTROY(*clnt);
			close(*fd);
			*fd = -1;
			*clnt = NULL;
			retries++;
			if (retries > CQOS_MAX_RETRIES)
				return;
			goto retry;
		}
	} else if (*fd >= 0) {
		*clnt = cqos_create_rpc_client(*fd);
		if (*clnt != NULL) {
			if (!cqos_send_rpc_msg(*clnt, pubsub_msg)) {
				if (*clnt != NULL)
					CLNT_DESTROY(*clnt);
				close(*fd);
				*fd = -1;
				*clnt = NULL;
				retries++;
				if (retries > CQOS_MAX_RETRIES)
					return;
				goto retry;
			}
		}
	} else if (*fd < 0) {
		*fd = cqos_create_socket(&sockaddr);
		if (*fd >= 0) {
			*clnt = cqos_create_rpc_client(*fd);
			if (*clnt != NULL) {
				if (!cqos_send_rpc_msg(*clnt, pubsub_msg)) {
					if (*clnt != NULL)
						CLNT_DESTROY(*clnt);
					close(*fd);
					*fd = -1;
					*clnt = NULL;
					retries++;
					if (retries > CQOS_MAX_RETRIES)
						return;
					goto retry;
				}
			} else {
				close(*fd);
				*fd = -1;
			}
		}
	}
}

/**
 * This function tries to send sub/unsub message to all nodes.
 * This is to notify all ceph nodes that currently I/O is going on
 * for this export/client.
 *
 * Unsub message is to notify that I/O has been completed.
 *
 * Subscribe/Unsubscribe messages are for all ceph nodes.
 *
 * Publish messages are only for subscribed nodes to this export/client.
 *
 * we need not send any message to local system.
 *
 * @param [in] qos class Pointer of the export/client.
 * @param [in] cluster qos message that needs to be sent.
 */
static void send_sub_unsub_msg_to_nodes(qos_class_t *class_ptr,
					cluster_qos_msg sub_unsub_qos_msg)
{
	cqos_ceph_nodes_t *node;
	struct glist_head *glist;

	glist_for_each(glist, &cqos_hosts) {
		node = glist_entry(glist, cqos_ceph_nodes_t, node_list);
		cqos_process_send_msg(&node->fd, &node->clnt,
				      node->node_addr,
				      sub_unsub_qos_msg);
	}
}

/**
 * This function will be called from QoS module, it checks for bandwidth
 * subscription for export/client.
 *
 * If this node already subscribed for this op_type then it returns.
 *
 * If not subscribed, it will subscribe and send msg to all nodes.
 *
 * @param [in] qos class Pointer of the export/client.
 * @param [in] op_type is READ/WRITE
 * @param [in] class_type is EXPORT/CLIENT
 */
void check_cqos_bw_subscription(qos_class_t *qos_class, qos_op_type_t op_type,
				qos_class_type_t class_type)
{
	qos_bucket_t *bucket;
	uint32_t subscribe_op = 0;
	cluster_qos_msg subscribe_qos_msg;
	uint64_t ctime = get_time_in_usec();

	memset(&subscribe_qos_msg, 0, sizeof(cluster_qos_msg));

	bucket = qos_get_bw_bucket(qos_class, op_type);

	if (bucket == NULL) {
		return;
	}

	if (bucket->bw_subscribed == true) {
		return;
	}

	bucket->bw_subscribed = true;
	bucket->bw_last_published_time = ctime;
	if (class_type == QOS_EXPORT) {
		subscribe_qos_msg.export_id = qos_class->gsh_export->export_id;
		subscribe_op = (op_type == QOS_READ) ? CQOS_EXPORT_RBW
						     : CQOS_EXPORT_WBW;
		if (qos_class->combined_rw_bw_control == true)
			subscribe_op = CQOS_EXPORT_CBW;
		subscribe_qos_msg.cqos_ops |= 1 << (subscribe_op - 1);
	} else if (class_type == QOS_CLIENT) {
		memcpy(&subscribe_qos_msg.client_addr,
		       &qos_class->gsh_client->cl_addrbuf, sizeof(sockaddr_t));
		subscribe_op = (op_type == QOS_READ) ? CQOS_CLIENT_RBW
						     : CQOS_CLIENT_WBW;
		if (qos_class->combined_rw_bw_control == true)
			subscribe_op = CQOS_CLIENT_CBW;
		subscribe_qos_msg.cqos_ops |= 1 << (subscribe_op - 1);
	}

	subscribe_qos_msg.cqos_cmd = CQOS_SUBSCRIBE;
	send_sub_unsub_msg_to_nodes(qos_class, subscribe_qos_msg);
}

/**
 * This function will be called from QoS module, it checks for iops
 * subscription for export/client.
 *
 * If this node already subscribed for this op_type then it returns.
 *
 * If not subscribed, it will subscribe and send msg to all nodes.
 *
 * @param [in] qos class Pointer of the export/client.
 * @param [in] op_type is READ/WRITE
 * @param [in] class_type is EXPORT/CLIENT
 */
void check_cqos_iops_subscription(qos_class_t *qos_class, qos_op_type_t op_type,
				  qos_class_type_t class_type)
{
	qos_bucket_t *bucket;
	uint32_t subscribe_op = 0;
	cluster_qos_msg subscribe_qos_msg;
	uint64_t ctime = get_time_in_usec();

	memset(&subscribe_qos_msg, 0, sizeof(cluster_qos_msg));

	bucket = qos_get_iops_bucket(qos_class, op_type);

	if (bucket == NULL) {
		return;
	}

	if (bucket->iops_subscribed == true) {
		return;
	}

	bucket->iops_subscribed = true;
	bucket->iops_last_published_time = ctime;
	if (class_type == QOS_EXPORT) {
		subscribe_qos_msg.export_id = qos_class->gsh_export->export_id;
		subscribe_op = (op_type == QOS_READ) ? CQOS_EXPORT_RIOPS
						     : CQOS_EXPORT_WIOPS;

		if (qos_class->combined_rw_iops_control == true)
			subscribe_op = CQOS_EXPORT_CIOPS;
		subscribe_qos_msg.cqos_ops |= 1 << (subscribe_op - 1);
	} else if (class_type == QOS_CLIENT) {
		memcpy(&subscribe_qos_msg.client_addr,
		       &qos_class->gsh_client->cl_addrbuf, sizeof(sockaddr_t));
		subscribe_op = (op_type == QOS_READ) ? CQOS_CLIENT_RIOPS
						     : CQOS_CLIENT_WIOPS;
		if (qos_class->combined_rw_iops_control == true)
			subscribe_op = CQOS_CLIENT_CIOPS;
		subscribe_qos_msg.cqos_ops |= 1 << (subscribe_op - 1);
	}
	subscribe_qos_msg.cqos_cmd = CQOS_SUBSCRIBE;
	send_sub_unsub_msg_to_nodes(qos_class, subscribe_qos_msg);
}

/**
 * This function finds glist entry of the given node address
 * for taking fd, rpc client handle to send publish message.
 *
 * Global glist contains all ceph node entries.
 *
 * Avl tree in each qos_class of export/client contains nodes info of
 * all subscribed nodes to that export/client I/Os.
 *
 * @param [in] Address of node to which publish message needs to be sent
 * @param [in] publish message that needs to be sentc
 */
static void send_publish_message(sockaddr_t sockaddr, cluster_qos_msg cqos_msg)
{
	cqos_ceph_nodes_t *node;
	struct glist_head *glist;

	glist_for_each(glist, &cqos_hosts) {
		node = glist_entry(glist, cqos_ceph_nodes_t, node_list);
		if (!sockaddr_cmp(&node->node_addr, &sockaddr, true)) {
			cqos_process_send_msg(&node->fd, &node->clnt,
					      node->node_addr,
					      cqos_msg);
			return;
		}
	}
}

/**
 * This function populates all write op_type info, for bw/iops
 * If this node is already subscribed i.e if write I/O is going on,
 * then it will populate processed bw/iops count for export/client.
 *
 * If there is no write bytes processed for this class in the given time
 * interval, then it will populate unsubscribed message as well for sending.
 *
 *
 * In the message, cqos_ops field 12 bits are used for denoting op_type
 *
 * From Lower bits, 0,1,2 bits are used for export read/write/combined bw.
 *                  3,4,5 bits are used for export read/write/combined iops.
 *                  6,7,8 bits are used for client read/write/combined bw
 *                  9,10,11 bits are used for client read/write/combined iops.
 *
 * @param [in] bucket Pointer to the qos_bucket_t representing the bucket
 * @param [in] qos class Pointer of the export/client.
 * @param [in] class_type is EXPORT/CLIENT
 * @param [in] publish cqos message that needs to be sent
 * @param [in] unsubscribe cqos message that needs to be sent
 */
static void populate_pubsub_write_info(qos_bucket_t *bucket,
				       qos_class_t *class_ptr,
				       qos_class_type_t class_type,
				       cluster_qos_msg *publish_qos_msg,
				       cluster_qos_msg *unsubscribe_qos_msg)
{
	cqos_ops_type_t cqos_op_type;
	uint16_t export_id = class_ptr->gsh_export->export_id;
	sockaddr_t *cli_addr = &class_ptr->gsh_client->cl_addrbuf;
	bool bw_subscribed = bucket->bw_subscribed;
	uint64_t bw_consumed = bucket->bw_consumed_intime;
	bool iops_subscribed = bucket->iops_subscribed;
	uint64_t iops_consumed = bucket->iops_consumed_intime;
	uint64_t ctime = get_time_in_usec();
	cluster_qos_msg *pMsg = publish_qos_msg;
	cluster_qos_msg *uMsg = unsubscribe_qos_msg;

	pMsg->cqos_cmd = CQOS_PUBLISH;
	uMsg->cqos_cmd = CQOS_UNSUBSCRIBE;

	if (class_type == QOS_EXPORT) {
		pMsg->export_id = export_id;
		uMsg->export_id = export_id;
	} else if (class_type == QOS_CLIENT) {
		memcpy(&pMsg->client_addr, cli_addr, sizeof(sockaddr_t));
		memcpy(&uMsg->client_addr, cli_addr, sizeof(sockaddr_t));
	}

	if (bw_subscribed == true && bw_consumed != 0) {
		cqos_op_type = (class_type == QOS_EXPORT) ? CQOS_EXPORT_WBW
							  : CQOS_CLIENT_WBW;
		pMsg->cqos_ops |= 1 << (cqos_op_type - 1);
		if (class_type == QOS_EXPORT) {
			pMsg->export_wbw = bw_consumed;
		} else if (class_type == QOS_CLIENT) {
			pMsg->client_wbw = bw_consumed;
		}
		bucket->bw_last_published_time = ctime;
		bucket->bw_consumed_intime = 0;
	} else if (bw_subscribed == true && bw_consumed == 0) {
		if (ctime - bucket->bw_last_published_time > CQOS_UNSUB_TIME) {
			cqos_op_type = (class_type == QOS_EXPORT)
					       ? CQOS_EXPORT_WBW
					       : CQOS_CLIENT_WBW;
			uMsg->cqos_ops |= 1 << (cqos_op_type - 1);
			bucket->bw_subscribed = false;
		}
	}

	if (iops_subscribed == true && iops_consumed != 0) {
		cqos_op_type = (class_type == QOS_EXPORT) ? CQOS_EXPORT_WIOPS
							  : CQOS_CLIENT_WIOPS;
		pMsg->cqos_ops |= 1 << (cqos_op_type - 1);
		if (class_type == QOS_EXPORT) {
			pMsg->export_wiops = iops_consumed;
		} else if (class_type == QOS_CLIENT) {
			pMsg->client_wiops = iops_consumed;
		}
		bucket->iops_last_published_time = ctime;
		bucket->iops_consumed_intime = 0;
	} else if (iops_subscribed == true && iops_consumed == 0) {
		if (ctime - bucket->iops_last_published_time >
		    CQOS_UNSUB_TIME) {
			cqos_op_type = (class_type == QOS_EXPORT)
					       ? CQOS_EXPORT_WIOPS
					       : CQOS_CLIENT_WIOPS;
			uMsg->cqos_ops |= 1 << (cqos_op_type - 1);
			bucket->iops_subscribed = false;
		}
	}
}

/**
 * This function papulates all read op_type info, for bw/iops
 * If this node is already subscribed i.e if read I/O is going on,
 * then it will populate processed bw/iops count for export/client.
 *
 * If there is no read bytes processed for this class in the given time
 * interval, then it will populate unsubscribed message as well for sending.
 *
 *
 * In the message, cqos_ops field 12 bits are used for denoting op_type
 *
 * From Lower bits, 0,1,2 bits are used for export read/write/combined bw.
 *                  3,4,5 bits are used for export read/write/combined iops.
 *                  6,7,8 bits are used for client read/write/combined bw
 *                  9,10,11 bits are used for client read/write/combined iops.
 *
 *
 * @param [in] bucket Pointer to the qos_bucket_t representing the bucket
 * @param [in] qos class Pointer of the export/client.
 * @param [in] class_type is EXPORT/CLIENT
 * @param [in] publish cqos message that needs to be sent
 * @param [in] unsubscribe cqos message that needs to be sent
 */
static void populate_pubsub_read_info(qos_bucket_t *bucket,
				      qos_class_t *class_ptr,
				      qos_class_type_t class_type,
				      cluster_qos_msg *publish_qos_msg,
				      cluster_qos_msg *unsubscribe_qos_msg)
{
	cqos_ops_type_t cqos_op_type;
	uint16_t export_id = class_ptr->gsh_export->export_id;
	sockaddr_t *cli_addr = &class_ptr->gsh_client->cl_addrbuf;
	bool bw_subscribed = bucket->bw_subscribed;
	uint64_t bw_consumed = bucket->bw_consumed_intime;
	bool iops_subscribed = bucket->iops_subscribed;
	uint64_t iops_consumed = bucket->iops_consumed_intime;
	uint64_t ctime = get_time_in_usec();
	cluster_qos_msg *pMsg = publish_qos_msg;
	cluster_qos_msg *uMsg = unsubscribe_qos_msg;

	pMsg->cqos_cmd = CQOS_PUBLISH;
	uMsg->cqos_cmd = CQOS_UNSUBSCRIBE;

	if (class_type == QOS_EXPORT) {
		pMsg->export_id = export_id;
		uMsg->export_id = export_id;
	} else if (class_type == QOS_CLIENT) {
		memcpy(&pMsg->client_addr, cli_addr, sizeof(sockaddr_t));
		memcpy(&uMsg->client_addr, cli_addr, sizeof(sockaddr_t));
	}

	if (bw_subscribed == true && bw_consumed != 0) {
		cqos_op_type = (class_type == QOS_EXPORT) ? CQOS_EXPORT_RBW
							  : CQOS_CLIENT_RBW;
		pMsg->cqos_ops |= 1 << (cqos_op_type - 1);
		if (class_type == QOS_EXPORT) {
			pMsg->export_rbw = bw_consumed;
		} else if (class_type == QOS_CLIENT) {
			pMsg->client_rbw = bw_consumed;
		}
		bucket->bw_last_published_time = ctime;
		bucket->bw_consumed_intime = 0;
	} else if (bw_subscribed == 1 && bw_consumed == 0) {
		if (ctime - bucket->bw_last_published_time > CQOS_UNSUB_TIME) {
			cqos_op_type = (class_type == QOS_EXPORT)
					       ? CQOS_EXPORT_RBW
					       : CQOS_CLIENT_RBW;
			uMsg->cqos_ops |= 1 << (cqos_op_type - 1);
			bucket->bw_subscribed = false;
		}
	}

	if (iops_subscribed == true && iops_consumed != 0) {
		cqos_op_type = (class_type == QOS_EXPORT) ? CQOS_EXPORT_RIOPS
							  : CQOS_CLIENT_RIOPS;
		pMsg->cqos_ops |= 1 << (cqos_op_type - 1);
		if (class_type == QOS_EXPORT) {
			pMsg->export_riops = iops_consumed;
		} else if (class_type == QOS_CLIENT) {
			pMsg->client_riops = iops_consumed;
		}
		bucket->iops_last_published_time = ctime;
		bucket->iops_consumed_intime = 0;
	} else if (iops_subscribed == true && iops_consumed == 0) {
		if (ctime - bucket->iops_last_published_time >
		    CQOS_UNSUB_TIME) {
			cqos_op_type = (class_type == QOS_EXPORT)
					       ? CQOS_EXPORT_RIOPS
					       : CQOS_CLIENT_RIOPS;
			uMsg->cqos_ops |= 1 << (cqos_op_type - 1);
			bucket->iops_subscribed = false;
		}
	}
}

/**
 * This function sends publish message to all subscribed nodes
 * of this export/client.
 *
 * Avl tree in each qos class contains all subscribed nodes.
 *
 * @param [in] qos class Pointer of the export/client.
 * @param [in] publish message that needs to be sent
 */
static void publish_bw_iops_values(qos_class_t *class_ptr,
				   cluster_qos_msg publish_qos_msg)
{
	struct cqos_nodes_info *lk;
	struct avltree_node *node;

	for (node = avltree_first(&class_ptr->cqos_subnodes); node != NULL;
	     node = avltree_next(node)) {
		lk = avltree_container_of(node, cqos_nodes_info_t,
					  cqos_avl_node);
		if (lk == NULL)
			return;
		if ((lk->subscribed_ops & publish_qos_msg.cqos_ops) != 0) {
			send_publish_message(lk->node_addr, publish_qos_msg);
		}
	}
}

/**
 * This function handles setting combined bw/iops bit in messages.
 *
 * In the message, cqos_ops field 12 bits are used for denoting op_type
 *
 * From Lower bits, 0,1,2 bits are used for export read/write/combined bw.
 *                  3,4,5 bits are used for export read/write/combined iops.
 *                  6,7,8 bits are used for client read/write/combined bw
 *                  9,10,11 bits are used for client read/write/combined iops.
 *
 * @param [in] qos class Pointer of the export/client.
 * @param [in] class_type is EXPORT/CLIENT
 * @param [in] publish cqos message that needs to be sent
 * @param [in] unsubscribe cqos message that needs to be sent
 */
static void cqos_handle_combined_bw_iops(qos_class_t *class_ptr,
					 qos_class_type_t class_type,
					 cluster_qos_msg *publish_qos_msg,
					 cluster_qos_msg *unsubscribe_qos_msg)
{
	cluster_qos_msg *pMsg = publish_qos_msg;
	cluster_qos_msg *uMsg = unsubscribe_qos_msg;

	if (class_ptr->combined_rw_bw_control == true) {
		if (class_type == QOS_EXPORT) {
			if (pMsg->cqos_ops & RW_EXP_BITS) {
				pMsg->cqos_ops &= ~RW_EXP_BITS;
				pMsg->cqos_ops |= 1 << (CQOS_EXPORT_CBW - 1);
			}
			if (uMsg->cqos_ops & RW_EXP_BITS) {
				uMsg->cqos_ops &= ~RW_EXP_BITS;
				uMsg->cqos_ops |= 1 << (CQOS_EXPORT_CBW - 1);
			}
		} else if (class_type == QOS_CLIENT) {
			if (pMsg->cqos_ops & RW_CLI_BITS) {
				pMsg->cqos_ops &= ~RW_CLI_BITS;
				pMsg->cqos_ops |= 1 << (CQOS_CLIENT_CBW - 1);
			}
			if (uMsg->cqos_ops & RW_CLI_BITS) {
				uMsg->cqos_ops &= ~RW_CLI_BITS;
				uMsg->cqos_ops |= 1 << (CQOS_CLIENT_CBW - 1);
			}
		}
	}

	if (class_ptr->combined_rw_iops_control == true) {
		if (class_type == QOS_EXPORT) {
			if (pMsg->cqos_ops & RW_IOP_EXP_BITS) {
				pMsg->cqos_ops &= ~RW_IOP_EXP_BITS;
				pMsg->cqos_ops |= 1 << (CQOS_EXPORT_CIOPS - 1);
			}
			if (uMsg->cqos_ops & RW_IOP_EXP_BITS) {
				uMsg->cqos_ops &= ~RW_IOP_EXP_BITS;
				uMsg->cqos_ops |= 1 << (CQOS_EXPORT_CIOPS - 1);
			}
		} else if (class_type == QOS_CLIENT) {
			if (pMsg->cqos_ops & RW_IOP_CLI_BITS) {
				pMsg->cqos_ops &= ~RW_IOP_CLI_BITS;
				pMsg->cqos_ops |= 1 << (CQOS_CLIENT_CIOPS - 1);
			}
			if (uMsg->cqos_ops & RW_IOP_CLI_BITS) {
				uMsg->cqos_ops &= ~RW_IOP_CLI_BITS;
				uMsg->cqos_ops |= 1 << (CQOS_CLIENT_CIOPS - 1);
			}
		}
	}
}

/**
 * This function creates publish/unsubscribe messages to be sent.
 *
 * It sends publish message if any I/Os are going on
 *
 * else it will send unsubscribe message for the ops
 *
 * In the message, cqos_ops field 12 bits are used for denoting op_type
 *
 * From Lower bits, 0,1,2 bits are used for export read/write/combined bw.
 *                  3,4,5 bits are used for export read/write/combined iops.
 *                  6,7,8 bits are used for client read/write/combined bw
 *                  9,10,11 bits are used for client read/write/combined iops.
 *
 * @param [in] qos class Pointer of the export/client.
 * @param [in] class_type is EXPORT/CLIENT
 */
static void cqos_send_publish_unsub_ps_pc(qos_class_t *class_ptr,
					  qos_class_type_t class_type)
{
	qos_bucket_t *bucket;
	qos_class_t *qos_class = class_ptr;
	cluster_qos_msg publish_qos_msg;
	cluster_qos_msg unsubscribe_qos_msg;

	if (qos_class == NULL)
		return;

	memset(&publish_qos_msg, 0, sizeof(cluster_qos_msg));
	memset(&unsubscribe_qos_msg, 0, sizeof(cluster_qos_msg));

	if (qos_class->bw_enabled)
		bucket = qos_get_bw_bucket(qos_class, QOS_WRITE);
	else
		bucket = qos_get_iops_bucket(qos_class, QOS_WRITE);

	if (bucket == NULL)
		return;

	PTHREAD_MUTEX_lock(&bucket->lock);
	populate_pubsub_write_info(bucket, class_ptr, class_type,
				   &publish_qos_msg, &unsubscribe_qos_msg);
	PTHREAD_MUTEX_unlock(&bucket->lock);

	if (qos_class->bw_enabled)
		bucket = qos_get_bw_bucket(qos_class, QOS_READ);
	else
		bucket = qos_get_iops_bucket(qos_class, QOS_READ);

	if (bucket == NULL)
		return;

	PTHREAD_MUTEX_lock(&bucket->lock);
	populate_pubsub_read_info(bucket, class_ptr, class_type,
				  &publish_qos_msg, &unsubscribe_qos_msg);
	PTHREAD_MUTEX_unlock(&bucket->lock);

	cqos_handle_combined_bw_iops(class_ptr, class_type, &publish_qos_msg,
				     &unsubscribe_qos_msg);

	/* If there is no bw/iops count filled, no need to send publish msg */
	if (publish_qos_msg.cqos_ops != 0) {
		publish_bw_iops_values(class_ptr, publish_qos_msg);
	}

	/*
	 *  If unsubscribed ops are set, we need to send unsubscribe msg
	 *  It means there is no I/O happening for sometime for that op....
	 */
	if (unsubscribe_qos_msg.cqos_ops != 0) {
		send_sub_unsub_msg_to_nodes(class_ptr, unsubscribe_qos_msg);
	}
}

/**
 * This function is a callback for each export to process and send cqos msgs.
 * this function will handle all export level cqos messages
 *
 * @param [in] gsh_export pointer
 * @param [in] argument to be passed to callback, we are not using this param.
 * @return true, there is no failure case for this function.
 */
static bool ps_cqos_pubsub_cb(struct gsh_export *gsh_export, void *state)
{
	if (gsh_export) {
		if (!gsh_export->qos_class)
			return true;
		cqos_send_publish_unsub_ps_pc(gsh_export->qos_class,
					      QOS_EXPORT);
	}
	return true;
}

/**
 * This function is a callback for each client to process and send cqos msgs.
 * this function will handle all client level cqos messages
 *
 * @param [in] gsh_client pointer
 * @param [in] argument to be passed to callback, we are not using this param.
 * @return true, there is no failure case for this function.
 */
static bool pc_cqos_pubsub_cb(struct gsh_client *gsh_client, void *state)
{
	if (gsh_client) {
		if (!gsh_client->qos_class)
			return true;
		cqos_send_publish_unsub_ps_pc(gsh_client->qos_class,
					      QOS_CLIENT);
	}
	return true;
}

/**
 * This function is a thread function for sending cqos messages
 *
 * This thread sends only publish/unsubscribe messages
 *
 * All subscribe messages sent only when I/O is initiated
 *
 * @param [in]  This thread not taking any params
 * @return      void *ptr, this thread doesn't return anything
 */
static void *cqos_pubsub_thread_func(void *arg)
{
	while (true) {
		switch (g_qos_config->qos_type) {
		case QOS_NOT_ENABLED:
			LogDebug(COMPONENT_QOS, "QOS not enabled :%d",
				 g_qos_config->qos_type);
			break;
		case QOS_PER_EXPORT_ENABLED:
			foreach_gsh_export(ps_cqos_pubsub_cb, false, NULL);
			break;
		case QOS_PER_CLIENT_ENABLED:
			foreach_gsh_client(pc_cqos_pubsub_cb, NULL);
			break;
		case QOS_PEREXPORT_PERCLIENT_ENABLED:
			foreach_gsh_export(ps_cqos_pubsub_cb, false, NULL);
			break;
		default:
			LogDebug(COMPONENT_QOS, " Something really wrong:%d",
				 g_qos_config->qos_type);
			break;
		}

		/*
		 * This thread wakes up based on cqos_msg_interval config value
		 * this config value is in milli seconds
		 */
		usleep(g_qos_config->cqos_msg_interval * CQOS_USEC_IN_MSEC);
	}
	return NULL;
}

/**
 * This function creates thread required for sending cluster qos msgs
 *
 * @param [in] --
 */
pthread_t cqos_thread;

static void cqos_thread_init(void)
{
	int ret = 0;

	if (cqos_initialized == 0) {
		ret = pthread_create(&cqos_thread, NULL,
				     cqos_pubsub_thread_func, NULL);
		if (ret != 0) {
			LogFatal(COMPONENT_QOS,
				"CQOS: Thread creation failed error %d (%s)",
				errno, strerror(errno));
		}

		pthread_detach(cqos_thread);
	}
	cqos_initialized = 1;
	LogDebug(COMPONENT_QOS, "CQOS: Cluster QOS thread is initialized");
}

/**
 * This function is cluster QoS initialization function
 * This function gets called from nfs init time.
 *
 * It will get called only if QoS is enabled, and we have ceph nodes IPs.
 *
 * @param [in] --
 */
void cluster_qos_init(void)
{
	if (cqos_initialized == 0) {
		LogDebug(COMPONENT_QOS, "Cluster QOS thread_init");
		cqos_thread_init();
	}
}
