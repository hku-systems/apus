/*
* Author: Jingyu Yang
* Date: April 23, 2016
* Description: Output management
*/
#include "../include/output/output.h"
#include "../include/output/crc64.h"
#include "../include/output/adlist.h"
#include "../include/util/debug.h"
void init_output_mgr(){
	output_manager_t *output_mgr = get_output_mgr();
	debug_log("[init_output_mgr] output_mgr is inited at %p\n",output_mgr);
}

void deinit_output_mgr(){
	output_manager_t *output_mgr = get_output_mgr();
	if (output_mgr){ 
		debug_log("[deinit_output_mgr] output_mgr(%p) will be freed\n",output_mgr);
		//[TODO] free output_handler first
		int num = deinit_fd_handler(output_mgr);
		debug_log("[deinit_output_mgr] %d fd_handler have been freed\n",num);
		free(output_mgr);
		output_mgr=NULL;
	}
}

void init_fd_handler(output_manager_t *output_mgr){
	if (NULL==output_mgr){
		return;
	}
	memset(output_mgr->fd_handler,0,sizeof(output_mgr->fd_handler));
}

// return how many fd handler has been freed.
int deinit_fd_handler(output_manager_t *output_mgr){
	if (NULL==output_mgr){
		return 0;
	}
	int cnt=0;
	for (int i=0;i<MAX_FD_SIZE;i++){
		output_handler_t* ptr = output_mgr->fd_handler[i];
		if (ptr){
			delete_output_handler(ptr);
			ptr=NULL;
			output_mgr->fd_handler[i] = NULL;
			cnt++;
		}
	}
	return cnt;
}

output_manager_t * get_output_mgr(){
	static output_manager_t * inner_output_mgr=NULL; // This is a singleton of output_mgr
	if (NULL == inner_output_mgr){
		//init output manager
		inner_output_mgr = (output_manager_t *)malloc(sizeof(output_manager_t)); // it will be freed in deinit_output_mgr
		init_fd_handler(inner_output_mgr);
	}
	return inner_output_mgr;
}

static int min(int a, int b){
	return a<b?a:b;
}

// malloc a output_handler_t for this fd
output_handler_t* new_output_handler(int fd){
	output_handler_t* ptr = (output_handler_t*)malloc(sizeof(output_handler_t));
	ptr->fd = fd;
	ptr->count = 0;
	ptr->output_list = listCreate(); 
	// set free function 
	ptr->output_list->free = free;
	ptr->hash = 0L;
	memset(ptr->hash_buffer,0,sizeof(ptr->hash_buffer));
	ptr->hash_buffer_curr = 0;
	ptr->called_cnt=0;
	return ptr;
}

void delete_output_handler(output_handler_t* ptr){
	if (NULL == ptr){
		return;
	}
	listRelease(ptr->output_list);
	ptr->output_list = NULL;
	free(ptr);
}

int del_output_handler_by_fd(int fd){
	//debug_log("[del_output_handler_by_fd] fd: %d \n",fd);
	int retval = -1;
	output_manager_t *output_mgr = get_output_mgr();
	if (NULL == output_mgr){
		return retval;
	}
	if (fd>=MAX_FD_SIZE){
		debug_log("[del_output_handler_by_fd] fd: %d is out of limit %d\n",fd,MAX_FD_SIZE);
		return retval;
	}
	output_handler_t* ptr = output_mgr->fd_handler[fd];
	if (NULL == ptr){
		//debug_log("[del_output_handler_by_fd] the handler is NULL for fd:%d, no need to delete.\n",fd);
		retval =0;
	}else{
		// release output_list
		if (ptr->output_list){
			// The value will be freed during iteratering.
			listRelease(ptr->output_list);
			ptr->output_list=NULL;
		}
		free(ptr);
		ptr=NULL;
		output_mgr->fd_handler[fd]=NULL;
		retval =0;
	}
	return retval;
}

output_handler_t* get_output_handler_by_fd(int fd){
	debug_log("[get_output_handler_by_fd] fd: %d \n",fd);
	output_manager_t *output_mgr = get_output_mgr();
	if (NULL == output_mgr){
		return NULL;
	}
	if (fd>=MAX_FD_SIZE){
		debug_log("[get_output_handler_by_fd] fd: %d is out of limit %d\n",fd,MAX_FD_SIZE);
		return NULL;
	}
	output_handler_t* ptr = output_mgr->fd_handler[fd];
	if (NULL == ptr){ // At the first time, output_handler_t need to be inited.		
		output_mgr->fd_handler[fd] = new_output_handler(fd);
		debug_log("[get_output_handler_by_fd] fd: %d got a handler:%p\n",fd,output_mgr->fd_handler[fd]);
	}
	return output_mgr->fd_handler[fd];
}

//accept a buff with size, I will store into different connection (fd).
//And return n number of hash values.
#ifdef DEBUG_LOG
void show_buff(unsigned char* buff, ssize_t buff_size){
	fprintf(stderr,"[show_buff]#");
	for (int i=0;i<buff_size;i++){
		fprintf(stderr,"0x%x,",buff[i]);
	}
	fprintf(stderr,"#\n");
}
#else
void show_buff(unsigned char* buff, ssize_t buff_size){
	return;
}
#endif

int store_output(int fd, const unsigned char *buff, ssize_t buff_size)
{
	show_buff(buff,buff_size);
	debug_log("[store_output] fd: %d, input buff_size: %zu, buff:#%s#\n",fd, buff_size,(char*)buff);
	int retval=-1;
	if (NULL==buff || 0 == buff_size){ // nothing to be done, but it is not a error
		retval=0;
		return retval;
	}
	// A output_handler will be got from different fd
	output_handler_t* output_handler = get_output_handler_by_fd(fd);
	if (NULL == output_handler){// error
		debug_log("[store_output] fd:%d, get_output_handler_by_fd error. \n",fd);
		return retval;
	}
	// The following code will put buff into output_handler.hash_buffer
	// Please do not delete the comment unless you list your reason at here. ^^.Thanks.
	int push_size =0;
	retval=0; // default value means no hash value is generated.
	while (push_size < buff_size){ // Which means the input buff has not been handled.
		int left_space = HASH_BUFFER_SIZE - output_handler->hash_buffer_curr;
		int wait_size = buff_size - push_size;
		int actual_size = min(left_space,wait_size); 
		unsigned char * dest_ptr = output_handler->hash_buffer + output_handler->hash_buffer_curr;
		// Bugfixed. at memcpy, buff need a offset.
		unsigned char* src_ptr = buff+push_size;
		memcpy(dest_ptr,src_ptr,actual_size);
		output_handler->hash_buffer_curr+=actual_size;
		left_space = HASH_BUFFER_SIZE - output_handler->hash_buffer_curr;
		push_size+=actual_size;
		debug_log("[store_output] fd:%d, copied %d bytes into hash_buffer[%d/%d], then push_size:%d\n",fd,actual_size,output_handler->hash_buffer_curr,HASH_BUFFER_SIZE,push_size);
		if (0==left_space){ // The hash buffer is full.
			show_buff(output_handler->hash_buffer,HASH_BUFFER_SIZE);
			debug_log("[store_output] fd:%d, previous hash:0x%"PRIx64", buff will be used for crc64.\n",fd,output_handler->hash);
			output_handler->hash = crc64(output_handler->hash,output_handler->hash_buffer,HASH_BUFFER_SIZE);
			// curr is clear, since new hash is generated.
			output_handler->hash_buffer_curr=0;
			debug_log("[store_output] fd:%d, hash is generated, hash:0x%"PRIx64"\n",fd, output_handler->hash);
			if (output_handler->output_list){
				// add hash into output_list;
				uint64_t *new_hash = (uint64_t*)malloc(sizeof(uint64_t));
				*new_hash = output_handler->hash;
				listAddNodeTail(output_handler->output_list, (void*)new_hash);
				debug_log("[store_output] fd:%d, hash is putted into output_list. count:%ld, hash:0x%"PRIx64"\n", 
				fd, output_handler->count, output_handler->hash);
				output_handler->count++;
				retval++;// one hash value is generated.
			}else{
				debug_log("[store_output] [error] output_list is NULL, fd:%d, output_handler ptr:%p\n",fd,output_handler);
			}
		}
	}
	return retval;
}

long determine_output(int fd){
	debug_log("[determine_output] fd: %d\n",fd);
	long retval=-1;
	// A output_handler will be got from different fd
	output_handler_t* output_handler = get_output_handler_by_fd(fd);
	if (NULL == output_handler){// error
		debug_log("[determine_output] fd:%d, get_output_handler_by_fd error. \n",fd);
		return retval;
	}
	output_handler->called_cnt++;
	if (output_handler->called_cnt%CHECK_PERIOD == 0){ 
		// A output conconsistency will be triggred.
		// However, if one machine is slow, it may not calculate hash value at this round.
		// We decide to propose hash value in the old round.
		long round_goback = output_handler->count - CHECK_GOBACK;
		if (round_goback>=0){
			retval = round_goback;
		}else{
			retval = -1;
		}
	}else{
		retval = -1;
	}
	return retval;
}

// if not found, it will return 0
uint64_t get_val_by_index(list *list_head, long index){
	listNode *ln;
    listIter li;
    long cnt=0;
    uint64_t retval = 0;
    listRewind(list_head,&li);
    while((ln = listNext(&li))) {
    	uint64_t val = *(uint64_t *)listNodeValue(ln);
    	if (index == cnt){ // Found it
    		retval = val;
    		debug_log("[get_val_by_index] found val:0x%"PRIx64" at index:%ld\n",retval,index);
    		return retval;
    	}
    	cnt++;
    }
    debug_log("[get_val_by_index] Not Found at index:%ld, 0 will be returned\n",index);
    return retval;
}
// If it is impossible to get hash value, 0 will be returned as default value.
uint64_t get_output_hash(int fd, long hash_index){
	debug_log("[get_output_hash] fd: %d hash_index:%ld\n",fd,hash_index);
	uint64_t retval = 0;
	// A output_handler will be got from different fd
	output_handler_t* output_handler = get_output_handler_by_fd(fd);
	if (NULL == output_handler){// error
		debug_log("[get_output_hash] fd:%d, get_output_handler_by_fd error. \n",fd);
		return retval;
	}
	if (hash_index < output_handler->count){
		list * list_head = output_handler->output_list;
		retval = get_val_by_index(list_head,hash_index);
	}else{
		debug_log("[get_output_hash] hash_index: %ld is invalid, count: %ld\n", hash_index, output_handler->count);
		retval = 0;
	}
	return retval;
}

int del_output(int fd){
	//debug_log("[del_output] the output data structure of fd: %d will be freed\n",fd);
	// A output_handler will be got from different fd
	int retval = del_output_handler_by_fd(fd);
	return retval;
}
