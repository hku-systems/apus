#ifndef RSM_INTERFACE_H
#define RSM_INTERFACE_H
#include <unistd.h>
#include <stdint.h>

#include "./output/output.h"

struct event_manager_t;

#ifdef __cplusplus
extern "C" {
#endif
	
	struct event_manager_t* mgr_init(uint8_t node_id,const char* config_path,const char* log_path);
	void server_side_on_read(struct event_manager_t* ev_mgr,void *buf,size_t ret,int fd);
	void mgr_on_accept(int fd, struct event_manager_t* ev_mgr);
	void mgr_on_check(int fd, const void* buf, size_t ret, struct event_manager_t* ev_mgr);
	void mgr_on_close(int fd, struct event_manager_t* ev_mgr);
	int mgr_on_process_init(struct event_manager_t* ev_mgr);
	void mgr_on_recvfrom(struct event_manager_t* ev_mgr, void* buf, ssize_t ret, struct sockaddr* src_addr);
#ifdef __cplusplus
}
#endif

#endif
