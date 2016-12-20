// Unix socket echo server
// copy from 
// https://www.pacificsimplicity.ca/blog/libevent-echo-server-tutorial


#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#define UNIX_SOCK_PATH "/tmp/checkpoint.server.sock"
#define UNIX_CMD "disconnect"


void accept_error_cb(struct evconnlistener *listener, void *ctx){
	struct event_base *base = evconnlistener_get_base(listener);
	int err = EVUTIL_SOCKET_ERROR();
	fprintf(stderr, "[check point] Got an error %d (%s) on the listener. "
	    "Shutting down.\n", err, evutil_socket_error_to_string(err));
	event_base_loopexit(base, NULL);
}

//fake
int disconnect_inner(){
	printf("[check point] disconnect_inner has not been implemented.\n");
	return 0;
}
void unix_read_cb(struct bufferevent *bev, void *ctx){
	struct evbuffer *input = bufferevent_get_input(bev);
	struct evbuffer *output = bufferevent_get_output(bev);
	size_t len = evbuffer_get_length(input);
	char *data;
	data = malloc(len);
	evbuffer_copyout(input, data, len);
	printf("[check point] read data:#%s#\n",data);
	char* pos = strstr(data,UNIX_CMD);
	int ret=0;
	if (pos){ // got a command
		printf("[check point] I will call disconnct_inner().\n");
		ret=disconnect_inner();	
	}else{// error command
		ret=1;
	}
	if (0==ret){
		evbuffer_add_printf(output,"OK\n");	
	}else{ // error
		evbuffer_add_printf(output,"ERR\n");
	}	
}

void unix_event_cb(struct bufferevent *bev, short events, void *ctx){
    	if (events & BEV_EVENT_ERROR){
       		perror("[check point] Error from bufferevent");
  	}
    	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
		printf("[check point] client has quited.");
        	bufferevent_free(bev);
    	}
}

// When a unix socket accepts a new client, it will register two functions read and event.
void accept_conn_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *address, int socklen, void *ctx){
	printf("[check point] on accepted.\n");	
	struct event_base *base = evconnlistener_get_base(listener);
	// socket will be closed on free
	struct bufferevent *bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	// register two callback functions read and event
	bufferevent_setcb(bev, unix_read_cb, NULL, unix_event_cb, NULL);	
	bufferevent_enable(bev, EV_READ | EV_WRITE);
}


int start_unix_server_loop(){
	struct event_base *base;
	struct evconnlistener *listener;
	struct sockaddr_un sin;
	base = event_base_new();
	if (!base) {
        	printf("[check point] Couldn't open event base unix socket.\n");
        	return -1;
    	}
	memset(&sin, 0, sizeof(sin));
	sin.sun_family = AF_UNIX;
	strcpy(sin.sun_path, UNIX_SOCK_PATH);
	// remove socket first
	unlink(UNIX_SOCK_PATH);
	listener = evconnlistener_new_bind(base, accept_conn_cb, NULL,
			       LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, -1,
			       (struct sockaddr *) &sin, sizeof(sin));
	if (!listener){
		printf("[check point] Couldn't create unix listener.\n");
		return -2;
	}
	evconnlistener_set_error_cb(listener, accept_error_cb);
	printf("[check point] Unix socket will be ready to accept at %s\n",UNIX_SOCK_PATH);
	event_base_dispatch(base);
	return 0;
}

int main(){
	start_unix_server_loop();
	return 0;
}
