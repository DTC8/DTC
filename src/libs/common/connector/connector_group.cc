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
#include <stdio.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/socket.h>

#include "list/list.h"
#include "config/dbconfig.h"
#include "connector_client.h"
#include "connector/connector_group.h"
#include "../log/log.h"
#include "socket/unix_socket.h"

static StatCounter statHelperExpireCount;

static void IncHelperExpireCount()
{
	statHelperExpireCount =
		g_stat_mgr.get_stat_int_counter(DATA_SOURCE_EXPIRE_REQ);
	statHelperExpireCount++;
}

class HelperClientList : public ListObject<HelperClientList> {
    public:
	HelperClientList() : helper(NULL)
	{
	}
	~HelperClientList()
	{
		DELETE(helper);
	}
	ConnectorClient *helper;
};

ConnectorGroup::ConnectorGroup(const char *s, const char *name_, int hc, int qs,
			       int statIndex)
	: JobAskInterface<DTCJobOperation>(NULL), queueSize(qs), helperCount(0),
	  helperMax(hc), readyHelperCnt(0), fallback(NULL),
	  average_delay(0) /*默认时延为0*/
{
	sockpath = strdup(s);
	freeHelper.InitList();
	helperList = new HelperClientList[hc];

	recvList = connList = retryList = NULL;

	strncpy(name, name_, sizeof(name) - 1);
	name[sizeof(name) - 1] = '0';

	statTime[0] = g_stat_mgr.get_sample(statIndex);
	statTime[1] = g_stat_mgr.get_sample(statIndex + 1);
	statTime[2] = g_stat_mgr.get_sample(statIndex + 2);
	statTime[3] = g_stat_mgr.get_sample(statIndex + 3);
	statTime[4] = g_stat_mgr.get_sample(statIndex + 4);
	statTime[5] = g_stat_mgr.get_sample(statIndex + 5);
}

ConnectorGroup::~ConnectorGroup()
{
	DELETE_ARRAY(helperList);
	free(sockpath);
}

void ConnectorGroup::record_response_delay(unsigned int t)
{
	if (t <= 0)
		t = 1;

	if (average_delay == 0)
		average_delay = t;

	double N = 20E6 / (average_delay + t);

	if ((unsigned)N > 200000)
		N = 200000; /* 2w/s */
	if ((unsigned)N < 5)
		N = 5; /* 0.5/s */

	average_delay = ((N - 1) / N) * average_delay + t / N;
}

void ConnectorGroup::add_ready_helper()
{
	if (readyHelperCnt == 0) {
		log4cplus_info("helper_group-%s switch to ONLINE mode",
			       sockpath);
		/* force flush job */
		attach_ready_timer(owner);
	}

	if (readyHelperCnt < helperMax)
		readyHelperCnt++;

	if (readyHelperCnt == helperMax)
		log4cplus_debug("%s", "all client ready");
}

void ConnectorGroup::dec_ready_helper()
{
	if (readyHelperCnt > 0)
		readyHelperCnt--;
	if (readyHelperCnt == 0) {
		log4cplus_error(
			"helper_group-%s all clients invalid, switch to OFFLINE mode",
			sockpath);
		/* reply all job */
		attach_ready_timer(owner);
	}
}

void ConnectorGroup::record_process_time(int cmd, unsigned int usec)
{
	static const unsigned char cmd2type[] = {
		/*TYPE_PASS*/ 0,
		/*result_code*/ 0,
		/*DTCResultSet*/ 0,
		/*HelperAdmin*/ 0,
		/*Get*/ 1,
		/*Purge*/ 0,
		/*Insert*/ 2,
		/*Update*/ 3,
		/*Delete*/ 4,
		/*Replace*/ 5,
		/*Flush*/ 0,
		/*Other*/ 0,
		/*Other*/ 0,
		/*Other*/ 0,
		/*Other*/ 0,
		/*Other*/ 0,
		/*Other*/ 0,
		/*Replicate*/ 1,
		/*LocalMigrate*/ 1,
	};
	statTime[0].push(usec);
	int t = cmd2type[cmd];
	if (t)
		statTime[t].push(usec);

	/* 计算新的平均时延 */
	record_response_delay(usec);
}

int ConnectorGroup::do_attach(PollerBase *thread,
			      LinkQueue<DTCJobOperation *>::allocator *a)
{
	owner = thread;
	for (int i = 0; i < helperMax; i++) {
		helperList[i].helper = new ConnectorClient(owner, this, i);
		helperList[i].helper->reconnect();
	}

	queue.SetAllocator(a);
	return 0;
}

int ConnectorGroup::connect_helper(int fd)
{
	struct sockaddr_un unaddr;
	socklen_t addrlen;

	addrlen = init_unix_socket_address(&unaddr, sockpath);
	return connect(fd, (struct sockaddr *)&unaddr, addrlen);
}

void ConnectorGroup::queue_back_task(DTCJobOperation *job)
{
	if (queue.Unshift(job) < 0) {
		job->set_error(-EC_SERVER_ERROR, __FUNCTION__,
			       "insufficient memory");
		job->turn_around_job_answer();
	}
}

void ConnectorGroup::request_completed(ConnectorClient *h)
{
	HelperClientList *h0 = &helperList[h->helperIdx];
	if (h0->ListEmpty()) {
		h0->ListAdd(freeHelper);
		helperCount--;
		attach_ready_timer(owner);
	}
}

void ConnectorGroup::connection_reset(ConnectorClient *h)
{
	HelperClientList *h0 = &helperList[h->helperIdx];
	if (!h0->ListEmpty()) {
		h0->list_del();
		helperCount++;
	}
}

void ConnectorGroup::check_queue_expire(void)
{
	attach_ready_timer(owner);
}

void ConnectorGroup::process_task(DTCJobOperation *job)
{
	if (DRequest::ReloadConfig == job->request_code() &&
	    TaskTypeHelperReloadConfig == job->request_type()) {
		process_reload_config(job);
		return;
	}
	HelperClientList *h0 = freeHelper.NextOwner();
	ConnectorClient *helper = h0->helper;

	log4cplus_debug("process job.....");
	if (helper->support_batch_key())
		job->mark_field_set_with_key();

	Packet *packet = new Packet;
	if (packet->encode_forward_request(job) != 0) {
		delete packet;
		log4cplus_error("[2][job=%d]request error: %m", job->Role());
		job->set_error(-EC_BAD_SOCKET, "ForwardRequest", NULL);
		job->turn_around_job_answer();
	} else {
		h0->ResetList();
		helperCount++;

		helper->attach_task(job, packet);
	}
}

void ConnectorGroup::flush_task(uint64_t now)
{
	//check timeout for helper client
	while (1) {
		DTCJobOperation *job = queue.Front();
		if (job == NULL)
			break;

		if (readyHelperCnt == 0) {
			log4cplus_debug(
				"no available helper, up stream server maybe offline");
			queue.Pop();
			job->set_error(
				-EC_UPSTREAM_ERROR, __FUNCTION__,
				"no available helper, up stream server maybe offline");
			job->turn_around_job_answer();
			continue;
		} else if (job->is_expired(now)) {
			log4cplus_debug(
				"%s", "job is expired in ConnectorGroup queue");
			IncHelperExpireCount();
			queue.Pop();
			job->set_error(
				-ETIMEDOUT, __FUNCTION__,
				"job is expired in ConnectorGroup queue");
			job->turn_around_job_answer();
		} else if (!freeHelper.ListEmpty()) {
			queue.Pop();
			process_task(job);
		} else if (fallback && fallback->has_free_helper()) {
			queue.Pop();
			fallback->process_task(job);
		} else {
			break;
		}
	}
}

void ConnectorGroup::job_timer_procedure(void)
{
	log4cplus_debug("enter timer procedure");
	uint64_t v = GET_TIMESTAMP() / 1000;
	attach_timer(retryList);
	flush_task(v);
	log4cplus_debug("leave timer procedure");
}

int ConnectorGroup::accept_new_request_fail(DTCJobOperation *job)
{
	unsigned work_client = helperMax;
	unsigned queue_size = queue.Count();

	/* queue至少排队work_client个任务 */
	if (queue_size <= work_client)
		return 0;

	uint64_t wait_time = queue_size * (uint64_t)average_delay;
	wait_time /= work_client; /* us */
	wait_time /= 1000; /* ms */

	uint64_t now = GET_TIMESTAMP() / 1000;

	if (job->is_expired(now + wait_time))
		return 1;

	return 0;
}

void ConnectorGroup::job_ask_procedure(DTCJobOperation *job)
{
	log4cplus_debug("ConnectorGroup::job_ask_procedure()");

	if (DRequest::ReloadConfig == job->request_code() &&
	    TaskTypeHelperReloadConfig == job->request_type()) {
		group_notify_helper_reload_config(job);
		return;
	}

	uint64_t now = GET_TIMESTAMP() / 1000; /* ms*/
	flush_task(now);

	if (readyHelperCnt == 0) {
		log4cplus_debug(
			"no available helper, up stream server maybe offline");
		job->set_error(
			-EC_UPSTREAM_ERROR, __FUNCTION__,
			"no available helper, up stream server maybe offline");
		job->turn_around_job_answer();
	} else if (job->is_expired(now)) {
		log4cplus_debug("job is expired when sched to ConnectorGroup");
		IncHelperExpireCount();
		//modify error message
		job->set_error(
			-ETIMEDOUT, __FUNCTION__,
			"job is expired when sched to ConnectorGroup,by DTC");
		job->turn_around_job_answer();
	} else if (!freeHelper.ListEmpty()) {
		/* has free helper, sched job */
		process_task(job);
	} else {
		if (fallback && fallback->has_free_helper()) {
			fallback->process_task(job);
		} else if (accept_new_request_fail(job)) {
			/* helper 响应变慢，主动踢掉task */
			log4cplus_debug(
				"ConnectorGroup response is slow, give up current job");
			IncHelperExpireCount();
			job->set_error(
				-EC_SERVER_BUSY, __FUNCTION__,
				"DB response is very slow, give up current job");
			job->turn_around_job_answer();
		} else if (!queue_full()) {
			queue.Push(job);
		} else {
			/* no free helper */
			job->set_error(-EC_SERVER_BUSY, __FUNCTION__,
				       "No available helper connections");
			log4cplus_error(
				"No available helper queue slot,count=%d, max=%d",
				queue.Count(), queueSize);

			job->turn_around_job_answer();
		}
	}
}

void ConnectorGroup::dump_state(void)
{
	log4cplus_info("ConnectorGroup %s count %d/%d", get_name(), helperCount,
		       helperMax);
	int i;
	for (i = 0; i < helperMax; i++) {
		log4cplus_info("helper %d state %s\n", i,
			       helperList[i].helper->state_string());
	}
}

void ConnectorGroup::group_notify_helper_reload_config(DTCJobOperation *job)
{
	//进入到这一步，helper应该是全部处于空闲状态的
	if (!freeHelper.ListEmpty())
		process_task(job);
	else if (fallback && fallback->has_free_helper())
		fallback->process_task(job);
	else {
		log4cplus_error(
			"not have available helper client, please check!");
		job->set_error(-EC_NOT_HAVE_AVAILABLE_HELPERCLIENT,
			       __FUNCTION__,
			       "not have available helper client");
	}
}

void ConnectorGroup::process_reload_config(DTCJobOperation *job)
{
	typedef std::vector<HelperClientList *> HELPERCLIENTLISTVEC;
	typedef std::vector<HelperClientList *>::iterator HELPERCLIENTVIT;
	HELPERCLIENTLISTVEC clientListVec;
	HelperClientList *head = freeHelper.ListOwner();
	for (HelperClientList *pos = head->NextOwner(); pos != head;
	     pos = pos->NextOwner()) {
		clientListVec.push_back(pos);
	}
	for (HELPERCLIENTVIT vit = clientListVec.begin();
	     vit != clientListVec.end(); ++vit) {
		HelperClientList *pHList = (*vit);
		ConnectorClient *pHelper = pHList->helper;
		pHList->ResetList();
		helperCount++;
		pHelper->client_notify_helper_reload_config();
	}

	log4cplus_error(
		"helpergroup [%s] notify work helper reload config finished!",
		get_name());
}
