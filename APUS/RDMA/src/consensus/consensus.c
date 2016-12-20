#define _GNU_SOURCE
#include "../include/consensus/consensus.h"
#include "../include/consensus/consensus-msg.h"

#include "../include/rdma/dare_ibv_rc.h"
#include "../include/rdma/dare_server.h"
#include "../include/util/clock.h"
#define IBDEV dare_ib_device
#define SRV_DATA ((dare_server_data_t*)dare_ib_device->udata)

#define USE_SPIN_LOCK
//#define REPLICA_MEASURE_LATENCY
#define LEADER_MEASURE_LATENCY
//#define AFFINITY

#define DUMMY_END 'f'

typedef enum request_type_t{
	P_TCP_CONNECT=1,
	P_SEND=2,
	P_CLOSE=3,
	P_OUTPUT=4,
	P_NOP=5,
    P_UDP_CONNECT=6,
}request_type;

typedef struct consensus_component_t{ con_role my_role;
    uint8_t node_id;

    uint8_t group_size;
    struct node_t* my_node;

    FILE* sys_log_file;
    int sys_log;
    int stat_log;

    view* cur_view;
    view_stamp* highest_seen_vs; 
    view_stamp* highest_to_commit_vs;
    view_stamp* highest_committed_vs;

    db* db_ptr;

    pthread_mutex_t lock;
    pthread_spinlock_t spinlock;

    user_cb ucb;
    up_check uc;
    up_get ug;
    void* up_para;
}consensus_component;

consensus_component* init_consensus_comp(struct node_t* node,uint8_t node_id,FILE* log,int sys_log,int stat_log,const char* db_name,void* db_ptr,int group_size,
    view* cur_view,view_stamp* to_commit,view_stamp* highest_committed_vs,view_stamp* highest,user_cb u_cb,up_check uc,up_get ug,void* arg){
    consensus_component* comp = (consensus_component*)malloc(sizeof(consensus_component));
    memset(comp,0,sizeof(consensus_component));

    if(NULL!=comp){
        comp->db_ptr = db_ptr;  
        comp->sys_log = sys_log;
        comp->stat_log = stat_log;
        comp->sys_log_file = log;
        comp->my_node = node;
        comp->node_id = node_id;
        comp->group_size = group_size;
        comp->cur_view = cur_view;
        if(comp->cur_view->leader_id == comp->node_id){
            comp->my_role = LEADER;
        }else{
            comp->my_role = SECONDARY;
        }
        comp->ucb = u_cb;
        comp->uc = uc;
        comp->ug = ug;
        comp->up_para = arg;
        comp->highest_seen_vs = highest;
        comp->highest_seen_vs->view_id = 1;
        comp->highest_seen_vs->req_id = 0;
        comp->highest_committed_vs = highest_committed_vs; 
        comp->highest_committed_vs->view_id = 1; 
        comp->highest_committed_vs->req_id = 0; 
        comp->highest_to_commit_vs = to_commit;
        comp->highest_to_commit_vs->view_id = 1;
        comp->highest_to_commit_vs->req_id = 0;

#ifdef USE_SPIN_LOCK
        pthread_spin_init(&comp->spinlock, PTHREAD_PROCESS_PRIVATE);
#else
        pthread_mutex_init(&comp->lock, NULL);
#endif

        init_output_mgr();
        goto consensus_init_exit;

    }
consensus_init_exit:
    return comp;
}

static view_stamp get_next_view_stamp(consensus_component* comp){
    view_stamp next_vs;
    next_vs.view_id = comp->highest_seen_vs->view_id;
    next_vs.req_id = (comp->highest_seen_vs->req_id+1);
    return next_vs;
};

static int reached_quorum(uint64_t bit_map, int group_size){
    // this may be compatibility issue 
    if(__builtin_popcountl(bit_map) >= ((group_size/2)+1)){
        return 1;
    }else{
        return 0;
    }
}


static void set_affinity(int core_id)
{
    if(core_id > 24)
	core_id = 24;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0)
        fprintf(stderr, "pthread_setaffinity_np failed\n");
}

__thread int has_set_affinity = 0;
int core_id = 1;

dare_log_entry_t* leader_handle_submit_req(struct consensus_component_t* comp, size_t data_size, void* data, uint8_t type, view_stamp* clt_id)
{	
#ifdef LEADER_MEASURE_LATENCY                                                                                                                                           
        clock_handler c_k;
        clock_init(&c_k);
        clock_add(&c_k);
#endif

#ifdef USE_SPIN_LOCK
        pthread_spin_lock(&comp->spinlock);
#else
        pthread_mutex_lock(&comp->lock);
#endif

#ifdef AFFINITY
	if (has_set_affinity == 0)
	{
		if (core_id > 24)
			core_id = 1;
		set_affinity(core_id);
		core_id++;
		has_set_affinity = 1;
		/*int s, policy = SCHED_FIFO;
		struct sched_param param;
		param.sched_priority = 1;
		s = pthread_setschedparam(pthread_self(), policy, &param);
		if (s != 0)
			fprintf(stderr, "pthread_setschedparam failed\n");*/
	}
#endif

        view_stamp next = get_next_view_stamp(comp);
        SYS_LOG(comp, "handling request, view id is %"PRIu32", req id  %"PRIu32", type is %d, data is (%s), size is %zu\n",
		               next.view_id, next.req_id, type, (char*)data, data_size);
	//if (type == 2)
	//fprintf(comp->sys_log_file, "size %zu\n", data_size);
        if (type == P_TCP_CONNECT)
        {
            clt_id->view_id = next.view_id;
            clt_id->req_id = next.req_id;
        }

        db_key_type record_no = vstol(&next);

        comp->highest_seen_vs->req_id = comp->highest_seen_vs->req_id + 1;

        dare_log_entry_t *entry = log_add_new_entry(SRV_DATA->log);

        if (!log_fit_entry_header(SRV_DATA->log, SRV_DATA->log->end)) {
            SRV_DATA->log->end = 0;
        }

        SRV_DATA->log->tail = SRV_DATA->log->end;
        entry->data_size = data_size + 1;
        SRV_DATA->log->end += log_entry_len(entry);
        uint32_t offset = (uint32_t)(offsetof(dare_log_t, entries) + SRV_DATA->log->tail);

        dare_ib_ep_t *ep;
        uint8_t i, *send_count_ptr;
        int send_flags[MAX_SERVER_COUNT], poll_completion[MAX_SERVER_COUNT] = {0};

        for (i = 0; i < comp->group_size; i++) {
            ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
            if (i == SRV_DATA->config.idx || 0 == ep->rc_connected)
                continue;
            send_count_ptr = &(ep->rc_ep.rc_qp.send_count);

            if((*send_count_ptr & S_DEPTH_) == 0)
                send_flags[i] = 1;
            else
                send_flags[i] = 0;

            if ((*send_count_ptr & S_DEPTH_) == S_DEPTH_)
                poll_completion[i] = 1;
            
            (*send_count_ptr)++;
        }

#ifdef USE_SPIN_LOCK
        pthread_spin_unlock(&comp->spinlock);
#else
        pthread_mutex_unlock(&comp->lock);
#endif

        entry->req_canbe_exed.view_id = comp->highest_committed_vs->view_id;
        entry->req_canbe_exed.req_id = comp->highest_committed_vs->req_id;
        
        if (data != NULL)
            memcpy(entry->data,data,data_size);

        entry->msg_vs = next;
        entry->node_id = comp->node_id;
        entry->type = type;
        entry->clt_id.view_id = (type != P_NOP)?clt_id->view_id:0;
        entry->clt_id.req_id = (type != P_NOP)?clt_id->req_id:0;

        SYS_LOG(comp, "storing view id is  %"PRIu32", req id %"PRIu32", entry addr is %p, end is %"PRIu64"\n", 
		next.view_id, next.req_id, (void*)entry, SRV_DATA->log->end);
        request_record* record_data = (request_record*)((char*)entry + offsetof(dare_log_entry_t, data_size));

	//fprintf(comp->sys_log_file, "size is %zu, type is %"PRIu8"\n", data_size, type);

        if(store_record(comp->db_ptr, sizeof(record_no), &record_no, REQ_RECORD_SIZE(record_data) - 1, record_data))
        {
            fprintf(stderr, "Can not save record from database.\n");
            goto handle_submit_req_exit;
        }

#ifdef LEADER_MEASURE_LATENCY                                                                                                                                           
        //clock_add(&c_k);                                                                                                                                              
#endif
        char* dummy = (char*)((char*)entry + log_entry_len(entry) - 1);
        *dummy = DUMMY_END;

        uint64_t bit_map = (1<<comp->node_id);
        rem_mem_t rm;
        memset(&rm, 0, sizeof(rem_mem_t));

        for (i = 0; i < comp->group_size; i++) {
            ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
            if (i == SRV_DATA->config.idx || 0 == ep->rc_connected)
                continue;

            rm.raddr = ep->rc_ep.rmt_mr.raddr + offset;
            rm.rkey = ep->rc_ep.rmt_mr.rkey;
            SYS_LOG(comp, "sending post to %"PRIu32", remote addr %p, view id is %"PRIu32", req id %"PRIu32", \
                           type is %d, data is (%s), bip is %"PRIu64"\n", i, (void*)rm.raddr, next.view_id, next.req_id, type, (char*)data, bit_map);

            post_send(i, entry, log_entry_len(entry), IBDEV->lcl_mr, IBV_WR_RDMA_WRITE, &rm, send_flags[i], poll_completion[i]);
        }

recheck:
        for (i = 0; i < comp->group_size; i++) {
            //if (entry->ack[i].msg_vs.view_id == next.view_id && entry->ack[i].msg_vs.req_id == next.req_id)
	    if(entry->ack[i].node_id != 0)
            {
                bit_map = bit_map | (1<<entry->ack[i].node_id);
            }
        }
        if (reached_quorum(bit_map, comp->group_size)) {
            SYS_LOG(comp, "reached quorum, view id is %"PRIu32", req id %"PRIu32", type is %d, data is (%s), \
                           bip is %"PRIu64"\n", next.view_id, next.req_id, type, (char*)data, bit_map);
#ifdef LEADER_MEASURE_LATENCY
            clock_add(&c_k);                                                                                                                                      
            clock_display(comp->sys_log_file, &c_k, type, data_size);  
#endif
            //TODO: do we need the lock here?
            while (entry->msg_vs.req_id > comp->highest_committed_vs->req_id + 1);
            comp->highest_committed_vs->req_id = comp->highest_committed_vs->req_id + 1;

        }else{
	    goto recheck;
        }
handle_submit_req_exit:
    return entry;
}

void *handle_accept_req(void* arg)
{
    consensus_component* comp = arg;

    db_key_type start;
    db_key_type end;
    db_key_type index;
    
    dare_log_entry_t* entry;

    int core_id = (comp->node_id / 8) * 3 + 1; // for 65 nodes
    //int core_id = 1;
    //int core_id = (comp->node_id / 8) * 2 - 2; // for 105 nodes
    //if (core_id <= 0) // for 105 nodes
    //	core_id += 3; // for 105 nodes

    fprintf(stderr, "\nnid %d is binding to %d\n", comp->node_id, core_id);
    set_affinity(core_id);
    
    SYS_LOG(comp, "launching replica thread. sys log is %d", comp->sys_log);

    for (;;)
    {
        if (comp->cur_view->leader_id != comp->node_id)
        {
            if (comp->uc(comp->up_para))
                return NULL;

            entry = log_get_entry(SRV_DATA->log, &SRV_DATA->log->end);

            //SYS_LOG(comp, "loop get view %"PRIu32", req id is %"PRIu32", type is %d, size is %zu, entry point %p\n",
           //	               entry->msg_vs.view_id , entry->msg_vs.req_id, entry->type, entry->data_size, (void*)entry);
            if (entry->data_size != 0)
            {
                SYS_LOG(comp, "match get view %"PRIu32", req id is %"PRIu32", type is %d, size is %zu, entry point %p, end is %"PRIu64"\n", 
                               entry->msg_vs.view_id , entry->msg_vs.req_id, entry->type, entry->data_size, (void*)entry, SRV_DATA->log->end);
                char* dummy = (char*)((char*)entry + log_entry_len(entry) - 1);
                if (*dummy == DUMMY_END) // atmoic opeartion
                {

#ifdef REPLICA_MEASURE_LATENCY
        	    clock_handler c_k;
        	    clock_init(&c_k);
        	    clock_add(&c_k);
#endif

                    SYS_LOG(comp, "found view %"PRIu32", req id is %"PRIu32", type is %d, data size is %zu, entry size is %"PRIu32"\n",
                                   entry->msg_vs.view_id , entry->msg_vs.req_id, entry->type, entry->data_size, log_entry_len(entry));
                    if(entry->msg_vs.view_id < comp->cur_view->view_id){
                    // TODO
                    //goto reloop;
                    }
                    // if we this message is not from the current leader
                    if(entry->msg_vs.view_id == comp->cur_view->view_id && entry->node_id != comp->cur_view->leader_id){
                    // TODO
                    //goto reloop;
                    }

                    // update highest seen request
                    if(view_stamp_comp(&entry->msg_vs, comp->highest_seen_vs) > 0){
                        *(comp->highest_seen_vs) = entry->msg_vs;
                    }

                    db_key_type record_no = vstol(&entry->msg_vs);
                    // record the data persistently
                    request_record* record_data = (request_record*)((char*)entry + offsetof(dare_log_entry_t, data_size));

                    store_record(comp->db_ptr, sizeof(record_no), &record_no, REQ_RECORD_SIZE(record_data) - 1, record_data);
#ifdef REPLICA_MEASURE_LATENCY
        	    clock_add(&c_k);
#endif
                    SRV_DATA->log->tail = SRV_DATA->log->end;
                    SRV_DATA->log->end += log_entry_len(entry);
                    uint32_t offset = (uint32_t)(offsetof(dare_log_t, entries) + SRV_DATA->log->tail + ACCEPT_ACK_SIZE * comp->node_id);
                    accept_ack* reply = (accept_ack*)((char*)entry + ACCEPT_ACK_SIZE * comp->node_id);
                    reply->node_id = comp->node_id;
                    //reply->msg_vs.view_id = entry->msg_vs.view_id;
                    //reply->msg_vs.req_id = entry->msg_vs.req_id;
                    
                    if (entry->type == P_OUTPUT)
                    {
                        // up = get_mapping_fd() is defined in ev_mgr.c
                        int fd = comp->ug(entry->clt_id, comp->up_para);
                        // consider entry->data as a pointer.
                        uint64_t hash = get_output_hash(fd, *(long*)entry->data);
                        //reply->hash = hash; 
                    }

                    rem_mem_t rm;
                    dare_ib_ep_t *ep = (dare_ib_ep_t*)SRV_DATA->config.servers[entry->node_id].ep;
                    memset(&rm, 0, sizeof(rem_mem_t));
                    uint32_t *send_count_ptr = &(ep->rc_ep.rc_qp.send_count);
                    int send_flags, poll_completion = 0;

                    if((*send_count_ptr & S_DEPTH_) == 0)
                        send_flags = 1;
                    else
                        send_flags = 0;

                    if ((*send_count_ptr & S_DEPTH_) == S_DEPTH_)
                        poll_completion = 1;

                    (*send_count_ptr)++;

                    rm.raddr = ep->rc_ep.rmt_mr.raddr + offset;
                    rm.rkey = ep->rc_ep.rmt_mr.rkey;

                    post_send(entry->node_id, reply, ACCEPT_ACK_SIZE, IBDEV->lcl_mr, IBV_WR_RDMA_WRITE, &rm, send_flags, poll_completion);


                    if(view_stamp_comp(&entry->req_canbe_exed, comp->highest_committed_vs) > 0)
                    {
                        start = vstol(comp->highest_committed_vs)+1;
                        end = vstol(&entry->req_canbe_exed);
                        SYS_LOG(comp, "start is %"PRIu64", end is  %"PRIu64"\n", start, end);
                        for(index = start; index <= end; index++)
                        {
                            comp->ucb(index,comp->up_para);
                            SYS_LOG(comp, "finish index %"PRIu64"\n", index);
                        }
                        *(comp->highest_committed_vs) = entry->req_canbe_exed;
                    }

#ifdef REPLICA_MEASURE_LATENCY
            	    clock_add(&c_k);
            	    clock_display(comp->sys_log_file, &c_k, 0, 0);
#endif
                    SYS_LOG(comp, "before leaving..... %d, SRV_DATA->log->end is %"PRIu64"\n", entry->msg_vs.view_id, SRV_DATA->log->end);


                }   
            }
        }
    }
};
