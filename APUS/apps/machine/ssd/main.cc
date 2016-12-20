#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include <time.h>
#include <stdint.h>

#define ALIGN	512
#define BLOCK_SIZE 1024

#define BILLION 1000000000L

#define TEST_FILE "test.txt"

#define MEASURE_LATENCY

void init_buff(char *buff){
	for (int i=0;i<BLOCK_SIZE;i++){
		buff[i]='a'+i%26; // a-z
	}
	buff[BLOCK_SIZE-1]=0;
	//printf("ctx:%s\n",buff);
}
// O_DSYNC Write I/O operations on the file descriptor shall complete as defined by synchronized I/O data integrity completion. 
// O_SYNC  Write I/O operations on the file descriptor shall complete as defined by synchronized I/O file integrity completion. 
// (all file attributes relative to the I/O operation)
// If both the O_SYNC and O_DSYNC flags are set, the effect is as if only the O_SYNC flag was set. 
// O_DIRECT To guarantee synchronous I/O, O_SYNC must be used in addition to O_DIRECT
// A successful return from write() does not make any guarantee that data has been committed to disk.
int main(int argc, char** argv){
	char * buff = (char*)memalign(ALIGN,BLOCK_SIZE);
	init_buff(buff);	
	int open_opt_array[] ={
		O_RDWR|O_TRUNC|O_CREAT,
		O_RDWR|O_TRUNC|O_CREAT|O_DIRECT,
		O_RDWR|O_TRUNC|O_CREAT|O_DIRECT|O_DSYNC,
		O_RDWR|O_TRUNC|O_CREAT|O_DIRECT|O_SYNC,
		O_RDWR|O_APPEND|O_CREAT,
		O_RDWR|O_APPEND|O_CREAT|O_DIRECT,
		0
	};
	int open_opt=0;
	if (argc==2){
		int mode_index = atoi(argv[1]);
		printf("chosed mode:%d\n",mode_index);
		open_opt = open_opt_array[mode_index];
	}else{
		printf("%s mode\n",argv[0]);
		exit(5);
	}
	int fd = open(TEST_FILE, open_opt);
	if (-1 == fd){
		perror("open failed.");
		exit(1);
	}

	struct timespec start, end;
	uint64_t diff;
	FILE *fp = fopen("latency.txt", "wb");

	int test_cnt = 1;
	int loop_size = 100000;
	for (int t=0;t<test_cnt;t++){
		int retcode = ftruncate(fd,0);
		if (retcode == -1){
			perror("ftruncate error.");
			exit(5);
		}
		for (int i=0;i<loop_size;i++){
#ifdef MEASURE_LATENCY
			clock_gettime(CLOCK_MONOTONIC, &start);
#endif
			int nwrite = write(fd,buff,BLOCK_SIZE);
			if (-1==nwrite){
				perror("write error.");
				exit(3);
			}else if (BLOCK_SIZE!=nwrite){
				perror("write is not enough.");
				exit(4);
			}
#ifdef MEASURE_LATENCY
                        clock_gettime(CLOCK_MONOTONIC, &end);
			diff = BILLION * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
			fprintf(fp, "%llu\n", (long long unsigned int) diff);
#endif
		}		
	}
	printf("Game Over. I did %d times write. And block size is %d\n",test_cnt*loop_size, BLOCK_SIZE);
	return 0;
}
