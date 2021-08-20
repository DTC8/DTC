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
* 
*/
#ifndef __STAT_DTC_DEFINITION__
#define __STAT_DTC_DEFINITION__

#include "stat_info.h"

enum {
	// 相同的统计项目
	S_VERSION = 5,
	C_TIME,

	LOG_COUNT_0 = 10,
	LOG_COUNT_1,
	LOG_COUNT_2,
	LOG_COUNT_3,
	LOG_COUNT_4,
	LOG_COUNT_5,
	LOG_COUNT_6,
	LOG_COUNT_7,

	REQ_USEC_ALL = 20,
	REQ_USEC_GET,
	REQ_USEC_INS,
	REQ_USEC_UPD,
	REQ_USEC_DEL,
	REQ_USEC_FLUSH,
	REQ_USEC_HIT,
	REQ_USEC_REPLACE,

	// accept连接次数
	ACCEPT_COUNT = 30,
	// 当前连接数
	CONN_COUNT,

	CUR_QUEUE_COUNT,
	MAX_QUEUE_COUNT,
	QUEUE_EFF,

	AGENT_ACCEPT_COUNT,
	AGENT_CONN_COUNT,

	// server是否为只读状态
	SERVER_READONLY = 40,
	SERVER_OPENNING_FD,
	SUPER_GROUP_ENABLE,

	// DTC
	DTC_CACHE_SIZE = 1000,
	DTC_CACHE_KEY,
	DTC_CACHE_VERSION,
	DTC_UPDATE_MODE,
	DTC_EMPTY_FILTER,

	DTC_USED_NGS,
	DTC_USED_NODES,
	DTC_DIRTY_NODES,
	DTC_USED_ROWS,
	DTC_DIRTY_ROWS,
	DTC_BUCKET_TOTAL,
	DTC_FREE_BUCKET,
	DTC_DIRTY_AGE,
	DTC_DIRTY_ELDEST,

	DTC_CHUNK_TOTAL,
	DTC_DATA_SIZE,
	DTC_DATA_EFF,

	DTC_DROP_COUNT,
	DTC_DROP_ROWS,
	DTC_FLUSH_COUNT,
	DTC_FLUSH_ROWS,
	DTC_GET_COUNT,
	DTC_GET_HITS,
	DTC_INSERT_COUNT,
	DTC_INSERT_HITS,
	DTC_UPDATE_COUNT,
	DTC_UPDATE_HITS,
	DTC_DELETE_COUNT,
	DTC_DELETE_HITS,
	DTC_PURGE_COUNT,
	DTC_HIT_RATIO,
	DTC_ASYNC_RATIO,

	DTC_SQL_USEC_ALL,
	DTC_SQL_USEC_GET,
	DTC_SQL_USEC_INS,
	DTC_SQL_USEC_UPD,
	DTC_SQL_USEC_DEL,
	DTC_SQL_USEC_FLUSH,

	DTC_FORWARD_USEC_ALL,
	DTC_FORWARD_USEC_GET,
	DTC_FORWARD_USEC_INS,
	DTC_FORWARD_USEC_UPD,
	DTC_FORWARD_USEC_DEL,
	DTC_FORWARD_USEC_FLUSH,

	DTC_FRONT_BARRIER_COUNT,
	DTC_FRONT_BARRIER_MAX_TASK,
	DTC_BACK_BARRIER_COUNT,
	DTC_BACK_BARRIER_MAX_TASK,

	// empty node count
	DTC_EMPTY_NODES,
	DTC_MEMORY_TOP,
	DTC_MAX_FLUSH_REQ,
	DTC_CURR_FLUSH_REQ,
	DTC_OLDEST_DIRTY_TIME,
	LAST_PURGE_NODE_MOD_TIME,
	DATA_EXIST_TIME,
	DATA_SIZE_AVG_RECENT,
	DTC_ASYNC_FLUSH_COUNT,

	DTC_KEY_EXPIRE_USER_COUNT,
	DTC_KEY_EXPIRE_DTC_COUNT,

	BTM_INDEX_1 = 2000,
	BTM_INDEX_2,
	BTM_INDEX_3,
	BTM_DATA,
	BTM_DATA_DELETE,

	// statisitc item for hotbackup
	HBP_LRU_SCAN_TM = 3000,
	HBP_LRU_TOTAL_BITS,
	HBP_LRU_TOTAL_1_BITS,
	HBP_LRU_SET_COUNT,
	HBP_LRU_SET_HIT_COUNT,
	HBP_LRU_CLR_COUNT,
	HBP_INC_SYNC_STEP,

	// statistic item for blacklist
	BLACKLIST_CURRENT_SLOT = 3010,
	BLACKLIST_SIZE,
	TRY_PURGE_COUNT,
	// try_purge_size 每次purge的节点个数
	TRY_PURGE_NODES,

	PLUGIN_REQ_USEC_ALL = 10000,

	// thread cpu statistic
	INC_THREAD_CPU_STAT_0 = 20000,
	INC_THREAD_CPU_STAT_1,
	INC_THREAD_CPU_STAT_2,
	INC_THREAD_CPU_STAT_3,
	INC_THREAD_CPU_STAT_4,
	INC_THREAD_CPU_STAT_5,
	INC_THREAD_CPU_STAT_6,
	INC_THREAD_CPU_STAT_7,
	INC_THREAD_CPU_STAT_8,
	INC_THREAD_CPU_STAT_9,

	CACHE_THREAD_CPU_STAT = 20100,
	DATA_SOURCE_CPU_STAT = 20200,

	DATA_SIZE_HISTORY_STAT = 20300,
	ROW_SIZE_HISTORY_STAT = 20301,
	DATA_SURVIVAL_HOUR_STAT = 20302,
	PURGE_CREATE_UPDATE_STAT = 20303,
	// 新增Helper的统计项, 从20400开始编号
	HELPER_READ_GROUR_CUR_QUEUE_MAX_SIZE = 20400,
	HELPER_WRITE_GROUR_CUR_QUEUE_MAX_SIZE = 20401,
	HELPER_COMMIT_GROUR_CUR_QUEUE_MAX_SIZE = 20402,
	HELPER_SLAVE_READ_GROUR_CUR_QUEUE_MAX_SIZE = 20403,

	// single thread
	WORKER_THREAD_CPU_STAT = 20500,

	// xpire time
	INCOMING_EXPIRE_REQ = 30000,
	CACHE_EXPIRE_REQ = 30001,
	DATA_SOURCE_EXPIRE_REQ = 30002,

	// job expire time set by client
	TASK_CLIENT_TIMEOUT = 30100,
};

extern DTCStatDefinition g_stat_definition[];
#endif
