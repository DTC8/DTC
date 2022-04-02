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
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <alloca.h>
#include <stdlib.h>

#include "log/log.h"
#include "connector_client.h"
#include "connector/connector_group.h"
#include "socket/unix_socket.h"
#include "table/table_def_manager.h"

ConnectorClient::ConnectorClient(EpollOperation *o, ConnectorGroup *hg, int idx)
    : EpollBase(o)
{
    packet = NULL;
    job = NULL;

    helperGroup = hg;
    helperIdx = idx;

    supportBatchKey = 0;
    connectErrorCnt = 0;
    ready = 0;
    Ready(); // 开始默认可用
}

ConnectorClient::~ConnectorClient()
{
    if ((0 != job)) {
        if (stage == HelperRecvVerifyState) {
            DELETE(job);
        } else if (stage != HelperRecvRepState) {
            queue_back_task();
        } else {
            if (job->result_code() >= 0)
                set_error(-EC_UPSTREAM_ERROR, __FUNCTION__,
                      "Server Shutdown");
            job->turn_around_job_answer();
            job = NULL;
        }
    }

    DELETE(packet);
}

int ConnectorClient::Ready()
{
    if (ready == 0) {
        helperGroup->add_ready_helper();
    }

    ready = 1;
    connectErrorCnt = 0;

    return 0;
}

int ConnectorClient::connect_error()
{
    connectErrorCnt++;
    if (connectErrorCnt > maxTryConnect && ready) {
        log4cplus_debug(
            "helper-client[%d] try connect %lu times, switch invalid.",
            helperIdx, (unsigned long)connectErrorCnt);
        helperGroup->dec_ready_helper();
        ready = 0;
    }

    return 0;
}

int ConnectorClient::attach_task(DTCJobOperation *p, Packet *s)
{
    log4cplus_debug("ConnectorClient::attach_task()");

    job = p;
    packet = s;

    int ret = packet->Send(netfd);
    if (ret == SendResultDone) {
        DELETE(packet);
        stopWatch.start();
        job->prepare_decode_reply();
        receiver.attach(netfd);
        receiver.erase();

        stage = HelperRecvRepState;
        enable_input();
    } else {
        stage = HelperSendReqState;
        enable_output();
    }

    attach_timer(helperGroup->recvList);
    return delay_apply_events();
}

void ConnectorClient::complete_task(void)
{
    DELETE(packet);
    if (job != NULL) {
        job->turn_around_job_answer();
        job = NULL;
    }
}

void ConnectorClient::queue_back_task(void)
{
    DELETE(packet);
    helperGroup->queue_back_task(job);
    job = NULL;
}

int ConnectorClient::Reset()
{
    if (stage == HelperSendVerifyState || stage == HelperRecvVerifyState) {
        DELETE(packet);
        DELETE(job);
    } else {
        if (job != NULL && job->result_code() >= 0) {
            if (stage == HelperRecvRepState)
                set_error(-EC_UPSTREAM_ERROR,
                      "ConnectorGroup::Reset",
                      "helper recv error");
            else if (stage == HelperSendReqState)
                set_error(-EC_SERVER_ERROR,
                      "ConnectorGroup::Reset",
                      "helper send error");
        }
        complete_task();
    }

    if (stage == HelperIdleState)
        helperGroup->connection_reset(this);

    disable_input();
    disable_output();
    EpollBase::detach_poller();
    if (netfd > 0)
        close(netfd);
    netfd = -1;
    stage = HelperDisconnected;
    attach_timer(helperGroup->retryList);
    return 0;
}

int ConnectorClient::connect_server(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return -1;

    if (is_unix_socket_path(path)) {
        if ((netfd = socket(AF_LOCAL, SOCK_STREAM, 0)) == -1) {
            log4cplus_error("%s", "socket error,%m");
            return -2;
        }

        fcntl(netfd, F_SETFL, O_RDWR | O_NONBLOCK);

        struct sockaddr_un unaddr;
        socklen_t addrlen;
        addrlen = init_unix_socket_address(&unaddr, path);
        return connect(netfd, (struct sockaddr *)&unaddr, addrlen);
    } else {
        const char *addr = NULL;
        const char *port = NULL;
        const char *begin = strchr(path, ':');
        if (begin) {
            char *p = (char *)alloca(begin - path + 1);
            memcpy(p, path, begin - path);
            p[begin - path] = '\0';
            addr = p;
        } else {
            log4cplus_error(
                "address error,correct address is addr:port/protocol");
            return -5;
        }

        const char *end = strchr(path, '/');
        if (begin && end) {
            char *p = (char *)alloca(end - begin);
            memcpy(p, begin + 1, end - begin - 1);
            p[end - begin - 1] = '\0';
            port = p;
        } else {
            log4cplus_error(
                "protocol error,correct address is addr:port/protocol");
            return -6;
        }

        struct sockaddr_in inaddr;
        bzero(&inaddr, sizeof(struct sockaddr_in));
        inaddr.sin_family = AF_INET;
        inaddr.sin_port = htons(atoi(port));

        if (strcmp(addr, "*") != 0 &&
            inet_pton(AF_INET, addr, &inaddr.sin_addr) <= 0) {
            log4cplus_error("invalid address %s:%s", addr, port);
            return -3;
        }

        if (strcasestr(path, "tcp"))
            netfd = socket(AF_INET, SOCK_STREAM, 0);
        else
            netfd = socket(AF_INET, SOCK_DGRAM, 0);

        if (netfd < 0) {
            log4cplus_error("%s", "socket error,%m");
            return -4;
        }

        fcntl(netfd, F_SETFL, O_RDWR | O_NONBLOCK);

        return connect(netfd, (const struct sockaddr *)&inaddr,
                   sizeof(inaddr));
    }
    return 0;
}

int ConnectorClient::reconnect(void)
{
    // increase connect count
    connect_error();

    if (stage != HelperDisconnected)
        Reset();

    const char *sockpath = helperGroup->sock_path();
    if (connect_server(sockpath) == 0) {
        log4cplus_debug("Connected to helper[%d]: %s", helperIdx,
                sockpath);

        packet = new Packet;
        packet->encode_detect(
            TableDefinitionManager::instance()->get_cur_table_def());

        if (attach_poller() != 0) {
            log4cplus_error("helper[%d] attach poller error",
                    helperIdx);
            return -1;
        }
        disable_input();
        enable_output();
        stage = HelperSendVerifyState;
        return send_verify();
    }

    if (errno != EINPROGRESS) {
        log4cplus_error("connect helper-%s error: %m", sockpath);
        close(netfd);
        netfd = -1;
        attach_timer(helperGroup->retryList);
        //check helpergroup job queue expire.
        helperGroup->check_queue_expire();
        return 0;
    }

    log4cplus_debug("Connectting to helper[%d]: %s", helperIdx, sockpath);

    disable_input();
    enable_output();
    attach_timer(helperGroup->connList);
    stage = HelperConnecting;
    return attach_poller();
}

int ConnectorClient::send_verify()
{
    int ret = packet->Send(netfd);
    if (ret == SendResultDone) {
        DELETE(packet);

        job = new DTCJobOperation(
            TableDefinitionManager::instance()->get_cur_table_def());
        if (job == NULL) {
            log4cplus_error("%s: %m", "new job & packet error");
            return -1;
        }
        job->prepare_decode_reply();
        receiver.attach(netfd);
        receiver.erase();

        stage = HelperRecvVerifyState;
        disable_output();
        enable_input();
    } else {
        stage = HelperSendVerifyState;
        enable_output();
    }

    attach_timer(helperGroup->recvList);
    return delay_apply_events();
}

int ConnectorClient::recv_verify()
{
    static int logwarn;
    int ret = job->do_decode(receiver);

    supportBatchKey = 0;
    switch (ret) {
    default:
    case DecodeFatalError:
        log4cplus_error(
            "decode fatal error retcode[%d] msg[%m] from helper",
            ret);
        goto ERROR_RETURN;

    case DecodeDataError:
        log4cplus_error("decode data error from helper %d",
                job->result_code());
        goto ERROR_RETURN;

    case DecodeWaitData:
    case DecodeIdle:
        attach_timer(helperGroup->recvList);
        return 0;

    case DecodeDone:
        switch (job->result_code()) {
        case -EC_EXTRA_SECTION_DATA:
            supportBatchKey = 1;
            break;
        case -EC_BAD_FIELD_NAME: // old version dtc
            supportBatchKey = 0;
            break;
        default:
            log4cplus_error("detect helper-%s error: %d, %s",
                    helperGroup->sock_path(),
                    job->result_code(),
                    job->resultInfo.error_message());
            goto ERROR_RETURN;
        }
        break;
    }

    if (supportBatchKey) {
        log4cplus_debug("helper-%s support batch-key",
                helperGroup->sock_path());
    } else {
        if (logwarn++ == 0)
            log4cplus_warning("helper-%s unsupported batch-key",
                      helperGroup->sock_path());
        else
            log4cplus_debug("helper-%s unsupported batch-key",
                    helperGroup->sock_path());
    }

    DELETE(job);
    Ready();

    enable_input();
    disable_output();
    stage = HelperIdleState;
    helperGroup->request_completed(this);
    disable_timer();
    return delay_apply_events();

ERROR_RETURN:
    Reset();
    attach_timer(helperGroup->retryList);
    //check helpergroup job queue expire.
    helperGroup->check_queue_expire();
    return 0;
}

//client peer
int ConnectorClient::recv_response()
{
    int ret = job->do_decode(receiver);

    switch (ret) {
    default:
    case DecodeFatalError:
        log4cplus_info(
            "decode fatal error retcode[%d] msg[%m] from helper",
            ret);
        job->set_error(-EC_UPSTREAM_ERROR, __FUNCTION__,
                   "decode fatal error from helper");
        break;

    case DecodeDataError:
        log4cplus_info("decode data error from helper %d",
                   job->result_code());
        job->set_error(-EC_UPSTREAM_ERROR, __FUNCTION__,
                   "decode data error from helper");
        break;

    case DecodeWaitData:
    case DecodeIdle:
        attach_timer(helperGroup->recvList);
        return 0;

    case DecodeDone:
        {   
            log4cplus_info("response is done");
            #if 1
            // 查热数据库，并将结果写入binlog，便于对账
            if (job->request_code() != DRequest::Get ) {
            DTCJobOperation* o_query_job = new DTCJobOperation(TableDefinitionManager::instance()->get_cur_table_def());
            o_query_job->set_request_key(job->request_key());
            o_query_job->build_packed_key();
            #if 1
                log4cplus_info("key val01:%d" , o_query_job->request_key()->s64);
                log4cplus_info("key val02:%d" , o_query_job->request_key()->u64);
            #endif
            // o_query_job.mr.m_sql = job->mr.m_sql; // sql is deep copy
            // for temp test
            o_query_job->mr.m_sql = "insert into table_hwc values(17, 'kaka' , 'shenzheng' ,112 ,112)";
            log4cplus_info("cyj:%d" , __LINE__);
                if (helperGroup->DirectQueryHotServer(o_query_job) != 0) {
                    reconnect();
                    break;
                }
            log4cplus_info("cyj:%d" , __LINE__);
                if (helperGroup->WriteHBLog(o_query_job , 1) != 0) {
                    reconnect();
                    break;
                }
            }
            #else
            // 正常写请求返回成功，写binlog
            if (job->request_code() != DRequest::Get 
                && (job->result_code() >= 0)) {
                // for temp test
                job->mr.m_sql = "insert into table_hwc values(7, 'lulu' , 'suzhou' ,111 ,111)";
                helperGroup->WriteHBLog(job);
            }
            #endif
        }
        break;
    }

    stopWatch.stop();
    helperGroup->record_process_time(job->request_code(), stopWatch);
    complete_task();
    helperGroup->request_completed(this);

    // ??
    enable_input();
    stage = HelperIdleState;
    if (ret != DecodeDone)
        return -1;
    return 0;
}

int ConnectorClient::send_request()
{
    int ret = packet->Send(netfd);

    log4cplus_debug(
        "[ConnectorClient][job=%d]Send Request result=%d, fd=%d",
        job->Role(), ret, netfd);

    switch (ret) {
    case SendResultMoreData:
        break;

    case SendResultDone:
        DELETE(packet);
        stopWatch.start();
        job->prepare_decode_reply();
        receiver.attach(netfd);
        receiver.erase();

        stage = HelperRecvRepState;
        disable_output();
        enable_input();
        break;

    case SendResultError:
    default:
        log4cplus_info("send result error, ret = %d msg = %m", ret);
        job->set_error(-EC_SERVER_ERROR, "Data source send failed",
                   NULL);
        return -1;
    }

    attach_timer(helperGroup->recvList);
    return 0;
}

void ConnectorClient::input_notify(void)
{
    log4cplus_debug("enter input_notify.");
    disable_timer();

    if (stage == HelperRecvVerifyState) {
        if (recv_verify() < 0)
            reconnect();
        return;
    } else if (stage == HelperRecvNotifyReloadConfigState) {
        if (recv_notify_helper_reload_config() < 0)
            reconnect();
        return;
    } else if (stage == HelperRecvRepState) {
        if (recv_response() < 0) {
            reconnect();
        }
        return;
    } else if (stage == HelperIdleState) {
        /* no data from peer allowed in idle state */
        Reset();
        return;
    }
    disable_input();
    log4cplus_debug("leave input_notify.");
}

void ConnectorClient::output_notify(void)
{
    log4cplus_debug("enter output_notify.");
    disable_timer();
    if (stage == HelperSendVerifyState) {
        if (send_verify() < 0) {
            DELETE(packet);
            reconnect();
        }
        return;
    } else if (stage == HelperSendNotifyReloadConfigState) {
        if (send_notify_helper_reload_config() < 0) {
            DELETE(packet);
            reconnect();
        }
        return;
    } else if (stage == HelperSendReqState) {
        if (send_request() < 0) {
            queue_back_task();
            reconnect();
        }
        return;
    } else if (stage == HelperConnecting) {
        packet = new Packet;
        packet->encode_detect(
            TableDefinitionManager::instance()->get_cur_table_def());

        disable_input();
        enable_output();
        stage = HelperSendVerifyState;
        send_verify();
        return;
    }
    disable_output();
    log4cplus_debug("leave output_notify.");
}

void ConnectorClient::hangup_notify(void)
{
    log4cplus_debug("enter hangup_notify.");
    Reset();
    log4cplus_debug("leave hangup_notify.");
}

void ConnectorClient::job_timer_procedure(void)
{
    log4cplus_debug("enter timer procedure");
    switch (stage) {
    case HelperRecvRepState:
        stopWatch.stop();
        helperGroup->record_process_time(job->request_code(),
                         stopWatch);
        // 查热数据库，并将结果写入binlog，便于对账
        if (job->request_code() != DRequest::Get ) {
            DTCJobOperation o_query_job(TableDefinitionManager::instance()->get_cur_table_def());
            o_query_job.set_request_key(job->request_key());
            o_query_job.mr.m_sql = job->mr.m_sql; // sql is deep copy
            if (helperGroup->DirectQueryHotServer(&o_query_job) != 0) {
                reconnect();
                break;
            }

            if (helperGroup->WriteHBLog(&o_query_job , 1) != 0) {
                reconnect();
                break;
            }
        }
        log4cplus_error("helper index[%d] do_execute timeout.", helperIdx);
        set_error(-EC_UPSTREAM_ERROR, "ConnectorGroup::Timeout",
              "helper do_execute timeout");
        reconnect();
        break;
    case HelperSendReqState:

        log4cplus_error("helper index[%d] send timeout.", helperIdx);
        set_error(-EC_SERVER_ERROR, "ConnectorGroup::Timeout",
              "helper send timeout");
        reconnect();
        break;
    case HelperDisconnected:
        reconnect();
        break;
    case HelperConnecting:
        Reset();
        break;
    case HelperSendVerifyState:
    case HelperRecvVerifyState:
        DELETE(packet);
        DELETE(job);
        reconnect();
        break;
    case HelperSendNotifyReloadConfigState:
    case HelperRecvNotifyReloadConfigState:
        DELETE(packet);
        DELETE(job);
        reconnect();
        break;
    default:
        break;
    }
    log4cplus_debug("leave timer procedure");
}

int ConnectorClient::client_notify_helper_reload_config()
{
    packet = new Packet;
    packet->encode_reload_config(
        TableDefinitionManager::instance()->get_cur_table_def());
    if (0 != attach_poller()) {
        log4cplus_error(
            "notify reload config helper [%d] attach poller failed!",
            helperIdx);
        DELETE(packet);
        return -1;
    }
    disable_input();
    enable_output();
    stage = HelperSendNotifyReloadConfigState;
    return send_notify_helper_reload_config();
}

int ConnectorClient::send_notify_helper_reload_config()
{
    int ret = packet->Send(netfd);
    if (SendResultDone == ret) {
        DELETE(packet);
        job = new DTCJobOperation(
            TableDefinitionManager::instance()->get_cur_table_def());
        if (NULL == job) {
            log4cplus_error(
                "new job error, maybe not have enough memory!");
            return -1;
        }
        job->prepare_decode_reply();
        receiver.attach(netfd);
        receiver.erase();

        stage = HelperRecvNotifyReloadConfigState;
        disable_output();
        enable_input();
    } else {
        stage = HelperSendNotifyReloadConfigState;
        enable_output();
    }

    attach_timer(helperGroup->recvList);
    return delay_apply_events();
}

int ConnectorClient::recv_notify_helper_reload_config()
{
    int ret = job->do_decode(receiver);
    switch (ret) {
    default:
    case DecodeFatalError: {
        log4cplus_error("decode fatal error retcode [%d] from helper",
                ret);
        goto ERROR_RETURN;
    }
    case DecodeDataError: {
        log4cplus_error("decode data error retcode [%d] from helper",
                ret);
        goto ERROR_RETURN;
    }
    case DecodeWaitData:
    case DecodeIdle: {
        attach_timer(helperGroup->recvList);
        return 0;
    }
    case DecodeDone: {
        switch (job->result_code()) {
        case 0:
            break;
        case -EC_RELOAD_CONFIG_FAILED: {
            log4cplus_error(
                "reload config failed EC_RELOAD_CONFIG_FAILED resultcode [%d] from helper",
                job->result_code());
            goto ERROR_RETURN;
        }
        default: {
            log4cplus_error(
                "reload config failed unknow resultcode [%d] from helper",
                job->result_code());
            goto ERROR_RETURN;
        }
        }
    }
    }
    DELETE(job);

    enable_input();
    disable_output();
    stage = HelperIdleState;
    helperGroup->request_completed(this);
    disable_timer();
    return delay_apply_events();

ERROR_RETURN:
    Reset();
    attach_timer(helperGroup->retryList);
    //check helpergroup job queue expire.
    helperGroup->check_queue_expire();
    return 0;
}
