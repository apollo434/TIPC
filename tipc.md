Question:
1. What's the neighbor discovery machnism?
2. What's the flow of the "server" and "client" connected with each other?

Answer:
1.
2. Take an example below:

**Server.c**

```
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

```

**Client.c**
```
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <linux/tipc.h>
#include <pthread.h>
#include <errno.h>
#define SERVER_TYPE  18888
#define SERVER_INST  17
#define MY_CPUS 8
#define MY_THS 8
#define MY_CLTS 8
#define BUFF_SIZE TIPC_MAX_USER_MSG_SIZE
#include <sched.h>
struct sockaddr_tipc All_Server[MY_THS];
int Client_Sock[MY_CLTS];
int Stats[MY_CLTS][2];
unsigned char * Buff;

void wait_for_server(__u32 name_type, __u32 name_instance, int wait)
{
	struct sockaddr_tipc topsrv;
	struct tipc_subscr subscr;
	struct tipc_event event;

	int sd = socket(AF_TIPC, SOCK_SEQPACKET, 0);

	memset(&topsrv, 0, sizeof(topsrv));
	topsrv.family = AF_TIPC;
	topsrv.addrtype = TIPC_ADDR_NAME;
	topsrv.addr.name.name.type = TIPC_TOP_SRV;
	topsrv.addr.name.name.instance = TIPC_TOP_SRV;

	/* Connect to topology server */

	if (0 > connect(sd, (struct sockaddr *)&topsrv, sizeof(topsrv))) {
		perror("Client: failed to connect to topology server");
		exit(1);
	}

	subscr.seq.type = htonl(name_type);
	subscr.seq.lower = htonl(name_instance);
	subscr.seq.upper = htonl(name_instance);
	subscr.timeout = htonl(wait);
	subscr.filter = htonl(TIPC_SUB_SERVICE);

	if (send(sd, &subscr, sizeof(subscr), 0) != sizeof(subscr)) {
		perror("Client: failed to send subscription");
		exit(1);
	}
	/* Now wait for the subscription to fire */

	if (recv(sd, &event, sizeof(event), 0) != sizeof(event)) {
		perror("Client: failed to receive event");
		exit(1);
	}
	if (event.event != htonl(TIPC_PUBLISHED)) {
		printf("Client: server {%u,%u} not published within %u [s]\n",
		       name_type, name_instance, wait/1000);
		exit(1);
	}

	close(sd);
}


void set_tipc_addr (struct sockaddr_tipc *server_addr, __u32 type, __u32 instance) {
	server_addr->family = AF_TIPC;
	server_addr->addrtype = TIPC_ADDR_NAME;
	server_addr->addr.name.name.type = type;
	server_addr->addr.name.name.instance = instance;
	server_addr->addr.name.domain = 0;


}

void wait_for_all_server (void) {

    int i = 0;

    for (i = 0; i < MY_THS; ++i) {
        wait_for_server(SERVER_TYPE + i, SERVER_INST, 10000);
        set_tipc_addr (All_Server + i, SERVER_TYPE + i, SERVER_INST);
    }

}


void * client_sent_th (void *args) {

    int ind = * (int *) args;

    int sock = Client_Sock[ind];
    struct sockaddr* server_addr;

    int *stats =  &Stats[ind][0];
    int pkt_size = 64;
    int sid = 0;

    printf("****** TIPC client send program started ind %d ******\n\n", ind);

    while (1) {
        for (pkt_size = 64; pkt_size < BUFF_SIZE; pkt_size<<=1) {

            for (sid = 0; sid < MY_THS; ++sid) {
                server_addr = (struct sockaddr* ) (All_Server + sid);
            	if (0 > sendto(sock, Buff, pkt_size, 0,
            		       (struct sockaddr*) server_addr, sizeof(struct sockaddr))) {
            		perror("Client: failed to send");
  //          		usleep (1000);
            	}
            	else {
            	    (*stats) ++;
            	}

            }
        }
//        usleep (1000000);
    }

    printf("****** TIPC client send program finished ind %d, %d ****** \n\n", ind, *stats);

    return NULL;

}

void * client_rcv_th (void *args)  {

    int ind = * (int *) args;
    int sock = Client_Sock[ind];
    int *stats =  &Stats[ind][0];

    unsigned char *buff = (unsigned char *) malloc (BUFF_SIZE);

    if (! buff) {
        printf ("malloc error: %d, %s\n", errno, strerror(errno));
        return NULL;
    }

    printf("****** TIPC client recv program started ind %d ******\n\n", ind);

    while (1) {
        if (0 >= recv(sock, buff, BUFF_SIZE, 0)) {
    		perror("Client: unexpected response");
	    }
	    else {
	        (*stats) ++;
	    }
    }

    printf("\n****** TIPC client recv finished ind %d, %d ******\n", ind, *stats);

    return NULL;
}



int main(void)
{
    int i = 0;
    int clients[MY_CLTS];
    pthread_t s_th_ids[MY_CLTS];
    pthread_t r_th_ids[MY_CLTS];
    int j = 0;
    int sd;
    cpu_set_t cpuset;

    wait_for_all_server();
    Buff = (unsigned char *) malloc (BUFF_SIZE);
    if (! Buff) {
        printf ("malloc error: %d, %s\n", errno, strerror(errno));
        return 0;
    }
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
    for (j = 0; j < MY_CLTS; ++j) {
        sd = socket(AF_TIPC, SOCK_RDM, 0);
        if (sd < 0) {
            printf ("create sock failed : %d, %s\n", errno, strerror(errno));
            return 0;
        }

        Client_Sock[j] = sd;
    }

    for (i = 0; i < MY_THS; ++i) {
        clients[i] = i;
        if (pthread_create (s_th_ids + i, NULL, client_sent_th, clients + i) < 0) {
            printf ("create thread ind %d failed, error: %d (%s)\n", i, errno, strerror(errno));
            exit (0);
        }
        CPU_ZERO (& cpuset);
        CPU_SET (i % MY_CPUS, &cpuset);
        if (pthread_setaffinity_np (s_th_ids[i], sizeof (cpuset), &cpuset) < 0) {
            printf ("setaffinity_np failed\n");
        }
        if (pthread_create (r_th_ids + i, NULL, client_rcv_th, clients + i) < 0) {
            printf ("create thread ind %d failed, error: %d (%s)\n", i, errno, strerror(errno));
            exit (0);
        }
        CPU_ZERO (& cpuset);
        CPU_SET (i % MY_CPUS, &cpuset);
        if (pthread_setaffinity_np (r_th_ids[i], sizeof (cpuset), &cpuset) < 0) {
            printf ("setaffinity_np failed\n");
        }

    }
    for (i = 0; i < MY_THS; ++i) {
        pthread_join (s_th_ids[i], NULL) ;
        pthread_join (r_th_ids[i], NULL) ;
    }
	exit(0);
}

```

Q:
1. What's the things the socket created?
2. Why need to create a socket?
3. What's the meaning?
4. Why need to bind a socket with a port or port name(TIPC)?
5. TIPC 不存在socket 的未决链接？ Why? 存在，当使用流socket时 是存在的
![Alt text](/pic/socket_no_define.png)
6. 数据报socket上使用connect()的作用？
7. socket套接字描述符和文件描述符是否一致？
套接字描述和文件描述符在linux下是一样的，其实我就是想问当进程有套接字描述符和打开的文件描述符是否都是在那个集里面。当然测试后，其实都在同一个集合里面。 
8. socket的fd也是在每进程中存在吗？即fd依附于进程控制块？
9. 
