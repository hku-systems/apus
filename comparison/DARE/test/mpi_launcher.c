/**                                                                                                      
 * DARE (Direct Access REplication)
 * 
 * Launch a set of applications from an MPI program 
 *
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 * 
 */
 
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

#include <mpi.h>

#define STRING_LENGTH 128
//#define DARE_PATH "/users/mpoke/key-value-stores/dare"
#define DARE_PATH "/mnt/SG/mpoke/repository/dare"

int main(int argc, char** argv) 
{
    int rc;
    int myrank, nprocs;
    char server_path[STRING_LENGTH],
         log_file[STRING_LENGTH],
         group_size[STRING_LENGTH],
         idx[STRING_LENGTH];   

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

    int dare_pid = fork();
    if (dare_pid == 0) {
        char hostname[64];
        gethostname(hostname, 64);
        printf("Hello from %s\n", hostname);
#if 1
        sprintf(server_path, "%s/bin/srv_test", DARE_PATH);
        sprintf(log_file, "./loggp_%d.log", myrank);
        sprintf(group_size, "%d", nprocs);
        sprintf(idx, "%d", myrank);

        printf("PATH=%s\n", server_path);

        rc = execl( server_path,
                    "srv_test",
                    "--loggp",
                    "-n",
                    hostname,
                    "-s",
                    group_size,
                    "-i",
                    idx,
                    "-l",
                    log_file,
                    (char *)0 );
        if (rc == -1) printf("Error: %s\n", strerror(errno));
#endif    
        return 0;
    }

    /* Wait for the kernel to finish */
    int status;
    waitpid(dare_pid, &status, 0);
    
    MPI_Finalize();    
}
