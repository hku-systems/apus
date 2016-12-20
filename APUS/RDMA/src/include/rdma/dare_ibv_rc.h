#ifndef DARE_IBV_RC_H
#define DARE_IBV_RC_H

#include <infiniband/verbs.h> /* OFED stuff */
#include "dare_ibv.h"

/*#define Q_DEPTH 8192
#define S_DEPTH 4096
#define S_DEPTH_ 4095*/

/*#define Q_DEPTH 1024
#define S_DEPTH 512
#define S_DEPTH_ 511*/

/*#define Q_DEPTH 512
#define S_DEPTH 216
#define S_DEPTH_ 215

#define Q_DEPTH 216
#define S_DEPTH 108
#define S_DEPTH_ 107*/

#define Q_DEPTH 128
#define S_DEPTH 64
#define S_DEPTH_ 63

struct cm_con_data_t {
	rem_mem_t log_mr;
	uint32_t idx;
	uint16_t lid;
	uint8_t gid[16];
	uint32_t qpns;
}__attribute__ ((packed));

int rc_init();
void rc_free();

int post_send( uint8_t server_id, void *buf, uint32_t len, struct ibv_mr *mr, enum ibv_wr_opcode opcode, rem_mem_t *rm, int signaled, int poll_completion);
int rc_disconnect_server();

#endif /* DARE_IBV_RC_H */
