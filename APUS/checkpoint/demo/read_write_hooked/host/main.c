#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#define BUFF_SIZE 512
int main(int argc, char ** argv){
    char *fname = "test.log";
    int fd = open(fname,O_RDWR|O_TRUNC|O_CREAT,0664);
    if (-1 == fd){
        printf("open %s failed for writing\n",fname);
        exit(1); 
    }
    char* buff="This is a test log\n";
    int buff_size = strlen(buff);
    int nwrite = write(fd,buff,buff_size);
    printf("[write_API] write %d bytes\n",nwrite);
    fsync(fd);
    close(fd);
    //
    fd = open(fname,O_RDONLY);
    if (-1 == fd){
        printf("open %s failed\n",fname);
        exit(1);
    }
    char buff_in[BUFF_SIZE];
    int nread = read(fd,buff_in,BUFF_SIZE-1);
    buff_in[nread]=0;
    printf("[read_API] %s\n",buff_in);
    return 0;
}
