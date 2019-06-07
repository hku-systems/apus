#ifndef MESSAGE_H
#define MESSAGE_H
#include <sys/queue.h>

struct tailq_cmd_t {
    uint16_t    len;
    uint8_t cmd[87380];
};
typedef struct tailq_cmd_t tailq_cmd_t;

struct tailq_entry_t {
	uint8_t type;
	uint16_t connection_id;
	uint64_t req_id;
	tailq_cmd_t cmd;
	TAILQ_ENTRY(tailq_entry_t) entries;
};
typedef struct tailq_entry_t tailq_entry_t;

TAILQ_HEAD(, tailq_entry_t) tailhead;

pthread_spinlock_t tailq_lock;

#endif