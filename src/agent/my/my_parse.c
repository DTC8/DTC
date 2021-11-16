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
#include <math.h>
#include <inttypes.h>
#include "da_protocal.h"
#include "../da_msg.h"
#include "../da_conn.h"
#include "../da_request.h"
#include "../da_buf.h"
#include "../da_util.h"
#include "../da_errno.h"
#include "../da_time.h"
#include "../da_core.h"
#include "my_comm.h"
#include "my_command.h"

#define MY_HEADER_SIZE 4
#define MAXPACKETSIZE	(64<<20)
#define MultiKeyValue 32
#define __FLTFMT__	"%LA"
#define CONVERT_NULL_TO_EMPTY_STRING 1

enum fieldtype {
	None = 0,
	Signed = 1,
	Unsigned = 2,
	Float = 3,
	String = 4,
	Binary = 5,
	TotalType,
};

enum codefield {
	FD_HEADER = 0,
	FD_VERSION = 1,
	FD_TABLEDEFINE = 2,
	FD_REQUEST = 3,
	FD_RESULTINFO = 4,
	FD_UPDATEINFO = 5,
	FD_CONDITIONINFO = 6,
	FD_FIELDSET = 7,
	FD_RESULTSET = 8,
	FD_TOTALFIELD = 9,
};

enum codestate {
	ST_ID = 0, ST_LENTH = 1, ST_VALUE = 2,
};

char dtc_key[50] = {0};
int dtc_key_type = 0;

/*
 * parse request msg
 */
void my_parse_req(struct msg *r) {
	struct mbuf *b;
	uint8_t *p;
	uint32_t input_packet_length = 0;
	enum enum_server_command command;
	int rc;

	log_debug("my_parse_req entry.");

	b = STAILQ_LAST(&r->buf_q, mbuf, next);
	p = r->pos;

	if (p < b->last) 
	{
		if (b->last - p < MY_HEADER_SIZE) {
			log_error("receive size small than package header. id:%d", r->id);
			goto error;
		}
		
		input_packet_length = uint3korr(p);
		p += 3;
		r->pkt_nr = (uint8_t)(*p);	// mysql sequence id
		p++;
		log_debug("pkt_nr:%d, packet len:%d", r->pkt_nr, input_packet_length);

		if(p + input_packet_length > b->last)
			goto error;

		if(r->owner->stage == CONN_STAGE_LOGGED_IN)
		{
			r->keytype = dtc_key_type;

			rc = my_get_command(p, input_packet_length, r, &command);
			if(rc)
			{
				if (rc < 0) 
				{
					log_error("parse command error:%d\n", rc);
					goto error;
				}

				r->command = command;
				r->keyCount = 1;
				r->cmd = MSG_REQ_SVRADMIN;
			}
		}

		p += input_packet_length;
		r->pos = p;
		
		goto success;
	}

error:
	r->pos = b->last;
	r->parse_res = MSG_PARSE_ERROR;
	log_debug("parse msg:%"PRIu64" error!", r->id);
	errno = EINVAL;
	log_debug("my_parse_req leave.");
	return;

success:
	r->parse_res = MSG_PARSE_OK;
	log_debug("parse msg:%"PRIu64" success!", r->id);
	log_debug("my_parse_req leave.");
	return;
}

void my_parse_rsp(struct msg *r) {
	struct mbuf *b;
	uint8_t *p;
	int ret;

	log_debug("my_parse_rsp entry.");

	b = STAILQ_LAST(&r->buf_q, mbuf, next);
	p = r->pos;
	r->token = NULL;

	if (p < b->last) 
	{
		if (b->last - p < sizeof(struct DTC_HEADER) + MY_HEADER_SIZE) {
			log_error("receive size small than package header. id:%d", r->id);
			p = b->last;
			r->parse_res = MSG_PARSE_ERROR;
			errno = EINVAL;
			return ;
		}
		r->peerid = ((struct DTC_HEADER*)p)->id;
		p = p + sizeof(struct DTC_HEADER);
		
		r->pkt_nr = (uint8_t)(p[3]);	// mysql sequence id
		log_debug("pkt_nr:%d, peerid:%d", r->pkt_nr, r->peerid);
		p = p + MY_HEADER_SIZE;

		p = b->last;
		r->pos = p;
		r->parse_res = MSG_PARSE_OK;
		log_debug("parse msg:%"PRIu64" success!", r->id);
	}
	else
	{
		r->parse_res = MSG_PARSE_ERROR;
		log_debug("parse msg:%"PRIu64" error!", r->id);
		errno = EINVAL;
	}

	log_debug("my_parse_rsp leave.");
	return;
}

int my_get_command(uint8_t *input_raw_packet, uint32_t input_packet_length, struct msg* r, enum enum_server_command* cmd)
{
	*cmd = (enum enum_server_command)(uchar)input_raw_packet[0];

	if (*cmd >= COM_END) *cmd = COM_END;  // Wrong command

	if(parse_packet(input_raw_packet, input_packet_length, r, *cmd))
		return 1;

	return -1;
}

int my_do_command(struct msg *msg)
{
	int rc = NEXT_RSP_ERROR;

	switch (msg->command) {
    case COM_INIT_DB:
    case COM_REGISTER_SLAVE: 
    case COM_RESET_CONNECTION:
    case COM_CLONE: 
    case COM_CHANGE_USER: {
      rc = NEXT_RSP_OK;
      break;
    }
    case COM_STMT_EXECUTE: {
      rc = NEXT_FORWARD;
      break;
    }
    case COM_STMT_FETCH: {
      rc = NEXT_FORWARD;
      break;
    }
    case COM_STMT_SEND_LONG_DATA: {
      rc = NEXT_RSP_ERROR;
      break;
    }
    case COM_STMT_PREPARE: 
    case COM_STMT_CLOSE: 
    case COM_STMT_RESET: {
      rc = NEXT_RSP_ERROR;
      break;
    }
    case COM_QUERY: {
      rc = NEXT_FORWARD;
      break;
    }
    case COM_FIELD_LIST:  // This isn't actually needed
    case COM_QUIT:
    case COM_BINLOG_DUMP_GTID:
    case COM_BINLOG_DUMP:
    case COM_REFRESH: 
    case COM_STATISTICS: 
    case COM_PING:
    case COM_PROCESS_INFO:
    case COM_PROCESS_KILL:
    case COM_SET_OPTION:
    case COM_DEBUG:
      rc = NEXT_RSP_OK;
      break;
    case COM_SLEEP:
    case COM_CONNECT:         // Impossible here
    case COM_TIME:            // Impossible from client
    case COM_DELAYED_INSERT:  // INSERT DELAYED has been removed.
    case COM_END:
    default:
      log_error("error command:%d", msg->command);
	  rc = NEXT_RSP_ERROR;
      break;
  }

	return rc;
}

static int dtc_decode_value(enum fieldtype type, int lenth, uint8_t *p,
		CValue *val) {
	uint8_t *q;
	switch (type) {
	case None:
		break;
	case Signed:
	case Unsigned:
		if (lenth == 0 || lenth > 8) {
			goto decode_value_error;
		}
		q = (uint8_t *) p + 1;
		int64_t s64;
		s64 = *(int8_t *) p;
		switch (lenth) {
		case 8:
			s64 = (s64 << 8) | *q++;
		case 7:
			s64 = (s64 << 8) | *q++;
		case 6:
			s64 = (s64 << 8) | *q++;
		case 5:
			s64 = (s64 << 8) | *q++;
		case 4:
			s64 = (s64 << 8) | *q++;
		case 3:
			s64 = (s64 << 8) | *q++;
		case 2:
			s64 = (s64 << 8) | *q++;
		}
		val->s64 = s64;
		break;
	case Float:
		if (lenth < 3)
			goto decode_value_error;
		if (p[lenth - 1] != '\0')
			goto decode_value_error;
		if (!strcmp((char *) p, "NAN"))
			val->flt = NAN;
		else if (!strcmp((char *) p, "INF"))
			val->flt = INFINITY;
		else if (!strcmp((char *) p, "-INF"))
			val->flt = -INFINITY;
		else {
			long double ldf;
			if (sscanf((char *) p, __FLTFMT__, &ldf) != 1)
				goto decode_value_error;
			val->flt = ldf;
		}
		break;
	case String:
	case Binary:
		if (lenth == 0) {
#if CONVERT_NULL_TO_EMPTY_STRING
			val->str.data = p;
			val->str.len = 0;
#else
			val->str.data=NULL;
			val->str.len=0;
#endif
		} else {
			if (p[lenth - 1] != '\0')
				goto decode_value_error;
			val->str.data = p;
			val->str.len = lenth - 1;
		}

		break;
	default:
		goto decode_value_error;
	}
	return 0;
	decode_value_error: return -1;
}

static uint64_t randomHashSeed = 1;

int my_fragment(struct msg *r, uint32_t ncontinuum, struct msg_tqh *frag_msgq) {
	int status,i;
	struct keypos *temp_kpos;
	CValue val;
	log_debug("key count:%d, cmd:%d", r->keyCount, r->cmd);

	if(r->cmd == MSG_NOP)
	{
		uint64_t randomkey=randomHashSeed++;
		r->idx = msg_backend_idx(r, (uint8_t *)&randomkey,sizeof(uint64_t));
		return 0;
	}
	else
	{
		if(r->keyCount == 0)
		{
			log_error(" request msg id: %"PRIu64" request without key",r->id);
			r->err= MSG_NOKEY_ERR;
			r->error = 1;
			return -1;
		}
		else if(r->keyCount == 1)
		{
			temp_kpos = &r->keys[0];
			switch (r->keytype)
			{
				case Signed:
				case Unsigned:
				{
					status=dtc_decode_value(Unsigned,temp_kpos->end - temp_kpos->start, temp_kpos->start,&val);
					if(status < 0)
					{
						log_error("decode value:%d", status);
						return -1;
					}
					log_debug("val.u64:%d", val.u64);
					r->idx = msg_backend_idx(r,(uint8_t *)&val.u64, sizeof(uint64_t));
					log_debug("r->idx:%d", r->idx);
					break;
				}
				case String:
				{
					int len = temp_kpos->end - temp_kpos->start;
					char temp[len+1];
					*temp = len;
					for(i=1; i<len+1; i++)
					{
						temp[i] = lower((char)(temp_kpos->start)[i-1]);
					}
					r->idx = msg_backend_idx(r, (uint8_t *)temp,len+1);
					log_debug("debug,len :%d the packet key is %u   '%s' the hash key(r->idx): %d ",len,*temp,temp+1,r->idx);
					break;
				}
				case Binary:
				{
					int len = temp_kpos->end - temp_kpos->start;
					char temp[len+1];
					*temp = len;
					memcpy(temp+1, temp_kpos->start,len);
					r->idx = msg_backend_idx(r, (uint8_t *)temp,len+1);
					log_debug("debug,len :%d the packet key is %u   '%s' the hash key(r->idx): %d ",len,*temp,temp+1,r->idx);
					break;
				}
			}

			log_debug("return status:%d", status);
			return status;
		}
		else
		{
			if(r->cmd == MSG_REQ_GET) {
				//status = dtc_fragment_get(r, ncontinuum, frag_msgq);
				//GET is not supported
				status = -1;
			}
			else if(r->cmd == MSG_REQ_UPDATE) {
				//MSET is not supported
				status = -1;
			}
			else if(r->cmd == MSG_REQ_DELETE) {
				//MDEL is not supported
				status = -1;
			} else {
				//other multi operation is not supported
				status = -1;
			}
			return status;
		}
	}
}

int my_get_key_value(uint8_t* sql, int sql_len, int* start_offset, int* end_offset)
{	
	int i = 0;
	int dtc_key_len = strlen(dtc_key);
	for(i = 0; i < sql_len; i++)
	{
		if(sql_len - i >= dtc_key_len)
		{
			if(strncmp(sql+i, dtc_key, dtc_key_len) == 0)
			{
				int j;
				for(j = i + dtc_key_len; j < sql_len; j++)
				{
					if(sql[j] == '=')
					{
						j++;
						//strip space.
						while(j < sql_len && sql[j] == ' ')
						{
							j++;
						}

						if(j < sql_len)
						{
							*start_offset = j;

							int k = 0;
							for(k = j; k < sql_len; k++)
							{
								if(sql[k+1] == ' ' || sql[k+1] == ';' || k + 1 == sql_len)
								{
									*end_offset = k+1;
									return 0;
								}
							}

							return -4;
						}
						else
							return -1;
					}
				}

				return -2;
			}
		}
	}

	return -3;
}