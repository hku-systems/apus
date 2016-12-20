/*
Date: 25 March, 2016
Author: Jingyu
Description: A demo that shows how to use librdmacm to communicate
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <byteswap.h>
#include <getopt.h>

#include <rdma/rdma_cma.h>

#define HOST_IP "10.22.1.1"
#define HOST_PORT_STR "7174"
const int message_size = 100;

// structures for cmatest
struct cmatest_node {
	int			id;
	struct rdma_cm_id	*cma_id;
	int			connected;
	struct ibv_pd		*pd;
	struct ibv_cq		*cq;
	struct ibv_mr		*mr;
	struct ibv_ah		*ah;
	uint32_t		remote_qpn;
	uint32_t		remote_qkey;
	void			*mem;
};

struct cmatest {
	struct rdma_event_channel *channel;
	struct cmatest_node	*node_ptr;
	int			conn_index;
	int			connects_left;

	struct rdma_addrinfo	*rai;
};

static struct cmatest test;
static struct rdma_addrinfo hints;

int alloc_nodes(void){
	test.node_ptr = malloc(sizeof *test.node_ptr);
	memset(test.node_ptr,0,sizeof *test.node_ptr);
	test.node_ptr->id = 1;	
}
int verify_test_params(struct cmatest_node *node){
	struct ibv_port_attr port_attr;
	int ret;
	ret = ibv_query_port(node->cma_id->verbs, node->cma_id->port_num,
			     &port_attr);

}
int create_message(struct cmatest_node *node)
{
	node->mem = malloc(message_size + sizeof(struct ibv_grh));
	sprintf(node->mem,"Hello World");
	printf("mem addr: %p\n",node->mem);
	if (!node->mem) {
		printf("failed message allocation\n");
		return -1;

	}
	int mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
	
	node->mr = ibv_reg_mr(node->pd, node->mem,
			      message_size + sizeof(struct ibv_grh),
			      mr_flags);
	if (!node->mr) {
		printf("failed to reg MR\n");
		goto err;
	}
	return 0;
err:
	free(node->mem);
	return -1;
}

int init_node(struct cmatest_node *node)
{
	struct ibv_qp_init_attr init_qp_attr;
	int cqe, ret;

	node->pd = ibv_alloc_pd(node->cma_id->verbs);
	if (!node->pd) {
		ret = -ENOMEM;
		printf("udaddy: unable to allocate PD\n");
		goto out;
	}
	cqe = 2;
	node->cq = ibv_create_cq(node->cma_id->verbs, cqe, node, 0, 0);
	if (!node->cq) {
		ret = -ENOMEM;
		printf("udaddy: unable to create CQ\n");
		goto out;
	}

	memset(&init_qp_attr, 0, sizeof init_qp_attr);
	init_qp_attr.cap.max_send_wr =  1;
	init_qp_attr.cap.max_recv_wr =  1;
	init_qp_attr.cap.max_send_sge = 1;
	init_qp_attr.cap.max_recv_sge = 1;
	init_qp_attr.qp_context = node;
	init_qp_attr.sq_sig_all = 0;
	init_qp_attr.qp_type = IBV_QPT_UD;
	init_qp_attr.send_cq = node->cq;
	init_qp_attr.recv_cq = node->cq;
	ret = rdma_create_qp(node->cma_id, node->pd, &init_qp_attr);
	if (ret) {
		perror("udaddy: unable to create QP");
		goto out;
	}

	ret = create_message(node);
	if (ret) {
		printf("udaddy: failed to create messages: %d\n", ret);
		goto out;
	}
out:
	return ret;
}

int post_recvs(struct cmatest_node *node){
	struct ibv_recv_wr recv_wr, *recv_failure;
	struct ibv_sge sge;
	int i, ret = 0;

	recv_wr.next=NULL;
	recv_wr.sg_list = &sge;
	recv_wr.num_sge = 1;
	recv_wr.wr_id = (uintptr_t) node;	
	sge.length = message_size + sizeof(struct ibv_grh);
	sge.lkey = node->mr->lkey;
	sge.addr = (uintptr_t) node->mem;
	
	for (i=0; i<1; i++){
		ret = ibv_post_recv(node->cma_id->qp, &recv_wr, &recv_failure);
		if (ret){
			printf("failed to post receives.\n");
		}else{
			printf("received succeed. %dth\n",i);
			printf("mem addr:%p, #%s\n",node->mem,(char*)node->mem);
		}
	}
	return ret;	
}
int connect_handler(struct rdma_cm_id *cma_id){
	int ret = 0;	
	struct cmatest_node *node;
	struct rdma_conn_param conn_param;

	node = test.node_ptr;
	node->cma_id = cma_id;	
	cma_id->context = node;
	ret = verify_test_params(node);	
	ret = init_node(node);	
	ret = post_recvs(node);
	memset(&conn_param, 0, sizeof conn_param);
	conn_param.qp_num = node->cma_id->qp->qp_num;
	ret = rdma_accept(node->cma_id, &conn_param);
	return ret;
}
int cma_handler(struct rdma_cm_id *cma_id, struct rdma_cm_event *event)
{
	int ret = 0;
	printf("I got a event: %s, error: %d\n",rdma_event_str(event->event), event->status);
	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		ret = connect_handler(cma_id);
		break;
	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
	case RDMA_CM_EVENT_REJECTED:
	case RDMA_CM_EVENT_ADDR_RESOLVED:
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
	case RDMA_CM_EVENT_ESTABLISHED:
		ret = event->status;
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		/* Cleanup will occur after test completes. */
		break;
	default:
		break;
	}
	return ret;
}

void connect_events(){
	struct rdma_cm_event *event;
	int ret = 0;
	ret = rdma_get_cm_event(test.channel, &event);
	if (!ret){
		printf("I recieved a message.\n");
		ret = cma_handler(event->id, event);
		rdma_ack_cm_event(event);
		printf("Finished handler.\n");
	}
}

int get_rdma_addr(char *src, char *dst, char *port,
		  struct rdma_addrinfo *hints, struct rdma_addrinfo **rai)
{
	struct rdma_addrinfo rai_hints, *res;
	int ret;

	if (hints->ai_flags & RAI_PASSIVE)
		return rdma_getaddrinfo(src, port, hints, rai);

	rai_hints = *hints;
	if (src) {
		rai_hints.ai_flags |= RAI_PASSIVE;
		ret = rdma_getaddrinfo(src, NULL, &rai_hints, &res);
		if (ret)
			return ret;

		rai_hints.ai_src_addr = res->ai_src_addr;
		rai_hints.ai_src_len = res->ai_src_len;
		rai_hints.ai_flags &= ~RAI_PASSIVE;
	}

	ret = rdma_getaddrinfo(dst, port, &rai_hints, rai);
	if (src)
		rdma_freeaddrinfo(res);

	return ret;
}
void create_reply_ah(struct cmatest_node *node, struct ibv_wc *wc)
{
	struct ibv_qp_attr attr;
	struct ibv_qp_init_attr init_attr;

	node->ah = ibv_create_ah_from_wc(node->pd, wc, node->mem,
					 node->cma_id->port_num);
	node->remote_qpn = ntohl(wc->imm_data);

	ibv_query_qp(node->cma_id->qp, &attr, IBV_QP_QKEY, &init_attr);
	node->remote_qkey = attr.qkey;
}
int poll_cqs(){
	struct ibv_wc wc;
	int done, i, ret, j;
	struct ibv_wc *p_wc = &wc;
	int count_down=10;	
	while (count_down--){
		printf("I am polling.\n");	
		ret = ibv_poll_cq(test.node_ptr->cq, 8, p_wc);
		if (ret < 0) {
			printf("udaddy: failed polling CQ: %d\n", ret);
			return ret;
		}else{
			printf("recv %d msg\n",ret);		
			if (ret){
				if (p_wc->status == IBV_WC_SUCCESS){
					printf("Work completion\n");
				}	
				printf("content: #%s\n",(char*)test.node_ptr->mem);
				if (!test.node_ptr->ah){
					create_reply_ah(test.node_ptr,p_wc);
				}
				break;
			}
		}
		sleep(1);
	}
}
void run_server(){
	hints.ai_flags |= RAI_PASSIVE;
	hints.ai_port_space = RDMA_PS_UDP;

	test.channel = rdma_create_event_channel();
	struct rdma_cm_id *listen_id;
	int ret=0;
	alloc_nodes();
	ret = rdma_create_id(test.channel, &listen_id, &test, hints.ai_port_space);
	if (ret){
		perror("rdma_create_id error");
		return;
	}
	ret = get_rdma_addr(HOST_IP, NULL, HOST_PORT_STR, &hints, &test.rai);
	if (ret){
                perror("rdma_getaddrinfo error");
                return;
        }
	ret = rdma_bind_addr(listen_id,test.rai->ai_src_addr);
	ret = rdma_listen(listen_id,5);
	connect_events();
	ret = poll_cqs();
	rdma_destroy_event_channel(test.channel);
	if (test.rai){
		rdma_freeaddrinfo(test.rai);
	}
};

int main(){
	printf("I am the server.\n");
	run_server();
	return 0;
}
