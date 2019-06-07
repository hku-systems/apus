#include "../include/proxy/proxy.h"
#include "../include/config-comp/config-proxy.h"
#include <fcntl.h>
#include <netinet/tcp.h>
#include "../include/dare/dare_server.h"
#include "../include/dare/message.h"
#define __STDC_FORMAT_MACROS

static void stablestorage_save_request(void* data,void*arg);
static void stablestorage_dump_records(void*buf,void*arg);
static uint32_t stablestorage_get_records_len(void*arg);
static int stablestorage_load_records(void*buf,uint32_t size,void*arg);
static void update_highest_rec(void*arg);
static void do_action_to_server(uint16_t clt_id,uint8_t type,size_t data_size,void* data,void *arg);
static void do_action_send(uint16_t clt_id,size_t data_size,void* data,void* arg);
static void do_action_connect(uint16_t clt_id,void* arg);
static void do_action_close(uint16_t clt_id,void* arg);
static int set_socket_blocking(int fd, int blocking);

FILE *log_fp;

int dare_main(proxy_node* proxy, const char* config_path)
{
    int rc; 
    dare_server_input_t *input = (dare_server_input_t*)malloc(sizeof(dare_server_input_t));
    memset(input, 0, sizeof(dare_server_input_t));
    input->log = stdout;
    input->name = "";
    input->output = "dare_servers.out";
    input->srv_type = SRV_TYPE_START;
    input->sm_type = CLT_KVS;
    input->server_idx = 0xFF;
    char *server_idx = getenv("server_idx");
    if (server_idx != NULL)
        input->server_idx = (uint8_t)atoi(server_idx);
    input->group_size = 3;
    char *group_size = getenv("group_size");
    if (group_size != NULL)
        input->group_size = (uint8_t)atoi(group_size);

    input->do_action = do_action_to_server;
    input->store_cmd = stablestorage_save_request;
    input->get_db_size = stablestorage_get_records_len;
    input->create_db_snapshot = stablestorage_dump_records;
    input->apply_db_snapshot = stablestorage_load_records;
    input->update_state = update_highest_rec;
    memcpy(input->config_path, config_path, strlen(config_path));
    input->up_para = proxy;
    static int srv_type = SRV_TYPE_START;

    const char *server_type = getenv("server_type");
    if (server_type != NULL) {
        if (strcmp(server_type, "join") == 0) {
            srv_type = SRV_TYPE_JOIN;
        }
    }
    char *dare_log_file = getenv("dare_log_file");
    if (dare_log_file == NULL)
        dare_log_file = "";

    input->srv_type = srv_type;

    if (strcmp(dare_log_file, "") != 0) {
        input->log = fopen(dare_log_file, "w+");
        if (input->log==NULL) {
            printf("Cannot open log file\n");
            exit(1);
        }
    }
    if (SRV_TYPE_START == input->srv_type) {
        if (0xFF == input->server_idx) {
            printf("A server cannot start without an index\n");
            exit(1);
        }
    }
    pthread_t dare_thread;
    rc = pthread_create(&dare_thread, NULL, &dare_server_init, input);
    if (0 != rc) {
        fprintf(log_fp, "Cannot init dare_thread\n");
        return 1;
    }

    list_entry_t *n1 = malloc(sizeof(list_entry_t));
    n1->tid = dare_thread;
    LIST_INSERT_HEAD(&listhead, n1, entries);
    //fclose(log_fp);
    
    return 0;
}

static int is_inner(pthread_t tid)
{
    list_entry_t *np;
    LIST_FOREACH(np, &listhead, entries) {
        if (np->tid == tid)
            return 1;
    }
    return 0;
}

static hk_t gen_key(nid_t node_id,nc_t node_count){
    hk_t key = 0;
    key |= ((hk_t)node_id<<8);
    key |= (hk_t)node_count;
    return key;
}

static void leader_handle_submit_req(uint8_t type, ssize_t data_size, void* buf, int clt_id, proxy_node* proxy)
{
    socket_pair* pair = NULL;
    uint64_t req_id;
    uint16_t connection_id;

    pthread_spin_lock(&tailq_lock);
    uint64_t cur_rec = ++proxy->cur_rec;
    switch(type) {
        case CONNECT:
            pair = (socket_pair*)malloc(sizeof(socket_pair));
            memset(pair,0,sizeof(socket_pair));
            pair->clt_id = clt_id;
            pair->req_id = 0;
            nid_t node_id = get_node_id();
            pair->connection_id = gen_key(node_id, proxy->pair_count++);
            
            req_id = ++pair->req_id;
            connection_id = pair->connection_id;
            
            HASH_ADD_INT(proxy->leader_hash_map, clt_id, pair);
            break;
        case SEND:
            HASH_FIND_INT(proxy->leader_hash_map, &clt_id, pair);
            
            req_id = ++pair->req_id;
            connection_id = pair->connection_id;
            
            socket_pair* replaced_pair = NULL;
            HASH_REPLACE_INT(proxy->leader_hash_map, clt_id, pair, replaced_pair);
            break;
        case CLOSE:
            HASH_FIND_INT(proxy->leader_hash_map, &clt_id, pair);
            
            req_id = ++pair->req_id;
            connection_id = pair->connection_id;
            
            HASH_DEL(proxy->leader_hash_map, pair);
            break;
    }

    tailq_entry_t* n2 = (tailq_entry_t*)malloc(sizeof(tailq_entry_t));
    n2->req_id = req_id;
    n2->connection_id = connection_id;
    n2->type = type;
    n2->cmd.len = data_size;
    if (data_size)
        memcpy(n2->cmd.cmd, buf, data_size);
    TAILQ_INSERT_TAIL(&tailhead, n2, entries);

    pthread_spin_unlock(&tailq_lock);

    while (cur_rec > proxy->highest_rec);
}

static void get_socket_buffer_size(int sockfd)
{
    /* 
     * TCP provides flow control. TCP always tells its peer exactly
     * how many bytes of data it is willing to accept from the peer
     * at any one time. This is called the advertised window.
     * At any time, the window is the amount of room currently available
     * in the receive buffer, guaranteeing that the sender cannot
     * overflow the receiver buffer. The window changes dynamically over
     * time: As data is received from the sender, the window size decreases,
     * but as the receiving application reads data from the buffer, the
     * window size increases.
     */
    socklen_t i;
    size_t len;

    i = sizeof(len);
    if (getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &len, &i) < 0) {
        perror(": getsockopt");
    }

    printf("receive buffer size = %d\n", len);

    if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &len, &i) < 0) {
        perror(": getsockopt");
    }

    printf("send buffer size = %d\n", len);
}

static int set_socket_blocking(int fd, int blocking) {
    int flags;

    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        fprintf(stderr, "fcntl(F_GETFL): %s", strerror(errno));
    }

    if (blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1) {
        fprintf(stderr, "fcntl(F_SETFL,O_NONBLOCK): %s", strerror(errno));
    }
    return 0;
}

static int set_socket_timeout(int fd, struct timeval *timeout) {
	/*
	 * SO_RCVTIMEO and SO_SNDTIMEO
     * Specify the receiving or sending timeouts until reporting  an
     * error. The argument is a struct timeval. If an input or output
     * function blocks for this period of time, and data has been sent
     * or received, the return value of that function will be the
     * amount of data transferred; if no data has been transferred and
     * the timeout has been reached then -1 is returned with errno set
     * to EAGAIN or EWOULDBLOCK, or EINPROGRESS just as if the socket
     * was specified to be nonblocking. If the timeout is set to zero
     * (the default) then the operation will never timeout.
	 */
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, timeout, sizeof(struct timeval)) < 0) {
        perror("set_socket_timeout");
    }
    return 0;
}

void proxy_on_read(proxy_node* proxy, void* buf, ssize_t bytes_read, int fd)
{
	if (is_inner(pthread_self()))
		return;

	if (is_leader())
        leader_handle_submit_req(SEND, bytes_read, buf, fd, proxy);

	return;
}

void proxy_on_accept(proxy_node* proxy, int fd)
{
	if (is_inner(pthread_self()))
		return;

	if (is_leader())
        leader_handle_submit_req(CONNECT, 0, NULL, fd, proxy);

	return;	
}

void proxy_on_close(proxy_node* proxy, int fd)
{
	if (is_inner(pthread_self()))
		return;

	if (is_leader())
        leader_handle_submit_req(CLOSE, 0, NULL, fd, proxy);

	return;
}

static void update_highest_rec(void*arg)
{
    proxy_node* proxy = arg;
    proxy->highest_rec++;   
}

static void stablestorage_save_request(void* data,void*arg)
{
    proxy_node* proxy = arg;
    proxy_msg_header* header = (proxy_msg_header*)data;
    switch(header->action){
        case CONNECT:
        {
            store_record(proxy->db_ptr,PROXY_CONNECT_MSG_SIZE,data);
            break;
        }
        case SEND:
        {
            proxy_send_msg* send_msg = (proxy_send_msg*)data;
            store_record(proxy->db_ptr,PROXY_SEND_MSG_SIZE(send_msg),data);
            break;
        }
        case CLOSE:
        {
            store_record(proxy->db_ptr,PROXY_CLOSE_MSG_SIZE,data);
            break;
        }
    }
}

static uint32_t stablestorage_get_records_len(void*arg)
{
    proxy_node* proxy = arg;
    uint32_t records_len = get_records_len(proxy->db_ptr);
    return records_len;
}

static void stablestorage_dump_records(void*buf,void*arg)
{
    proxy_node* proxy = arg;
    dump_records(proxy->db_ptr,buf);
}

static int stablestorage_load_records(void*buf,uint32_t size,void*arg)
{
    proxy_node* proxy = arg;
    proxy_msg_header* header;
    uint32_t len = 0;
    while(len < size) {
        header = (proxy_msg_header*)((char*)buf + len);
        switch(header->action){
            case SEND:
            {
                proxy_send_msg* send_msg = (proxy_send_msg*)header;
                len += PROXY_SEND_MSG_SIZE(send_msg);
                store_record(proxy->db_ptr,PROXY_SEND_MSG_SIZE(send_msg),header);
                do_action_send(header->connection_id, send_msg->data.cmd.len, send_msg->data.cmd.cmd, arg);
                break;
            }
            case CONNECT:
            {
                len += PROXY_CONNECT_MSG_SIZE;
                store_record(proxy->db_ptr,PROXY_CONNECT_MSG_SIZE,header);
                do_action_connect(header->connection_id, arg);
                break;
            }
            case CLOSE:
            {
                len += PROXY_CLOSE_MSG_SIZE;
                store_record(proxy->db_ptr,PROXY_CLOSE_MSG_SIZE,header);
                do_action_close(header->connection_id, arg);
                break;
            }
        }
    }
    return 0;
}

static void do_action_to_server(uint16_t clt_id,uint8_t type,size_t data_size,void* data,void*arg)
{
    proxy_node* proxy = arg;
    FILE* output = NULL;
    if(proxy->req_log){
        output = proxy->req_log_file;
    }
    switch(type){
        case CONNECT:
        	if(output!=NULL){
        		fprintf(output,"Operation: Connects.\n");
            }
            do_action_connect(clt_id,arg);
            break;
        case SEND:
        	if(output!=NULL){
        		fprintf(output,"Operation: Sends data.\n");
            }
            do_action_send(clt_id,data_size,data,arg);
            break;
        case CLOSE:
        	if(output!=NULL){
        		fprintf(output,"Operation: Closes.\n");
            }
            do_action_close(clt_id,arg);
            break;
        default:
            break;
    }
    return;
}

static void do_action_connect(uint16_t clt_id,void* arg)
{
    proxy_node* proxy = arg;

    socket_pair* ret;
    HASH_FIND(hh, proxy->follower_hash_map, &clt_id, sizeof(uint16_t), ret);
    if (NULL == ret)
    {
        ret = malloc(sizeof(socket_pair));
        memset(ret,0,sizeof(socket_pair));

        ret->connection_id = clt_id;
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0)
        {
            fprintf(stderr, "ERROR opening socket!\n");
            goto do_action_connect_exit;
        }
        ret->p_s = sockfd;
        HASH_ADD(hh, proxy->follower_hash_map, connection_id, sizeof(uint16_t), ret);

        if (connect(ret->p_s, (struct sockaddr*)&proxy->sys_addr.s_addr, proxy->sys_addr.s_sock_len) < 0)
            fprintf(stderr, "ERROR connecting!\n");

        set_socket_blocking(ret->p_s, 0);

        int enable = 1;
        if(setsockopt(ret->p_s, IPPROTO_TCP, TCP_NODELAY, (void*)&enable, sizeof(enable)) < 0)
            fprintf(stderr, "TCP_NODELAY SETTING ERROR!\n");
    }

do_action_connect_exit:
	return;
}

static void do_action_send(uint16_t clt_id,size_t data_size,void* data,void* arg)
{
	proxy_node* proxy = arg;
	socket_pair* ret;
	HASH_FIND(hh, proxy->follower_hash_map, &clt_id, sizeof(uint16_t), ret);

	if(NULL==ret){
		goto do_action_send_exit;
	}else{
		int n = write(ret->p_s, data, data_size);
		if (n < 0)
			fprintf(stderr, "ERROR writing to socket!\n");
	}
do_action_send_exit:
	return;
}

static void do_action_close(uint16_t clt_id,void* arg)
{
	proxy_node* proxy = arg;
	socket_pair* ret;
	HASH_FIND(hh, proxy->follower_hash_map, &clt_id, sizeof(uint16_t), ret);
	if(NULL==ret){
		goto do_action_close_exit;
	}else{
		if (close(ret->p_s))
			fprintf(stderr, "ERROR closing socket!\n");
		HASH_DEL(proxy->follower_hash_map, ret);
	}
do_action_close_exit:
	return;
}

proxy_node* proxy_init(const char* config_path,const char* proxy_log_path)
{
    proxy_node* proxy = (proxy_node*)malloc(sizeof(proxy_node));

    if(NULL==proxy){
        err_log("PROXY : Cannot Malloc Memory For The Proxy.\n");
        goto proxy_exit_error;
    }

    memset(proxy,0,sizeof(proxy_node));
    
    if(proxy_read_config(proxy,config_path)){
        err_log("PROXY : Configuration File Reading Error.\n");
        goto proxy_exit_error;
    }

    int build_log_ret = 0;
    if(proxy_log_path==NULL){
        proxy_log_path = ".";
    }else{
        if((build_log_ret=mkdir(proxy_log_path,S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH))!=0){
            if(errno!=EEXIST){
                err_log("PROXY : Log Directory Creation Failed,No Log Will Be Recorded.\n");
            }else{
                build_log_ret = 0;
            }
        }
    }

    if(!build_log_ret){
        //if(proxy->req_log){
            char* req_log_path = (char*)malloc(sizeof(char)*strlen(proxy_log_path)+50);
            memset(req_log_path,0,sizeof(char)*strlen(proxy_log_path)+50);
            if(NULL!=req_log_path){
                sprintf(req_log_path,"%s/node-proxy-req.log",proxy_log_path);
                //err_log("%s.\n",req_log_path);
                proxy->req_log_file = fopen(req_log_path,"w");
                free(req_log_path);
            }
            if(NULL==proxy->req_log_file && proxy->req_log){
                err_log("PROXY : Client Request Log File Cannot Be Created.\n");
            }
        //}
    }

    TAILQ_INIT(&tailhead);
    LIST_INIT(&listhead);

    proxy->db_ptr = initialize_db(proxy->db_name,0);

    proxy->follower_hash_map = NULL;
    proxy->leader_hash_map = NULL;

    if(pthread_spin_init(&tailq_lock, PTHREAD_PROCESS_PRIVATE)){
        err_log("PROXY: Cannot init the lock\n");
    }

    dare_main(proxy, config_path);

    return proxy;

proxy_exit_error:
    if(NULL!=proxy){
        free(proxy);
    }
    return NULL;

}
