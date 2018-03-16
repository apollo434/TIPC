#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <linux/tipc.h>
#include <pthread.h>
#include <sched.h>
#define SERVER_TYPE  18888
#define SERVER_INST  17
#define MY_CPUS 8
#define MY_THS 8
#define BUFF_SIZE TIPC_MAX_USER_MSG_SIZE


void * server_th (void * arg) {
    
	struct sockaddr_tipc server_addr;
	struct sockaddr_tipc client_addr;
	socklen_t alen = sizeof(client_addr);
	int sd;
	char *inbuf;
	int ind = * (int *) arg;
	int rcv_size = 0;
	
	inbuf = (char *) malloc (BUFF_SIZE);
	if (! inbuf) {
	    printf ("allocated buff failed, %d (%s)\n", errno, strerror(errno));
	    return NULL;
	}

	printf("TIPC server program started with ind %d n\n", ind);
	server_addr.family = AF_TIPC;
	server_addr.addrtype = TIPC_ADDR_NAMESEQ;
	server_addr.addr.nameseq.type = SERVER_TYPE + ind;
	server_addr.addr.nameseq.lower = SERVER_INST;
	server_addr.addr.nameseq.upper = SERVER_INST;
	server_addr.scope = TIPC_ZONE_SCOPE;
	sd = socket(AF_TIPC, SOCK_RDM, 0);
	if (0 != bind(sd, (struct sockaddr *)&server_addr, sizeof(server_addr))) {
		printf("Server: failed to bind port name\n");
		return NULL;
	}
    
    while (1) {

    	if (0 >= ( rcv_size = recvfrom(sd, inbuf, BUFF_SIZE, 0,
    			  (struct sockaddr *)&client_addr, &alen)) ) {
    		perror("Server: unexpected message");
    	}
    
    	if (0 > sendto(sd, inbuf, rcv_size, 0,
    		       (struct sockaddr *)&client_addr, sizeof(client_addr))) {
    		perror("Server: failed to send");
    	}
    }
    printf("\nTIPC server finished with ind %d n", ind);
}



int main(void)
{
    int servers[MY_THS];
    pthread_t th_ids[MY_THS];
    int i = 0;
    cpu_set_t cpuset;
    FILE *fh = NULL;
    if ((fh = fopen ("/dev/cgroups/tasks", "w")) == NULL) {
        printf ("open error %d: %m\n", errno);

        if ((fh = fopen ("/sys/fs/cgroup/tasks", "w")) == NULL) {
            printf ("open 2 error %d: %m\n", errno);
        }
    }
    if (fh) {
    fprintf (fh, "%d\n", getpid());
    fclose(fh);
    }
    
    for (i = 0; i < MY_THS; ++i) {
        CPU_ZERO (& cpuset);
        CPU_SET (i % MY_CPUS, &cpuset);
        servers[i] = i;
        if (pthread_create (th_ids + i, NULL, server_th, servers + i) < 0) {
            printf ("create thread ind %d failed, error: %d (%s)\n", i, errno, strerror(errno));
            exit (0);
        }
        if (pthread_setaffinity_np (th_ids[i], sizeof (cpuset), &cpuset) < 0) {
            printf ("setaffinity_np failed\n");
        }
    }
    for (i = 0; i < MY_THS; ++i) {
        pthread_join (th_ids[i], NULL) ;
    }
	exit(0);
}
