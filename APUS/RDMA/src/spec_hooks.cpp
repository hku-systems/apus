#include <string>
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/stat.h>
#include "include/rsm-interface.h"

#define dprintf(fmt...)

struct event_manager_t* ev_mgr;

typedef int (*main_type)(int, char**, char**);

struct arg_type
{
	char **argv;
	int (*main_func) (int, char **, char **);
};

main_type saved_init_func = NULL;
void tern_init_func(int argc, char **argv, char **env)
{
	dprintf("%04d: __tern_init_func() called.\n", (int) pthread_self());
	if(saved_init_func)
		saved_init_func(argc, argv, env);

	printf("tern_init_func is called\n");
	char *config_path = getenv("cfg_path");

	char* log_dir = NULL;
	const char* id = getenv("node_id");
	uint8_t node_id = atoi(id);
	ev_mgr = NULL;
	ev_mgr = mgr_init(node_id, config_path, log_dir);
}

typedef void (*fini_type)(void*);
fini_type saved_fini_func = NULL;

extern "C" int my_main(int argc, char **pt, char **aa)
{
	int ret;
	arg_type *args = (arg_type*)pt;
	dprintf("%04d: __libc_start_main() called.\n", (int) pthread_self());
	ret = args->main_func(argc, args->argv, aa);
	return ret;
}

extern "C" int __libc_start_main(
	void *func_ptr,
	int argc,
	char* argv[],
	void (*init_func)(void),
	void (*fini_func)(void),
	void (*rtld_fini_func)(void),
	void *stack_end)
{
	typedef void (*fnptr_type)(void);
	typedef int (*orig_func_type)(void *, int, char *[], fnptr_type,
		fnptr_type, fnptr_type, void*);
	orig_func_type orig_func;
	arg_type args;

	void * handle;
	int ret;

	// Get lib path.
	Dl_info dli;
	dladdr((void *)dlsym, &dli);
	std::string libPath = dli.dli_fname;
	libPath = dli.dli_fname;
	size_t lastSlash = libPath.find_last_of("/");
	libPath = libPath.substr(0, lastSlash);
	libPath += "/libc.so.6";
	libPath = "/lib/x86_64-linux-gnu/libc.so.6";
	if(!(handle=dlopen(libPath.c_str(), RTLD_LAZY))) {
		puts("dlopen error");
		abort();
	}

	orig_func = (orig_func_type) dlsym(handle, "__libc_start_main");

	if(dlerror()) {
		puts("dlerror");
		abort();
	}

	dlclose(handle);

	dprintf("%04d: __libc_start_main is hooked.\n", (int) pthread_self());

	args.argv = argv;
	args.main_func = (main_type)func_ptr;
	saved_init_func = (main_type)init_func;

	saved_fini_func = (fini_type)rtld_fini_func;

	const char* target = "server";
	if (NULL != strstr(argv[0], target))
	{
		ret = orig_func((void*)my_main, argc, (char**)(&args), (fnptr_type)tern_init_func, (fnptr_type)fini_func, rtld_fini_func, stack_end);
	}else{
		ret = orig_func((void*)my_main, argc, (char**)(&args), (fnptr_type)saved_init_func, (fnptr_type)fini_func, rtld_fini_func, stack_end);
	}

	return ret;
}

extern "C" pid_t fork(void)
{
	typedef pid_t (*orig_fork_type)(void);
	static orig_fork_type orig_fork;
	if (!orig_fork)
		orig_fork = (orig_fork_type) dlsym(RTLD_NEXT, "fork");
	pid_t ret = orig_fork();
	if (ret == 0) // child process
	{
		fprintf(stderr, "creating internal threads\n");
		// create replica thread and libevent thread
		mgr_on_process_init(ev_mgr);
	} else {
		if (ret<0)
		{
			fprintf(stderr, "fork() failed\n");
			exit(1);
		}
		// parent process
	}
	return ret;
}



extern "C" int accept4(int socket, struct sockaddr *address, socklen_t *address_len, int flags)
{
	typedef int (*orig_accept4_type)(int, sockaddr *, socklen_t *,int);
	static orig_accept4_type orig_accept4;
	if (!orig_accept4)
		orig_accept4 = (orig_accept4_type) dlsym(RTLD_NEXT, "accept4");

	int ret = orig_accept4(socket, address, address_len, flags);

	if (ret >= 0 && ev_mgr != NULL)
	{
		struct stat sb;
		fstat(ret, &sb);
		if ((sb.st_mode & S_IFMT) == S_IFSOCK)
		{
			mgr_on_accept(ret, ev_mgr);
		}
	}

	return ret;
}

extern "C" int accept(int socket, struct sockaddr *address, socklen_t *address_len)
{
	typedef int (*orig_accept_type)(int, sockaddr *, socklen_t *);
	static orig_accept_type orig_accept;
	if (!orig_accept)
		orig_accept = (orig_accept_type) dlsym(RTLD_NEXT, "accept");

	/* Previously I think connect() can only successfully return only when the accept() on the server side is called.
	   That's why replica_on_accept() was invoked before orig_accept().
	   Howevrer, actually there is no relationship between connect() and accept() */

	int ret = orig_accept(socket, address, address_len);

	if (ret >= 0 && ev_mgr != NULL)
	{
		struct stat sb;
		fstat(ret, &sb);
		if ((sb.st_mode & S_IFMT) == S_IFSOCK)
		{
			mgr_on_accept(ret, ev_mgr);
		}
	}

	return ret;
}

extern "C" int close(int fildes)
{
	if (ev_mgr != NULL)
	{
		struct stat sb;
		fstat(fildes, &sb);
		if ((sb.st_mode & S_IFMT) == S_IFSOCK)
			mgr_on_close(fildes, ev_mgr);
	}

	typedef int (*orig_close_type)(int);
	static orig_close_type orig_close;
	if (!orig_close)
		orig_close = (orig_close_type) dlsym(RTLD_NEXT, "close");
	int ret = orig_close(fildes);
	return ret;
}

extern "C" ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
	typedef ssize_t (*orig_recv_type)(int, void *, size_t, int);
	static orig_recv_type orig_recv;
	if (!orig_recv)
		orig_recv = (orig_recv_type) dlsym(RTLD_NEXT, "recv");
	ssize_t ret = orig_recv(sockfd, buf, len, flags);

	if (ret > 0 && ev_mgr != NULL)
		server_side_on_read(ev_mgr, buf, ret, sockfd);

	return ret;
}

extern "C" ssize_t read(int fd, void *buf, size_t count)
{
	typedef ssize_t (*orig_read_type)(int, void *, size_t);
	static orig_read_type orig_read;
	if (!orig_read)
		orig_read = (orig_read_type) dlsym(RTLD_NEXT, "read");
	ssize_t ret = orig_read(fd, buf, count);

	if (ret > 0 && ev_mgr != NULL)
		server_side_on_read(ev_mgr, buf, ret, fd);

	return ret;
}


extern "C" ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
	typedef ssize_t (*orig_recvfrom_type)(int, void *, size_t, int, struct sockaddr*, socklen_t*);
	static orig_recvfrom_type orig_recvfrom;
	if (!orig_recvfrom)
		orig_recvfrom = (orig_recvfrom_type) dlsym(RTLD_NEXT, "recvfrom");
	
	ssize_t ret = orig_recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
	mgr_on_recvfrom(ev_mgr, buf, ret, src_addr);
	return ret;

}

extern "C" ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
        typedef ssize_t (*orig_recvmsg_type)(int, struct msghdr *, int);
        static orig_recvmsg_type orig_recvmsg;
        if (!orig_recvmsg)
                orig_recvmsg = (orig_recvmsg_type) dlsym(RTLD_NEXT, "recvmsg");
        ssize_t ret = orig_recvmsg(sockfd, msg, flags);
        if (ret > 0 && ev_mgr != NULL)
        	server_side_on_read(ev_mgr, msg->msg_iov[0].iov_base, ret, sockfd);
        
        return ret;

}

extern "C" ssize_t write(int fd, const void *buf, size_t count)
{
	typedef ssize_t (*orig_write_type)(int, const void *, size_t);
	static orig_write_type orig_write;
	if (!orig_write)
		orig_write = (orig_write_type) dlsym(RTLD_NEXT, "write");
	ssize_t ret = orig_write(fd, buf, count);

	if (ret > 0 && ev_mgr != NULL)
	{
		struct stat sb;
		fstat(fd, &sb);
		if ((sb.st_mode & S_IFMT) == S_IFSOCK)
		{
			// add by jingle
			// It is better to differ unix socket and tcp socket and here.
			// 1. to seperate based on whether libevent thread. [This check has been done in the mgr_on_check. Good]
			// 2. to seperate by calling getsockopt.
			dprintf("[TODO] fd %d will be passed into mgr_on_check. However open tcp socket is valid not unix socket is allowed.\n",fd);
			mgr_on_check(fd, buf, ret, ev_mgr);
		}
	}

	return ret;
}

extern "C" ssize_t send(int fd, const void *buf, size_t len, int flags)
{
	typedef ssize_t (*orig_send_type)(int, const void *, size_t, int);
	static orig_send_type orig_send;
	if (!orig_send)
		orig_send = (orig_send_type) dlsym(RTLD_NEXT, "send");
	ssize_t ret = orig_send(fd, buf, len, flags);

	if (ret > 0 && ev_mgr != NULL)
	{
		struct stat sb;
		fstat(fd, &sb);
		if ((sb.st_mode & S_IFMT) == S_IFSOCK)
		{
			mgr_on_check(fd, buf, ret, ev_mgr);
		}
	}

	return ret;
}
