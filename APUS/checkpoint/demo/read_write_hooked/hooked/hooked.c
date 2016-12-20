#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h> 
#include <dlfcn.h>

ssize_t read(int fd, void *buf, size_t count){
static ssize_t (* orig_read)(int fd, void *buf, size_t count)= NULL; 
if (NULL==orig_read){
    orig_read = dlsym(RTLD_NEXT,"read");
}
ssize_t nread = orig_read(fd,buf,count);
if (fd>=3){
    printf("[read_hooked] read %ld bytes, orig_read=%p\n",nread,orig_read);
}
return nread;
}

ssize_t write(int fd, const void *buf, size_t count){
static ssize_t (* orig_write)(int fd,const void *buf, size_t count)=NULL;
if (NULL==orig_write){
    orig_write = dlsym(RTLD_NEXT,"write");
}
ssize_t nwrite = orig_write(fd,buf,count);
if (fd>=3){
    printf("[write_hooked] write %ld bytes, orig_write=%p\n",nwrite,orig_write);
}
return nwrite;
}
