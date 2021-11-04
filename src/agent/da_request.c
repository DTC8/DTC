/*
* Copyright [2021] JD.com, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include <inttypes.h>
#include "da_request.h"
#include "da_msg.h"
#include "da_util.h"
#include "da_response.h"
#include "da_server.h"
#include "da_event.h"
#include "da_conn.h"
#include "da_core.h"
#include "da_errno.h"
#include "da_stats.h"
#include "da_time.h"

void req_put(struct msg *msg) {
	struct msg *pmsg; /* peer message (response) */
	ASSERT(msg !=NULL);
	ASSERT(msg->request);
	pmsg = msg->peer;
	if (pmsg != NULL) {
		ASSERT(!pmsg->request && pmsg->peer == msg);
		msg->peer = NULL;
		pmsg->peer = NULL;
		rsp_put(pmsg);
	}
	
	msg_tmo_delete(msg);
	msg_put(msg);
}

static struct msg *req_get(struct conn *conn) {
	struct msg *msg;
	msg = msg_get(conn, true);
	//lack of memory ,close client connection
	if (msg == NULL) {
		conn->error = 1;
		conn->err = CONN_MSG_GET_ERR;
		log_error("req enter,get msg from pool_2msg error,lack of memory");
	}
	return msg;
}

struct msg *req_recv_next(struct context *ctx, struct conn *conn, bool alloc) {

	ASSERT(conn !=NULL && conn->fd>0);

	struct msg *msg;
	if (conn->eof) {
		msg = conn->rmsg;

		if (msg != NULL) {
			conn->rmsg = NULL;
			log_error(
					"eof s %d discarding incomplete req %"PRIu64" len " "%"PRIu32"",
					conn->fd, msg->id, msg->mlen);
			req_put(msg);
		}
		//no half connection
		//if (!conn->active(conn)) {
		conn->done = 1;
		log_debug("c %d active %d is done", conn->fd, conn->active(conn));
		//}
		return NULL;
	}
	msg = conn->rmsg;
	if (msg != NULL) {
		ASSERT(msg->request);
		return msg;
	}
	if (!alloc) {
		return NULL;
	}
	msg = req_get(conn);
	if (msg != NULL) {
		conn->rmsg = msg;
	}
	return msg;
}

struct msg *req_send_next(struct context *ctx, struct conn *conn) {

	int status;
	struct msg *msg, *nmsg; /* current and next message */

	if (conn->connecting) {
		server_connected(ctx, conn);
	}

	nmsg = TAILQ_FIRST(&conn->imsg_q);
	if (nmsg == NULL) {
		/* nothing to send as the server inq is empty */
		log_debug("del epoll out when to server send done");
		status = event_del_out(ctx->evb, conn);
		if (status != 0) {
			conn->error = 1;
			conn->err = CONN_EPOLL_DEL_ERR;
		}
		return NULL;
	}

	msg = conn->smsg;
	if (msg != NULL) {
		ASSERT(msg->request && !msg->done);
		nmsg = TAILQ_NEXT(msg, s_i_tqe);
	}

	conn->smsg = nmsg;

	if (nmsg == NULL) {
		return NULL;
	}

	ASSERT(nmsg->request && !nmsg->done);

	log_debug("send next req %"PRIu64" len %"PRIu32" type %d on " "s %d",
			nmsg->id, nmsg->mlen, nmsg->cmd, conn->fd);

	return nmsg;
}

/*
 * dequeue msg from server send msgq,insert masg into search tree
 */
void req_send_done(struct context *ctx, struct conn *conn, struct msg *msg) {

	ASSERT(conn->type & BACKWORK);
	ASSERT(msg != NULL && conn->smsg == NULL);
	ASSERT(msg->request && !msg->done);
	ASSERT(msg->owner != conn);

	log_debug("send done req %"PRIu64" len %"PRIu32" type %d on " "s %d",
			msg->id, msg->mlen, msg->cmd, conn->fd);

	/*struct cache_instance *ci;
	ci = conn->owner;
	if (ci->failure_num > 0)
	{
		ci->failure_num--;		
	}*/

	/* dequeue the message (request) from server inq */
	conn->dequeue_inq(ctx, conn, msg);

	/*put msg into msg search tree*/
	conn->en_msgtree(ctx, conn, msg);
}

void req_client_enqueue_omsgq(struct context *ctx, struct conn *conn,
		struct msg *msg) {
	ASSERT(msg->request);
	ASSERT(conn->type & FRONTWORK);

	TAILQ_INSERT_TAIL(&conn->omsg_q, msg, c_o_tqe);
	msg->cli_outq = 1;
}

void req_client_dequeue_omsgq(struct context *ctx, struct conn *conn,
		struct msg *msg) {
	ASSERT(msg->request);
	ASSERT(conn->type & FRONTWORK);

	TAILQ_REMOVE(&conn->omsg_q, msg, c_o_tqe);
	msg->cli_outq = 0;
}

void req_client_enqueue_imsgq(struct context *ctx, struct conn *conn,
		struct msg *msg) {
	ASSERT(msg->request);
	ASSERT(conn->type & FRONTWORK);

	TAILQ_INSERT_TAIL(&conn->imsg_q, msg, c_i_tqe);
	msg->cli_inq = 1;
}

void req_client_dequeue_imsgq(struct context *ctx, struct conn *conn,
		struct msg *msg) {
	ASSERT(msg->request);
	ASSERT(conn->type & FRONTWORK);

	TAILQ_REMOVE(&conn->imsg_q, msg, c_i_tqe);
	msg->cli_inq = 0;
}

void req_server_enqueue_imsgq(struct context *ctx, struct conn *conn,
		struct msg *msg) {
	ASSERT(msg->request);
	ASSERT(conn->type & BACKWORK);
	struct cache_instance *ci;
	
	msg_tmo_insert(msg, conn);
	TAILQ_INSERT_TAIL(&conn->imsg_q, msg, s_i_tqe);	
	msg->sev_inq = 1;	
	//TODO
	ci = conn->owner;
	stats_server_incr(ctx, ci, server_in_queue);
}

void req_server_dequeue_imsgq(struct context *ctx, struct conn *conn,
		struct msg *msg) {
	ASSERT(msg->request);
	ASSERT(conn->type & BACKWORK);

	TAILQ_REMOVE(&conn->imsg_q, msg, s_i_tqe);
	msg->sev_inq = 0;
	//TODO
	struct cache_instance *ci;
	ci = conn->owner;
	stats_server_decr(ctx, ci, server_in_queue);
}

/*
 * insert a msg to a server msg rbtree
 */
void req_server_en_msgtree(struct context *ctx, struct conn *conn,
		struct msg *msg) {
	ASSERT(msg->request);
	ASSERT(conn->type & BACKWORK);

	struct rbnode *node;
	struct cache_instance *ci;
	/*put msg into msg search tree*/
	node = &msg->msg_rbe;
	node->key = msg->id;
	node->data = msg;
	log_debug("msg id:%"PRIu64"node key:%"PRIu64"into search tree", msg->id,
			node->key);
	log_debug("src node:%p", node);

	rbtree_insert(&conn->msg_tree, node);
	msg->sev_msgtree = 1;
	//TODO
	ci = conn->owner;
	stats_server_incr(ctx, ci, server_in_tree);
}

/*
 *  delete a msg from server msg rbtree
 */
void req_server_de_msgtree(struct context *ctx, struct conn *conn,
		struct msg *msg) {
	ASSERT(msg->request);
	ASSERT(conn->type & BACKWORK);

	struct rbnode *node;

	/*put msg into msg search tree*/
	node = &msg->msg_rbe;
	if(node->data == NULL)
	{
		return;
	}

	rbtree_delete(&conn->msg_tree, node);
	msg->sev_msgtree = 0;
	//TODO
	struct cache_instance *ci;
	ci = conn->owner;
	stats_server_decr(ctx, ci, server_in_tree);
}

static void req_forward_stats(struct context *ctx, struct cache_instance *ci, struct msg *msg)
{
    ASSERT(msg->request);

    stats_server_incr(ctx, ci, server_requests);
    stats_server_incr_by(ctx, ci, server_request_bytes, msg->mlen);
}

/*
 * msg not in any queue ,can reply client
 */
static void req_make_loopback(struct context *ctx, struct conn *conn,
		struct msg *msg) {

	ASSERT((conn->type & FRONTWORK) && !(conn->type & LISTENER));

	log_debug(
			"loopback req %"PRIu64" len %"PRIu32" type %d from " "c %d failed",
			msg->id, msg->mlen, msg->cmd, conn->fd);

	msg->done = 1;

	if (conn->writecached == 0 && conn->connected == 1) {
			cache_send_event(conn);
	}
	//insert msg into client out msg queue
	conn->enqueue_outq(ctx, conn, msg);
	return;
}


//parsed the sql and return the key 
int check_forward_key()
{
	char* query = "select uid,name,city,age,sex from Table_Test where uid=3 and age=2 and sex=1;";
	//char* query = "insert into table_name (uid,age) values (111,2);";
	//char* query = "delete from table_name where uid = 3;";
	//char* query = "update table_name set age=2,sex=1 where uid =10";

	int value = my_da_sql_parsed(query);

	printf("key = %d\n",value);
	return value;
}


void req_process(struct context *ctx, struct conn *c_conn,
		struct msg *msg) 
{
	if(c_conn->stage == CONN_STAGE_LOGGING_IN)	/* this request is a login authentication */
	{
		c_conn->stage == CONN_STAGE_LOGGED_IN;
		if(net_send_ok(msg, c_conn) < 0)  /* default resp login success. */
			return ;
		req_make_loopback(ctx, c_conn, msg);

		return ;
	}
#if 0
	if(!c_conn->logged_in) /* not logged in yet, resp error */
	{
		log_error("log in auth occur something wrong.");
		net_send_error();
		return ;
	}
	
	if(!check_forward_key())
	{
		log_error("check forward key occure something wrong.");
		net_send_error();
		return ;
	}

	req_forward(ctx, c_conn, msg);
#endif
	return;
}

static void req_forward(struct context *ctx, struct conn *c_conn,
		struct msg *msg) {
	struct conn *s_conn;
	struct server_pool *pool;
	struct cache_instance *ci;
	log_debug("req_forward entry");

	/*insert msg to client imsgq，waiting for
	 *the response,first add to backworker's queue
	 */
	c_conn->enqueue_inq(ctx, c_conn, msg);

	pool = c_conn->owner;
	s_conn = server_pool_conn(ctx, (struct server_pool *) c_conn->owner, msg);
	if (s_conn == NULL) {
		log_debug("s_conn null");
		//client connection is still exist,no swallow
		msg->done = 1;
		msg->error = 1;
		msg->err = MSG_REQ_FORWARD_ERR;
		if (msg->frag_owner != NULL) {
			msg->frag_owner->nfrag_done++;
		}
		if (req_done(c_conn, msg)) {
			rsp_forward(ctx, c_conn, msg);
		}
		stats_pool_incr(ctx,pool,forward_error);
		log_error("msg :%"PRIu64" from c %d ,get s_conn fail!", msg->id,
				c_conn->fd);
		return;
	}
	//set the peer_conn of msg
	msg->peer_conn = s_conn;
	
	if (s_conn->writecached == 0 && s_conn->connected == 1) {
		cache_send_event(s_conn);
	}
	
	/*
	 * insert msg to server imsgq,send to dtc server
	 */
	s_conn->enqueue_inq(ctx, s_conn, msg);

	

	//stats counter incr
	if(msg->cmd == MSG_REQ_GET)
	{
		stats_pool_incr_by(ctx,(struct server_pool*)c_conn->owner, pool_request_get_keys, msg->keyCount);
	}
	ci = s_conn->owner;
	req_forward_stats(ctx, s_conn->owner, msg);
	log_debug("forward from c %d to s %d req %"PRIu64" len %"PRIu32 " type %d",
			c_conn->fd, s_conn->fd, msg->id, msg->mlen, msg->cmd);
	
}

/*
 * msg not in any queue ,can reply client
 */
static void req_make_error(struct context *ctx, struct conn *conn,
		struct msg *msg, int msg_errno) {

	ASSERT((conn->type & FRONTWORK) && !(conn->type & LISTENER));

	log_debug(
			"forward req %"PRIu64" len %"PRIu32" type %d from " "c %d failed,msg errno:%d,errmsg:%s",
			msg->id, msg->mlen, msg->cmd, conn->fd, msg_errno,
			GetMsgErrorCodeStr(msg_errno));

	msg->done = 1;
	msg->error = 1;
	msg->err = msg_errno;

	if (conn->writecached == 0 && conn->connected == 1) {
			cache_send_event(conn);
	}
	//insert msg into client out msg queue
	conn->enqueue_outq(ctx, conn, msg);
	return;
}

static bool req_filter_empty(struct context *ctx, struct conn *conn,
		struct msg *msg) {
	ASSERT(conn->client && !conn->proxy);
	if (msg_empty(msg)) {
		ASSERT(conn->rmsg == NULL);
		log_debug("filter empty req %"PRIu64" from c %d", msg->id, conn->fd);
		req_put(msg);		
		return true;
	}
	return false;
}

static void req_recv_done_stats(struct context *ctx, struct server_pool *pool,
		struct msg *msg)
{
	stats_pool_incr(ctx, pool, pool_requests);
	stats_pool_incr_by(ctx, pool, pool_request_bytes, msg->mlen);
}

void req_recv_done(struct context *ctx, struct conn *conn, struct msg *msg,
		struct msg *nmsg) {

	int status;
	struct server_pool *pool;
	struct msg_tqh frag_msgq;
	struct msg *sub_msg;
	struct msg *tmsg; /* tmp next message */

	ASSERT((conn->type & FRONTWORK) && !(conn->type & LISTENER));
	ASSERT(msg->request);
	ASSERT(msg->owner == conn);
	ASSERT(conn->rmsg == msg);
	ASSERT(nmsg == NULL || nmsg->request);

	/*switch the msg been process now*/
	conn->rmsg = nmsg;
	if (req_filter_empty(ctx, conn, msg)) {
		ASSERT(conn->rmsg == NULL);
		log_debug("filter a empty msg: %" PRIu64 "in conn:%d", msg->id,
				conn->fd);
		return;
	}

	pool = (struct server_pool *) conn->owner;

	conn->isvalid = 1;

	req_recv_done_stats(ctx, pool, msg);

	/*msg fragment*/
	TAILQ_INIT(&frag_msgq);
	status = msg->fragment(msg, pool->ncontinuum, &frag_msgq);
	if (status < 0) {
		ASSERT(TAILQ_EMPTY(&frag_msgq));
		if(msg->err == MSG_FRAGMENT_ERR)
			stats_pool_incr(ctx, pool, fragment_error);
		else
			stats_pool_incr(ctx, pool, pool_withoutkey_req);
		req_make_error(ctx, conn, msg, msg->err);
		return;
	}

	ASSERT(TAILQ_EMPTY(&frag_msgq));

	/* if no fragment happened */
	if (TAILQ_EMPTY(&frag_msgq)) {
		req_process(ctx, conn, msg);
		return;
	}

	/*
	 * insert msg into client in queue,it can
	 * be free when client close connection,set done
	 */
	msg->done = 1;
	conn->enqueue_inq(ctx, conn, msg);

	for (sub_msg = TAILQ_FIRST(&frag_msgq); sub_msg != NULL; sub_msg = tmsg) {
		tmsg = TAILQ_NEXT(sub_msg, o_tqe);
		log_debug("req forward msg %"PRIu64"", sub_msg->id);
		TAILQ_REMOVE(&frag_msgq, sub_msg, o_tqe);
		req_forward(ctx, conn, sub_msg);
	}

	
	ASSERT(TAILQ_EMPTY(&frag_msgq));
	return;
}

/*
 * whether req is done
 */
bool req_done(struct conn *c, struct msg *msg) {
	int id, nfragment;
	struct msg *sub_msg, *temp_msg;
	if (!msg->done) {
		return false;
	}
	id = msg->frag_id;
	if (id == 0) {
		/*no fragment is happened*/
		return true;
	}
	if (msg->fdone) {
		return true;
	}
	log_debug("nfrag_done:%d,nfrag:%d",msg->frag_owner->nfrag_done,msg->frag_owner->nfrag);
	if (msg->frag_owner->nfrag_done < msg->frag_owner->nfrag) {
		return false;
	}

	/*
	 * check all sub msg
	 */
	for (sub_msg = msg, temp_msg = TAILQ_PREV(msg, msg_tqh, c_i_tqe);
			temp_msg != NULL && temp_msg->frag_id == id;
			sub_msg = temp_msg, temp_msg = TAILQ_PREV(temp_msg, msg_tqh,
					c_i_tqe)) {
		if (!(sub_msg->done)) {
			return false;
		}
	}
	for (sub_msg = msg, temp_msg = TAILQ_NEXT(msg, c_i_tqe);
			temp_msg != NULL && temp_msg->frag_id == id;
			sub_msg = temp_msg, temp_msg = TAILQ_NEXT(temp_msg, c_i_tqe)) {
		if (!(sub_msg->done)) {
			return false;
		}
	}
	msg->fdone = 1;
	nfragment = 0;

	/*
	 * check all sub msgs and set fdone
	 */
	for (sub_msg = msg, temp_msg = TAILQ_PREV(msg, msg_tqh, c_i_tqe);
			temp_msg != NULL && temp_msg->frag_id == id;
			sub_msg = temp_msg, temp_msg = TAILQ_PREV(temp_msg, msg_tqh,
					c_i_tqe)) {
		temp_msg->fdone = 1;
		nfragment++;
	}
	for (sub_msg = msg, temp_msg = TAILQ_NEXT(msg, c_i_tqe);
			temp_msg != NULL && temp_msg->frag_id == id;
			sub_msg = temp_msg, temp_msg = TAILQ_NEXT(temp_msg, c_i_tqe)) {
		temp_msg->fdone = 1;
		nfragment++;
	}

	ASSERT(msg->frag_owner->nfrag == nfragment);
	return true;
}

bool req_error(struct conn *conn, struct msg *msg) {
	struct msg *cmsg; /* current message */
	uint64_t id;
	uint32_t nfragment;

	ASSERT(msg->request && req_done(conn, msg));

	if (msg->error) {
		return true;
	}

	id = msg->frag_id;
	if (id == 0) {
		return false;
	}
	if (msg->ferror) {
		/* request has already been marked to be in error */
		return true;
	}
	for (cmsg = TAILQ_PREV(msg, msg_tqh, c_i_tqe);
			cmsg != NULL && cmsg->frag_id == id;
			cmsg = TAILQ_PREV(cmsg, msg_tqh, c_i_tqe)) {

		if (cmsg->error) {
			goto ferror;
		}
	}

	for (cmsg = TAILQ_NEXT(msg, c_i_tqe); cmsg != NULL && cmsg->frag_id == id;
			cmsg = TAILQ_NEXT(cmsg, c_i_tqe)) {

		if (cmsg->error) {
			goto ferror;
		}
	}

	return false;

	ferror:

	/*
	 * Mark all fragments of the given request to be in error to speed up
	 * future req_error calls for any of fragments of this request
	 */

	msg->ferror = 1;
	nfragment = 1;

	for (cmsg = TAILQ_PREV(msg, msg_tqh, c_i_tqe);
			cmsg != NULL && cmsg->frag_id == id;
			cmsg = TAILQ_PREV(cmsg, msg_tqh, c_i_tqe)) {
		cmsg->ferror = 1;
		nfragment++;
	}

	for (cmsg = TAILQ_NEXT(msg, c_i_tqe); cmsg != NULL && cmsg->frag_id == id;
			cmsg = TAILQ_NEXT(cmsg, c_i_tqe)) {
		cmsg->ferror = 1;
		nfragment++;
	}

	log_debug(
			"req from c %d with fid %"PRIu64" and %"PRIu32" " "fragments is in error",
			conn->fd, id, nfragment);

	return true;
}
