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

A: Please refer to Q 10, there add key structure below:
```
/**
 *  struct socket - general BSD socket
 *  @state: socket state (%SS_CONNECTED, etc)
 *  @type: socket type (%SOCK_STREAM, etc)
 *  @flags: socket flags (%SOCK_ASYNC_NOSPACE, etc)
 *  @ops: protocol specific socket operations
 *  @file: File back pointer for gc
 *  @sk: internal networking protocol agnostic socket representation
 *  @wq: wait queue for several uses
 */
struct socket {
        socket_state            state;

        kmemcheck_bitfield_begin(type);
        short                   type;
        kmemcheck_bitfield_end(type);

        unsigned long           flags;

        struct socket_wq __rcu  *wq;

        struct file             *file;
        struct sock             *sk;
        const struct proto_ops  *ops;
};

(include/net/sock.h)
/**
  *     struct sock - network layer representation of sockets
  *     @__sk_common: shared layout with inet_timewait_sock
  *     @sk_shutdown: mask of %SEND_SHUTDOWN and/or %RCV_SHUTDOWN
  *     @sk_userlocks: %SO_SNDBUF and %SO_RCVBUF settings
  *     @sk_lock:       synchronizer
  *     @sk_rcvbuf: size of receive buffer in bytes
  *     @sk_wq: sock wait queue and async head
  *     @sk_rx_dst: receive input route used by early demux
  *     @sk_dst_cache: destination cache
  *     @sk_dst_lock: destination cache lock
  *     @sk_policy: flow policy
  *     @sk_receive_queue: incoming packets
  *     @sk_wmem_alloc: transmit queue bytes committed
  *     @sk_write_queue: Packet sending queue
  *     @sk_omem_alloc: "o" is "option" or "other"
  *     @sk_wmem_queued: persistent queue size
  *     @sk_forward_alloc: space allocated forward
  *     @sk_napi_id: id of the last napi context to receive data for sk
  *     @sk_ll_usec: usecs to busypoll when there is no data
  *     @sk_allocation: allocation mode
  *     @sk_pacing_rate: Pacing rate (if supported by transport/packet scheduler)
  *     @sk_max_pacing_rate: Maximum pacing rate (%SO_MAX_PACING_RATE)
  *     @sk_sndbuf: size of send buffer in bytes
  *     @sk_flags: %SO_LINGER (l_onoff), %SO_BROADCAST, %SO_KEEPALIVE,
  *                %SO_OOBINLINE settings, %SO_TIMESTAMPING settings
  *     @sk_no_check_tx: %SO_NO_CHECK setting, set checksum in TX packets
  *     @sk_no_check_rx: allow zero checksum in RX packets
  *     @sk_route_caps: route capabilities (e.g. %NETIF_F_TSO)
  *     @sk_route_nocaps: forbidden route capabilities (e.g NETIF_F_GSO_MASK)
  *     @sk_gso_type: GSO type (e.g. %SKB_GSO_TCPV4)
  *     @sk_gso_max_size: Maximum GSO segment size to build
  *     @sk_gso_max_segs: Maximum number of GSO segments
  *     @sk_lingertime: %SO_LINGER l_linger setting
  *     @sk_backlog: always used with the per-socket spinlock held
  *     @sk_callback_lock: used with the callbacks in the end of this struct
  *     @sk_error_queue: rarely used
  *     @sk_prot_creator: sk_prot of original sock creator (see ipv6_setsockopt,
  *                       IPV6_ADDRFORM for instance)
  *     @sk_err: last error
  *     @sk_err_soft: errors that don't cause failure but are the cause of a
  *                   persistent failure not just 'timed out'
  *     @sk_drops: raw/udp drops counter
  *     @sk_ack_backlog: current listen backlog
  *     @sk_max_ack_backlog: listen backlog set in listen()
  *     @sk_priority: %SO_PRIORITY setting
  *     @sk_cgrp_prioidx: socket group's priority map index
  *     @sk_type: socket type (%SOCK_STREAM, etc)
  *     @sk_protocol: which protocol this socket belongs in this network family
  *     @sk_peer_pid: &struct pid for this socket's peer
  *     @sk_peer_cred: %SO_PEERCRED setting
  *     @sk_rcvlowat: %SO_RCVLOWAT setting
  *     @sk_rcvtimeo: %SO_RCVTIMEO setting
  *     @sk_sndtimeo: %SO_SNDTIMEO setting
  *     @sk_rxhash: flow hash received from netif layer
  *     @sk_incoming_cpu: record cpu processing incoming packets
  *     @sk_txhash: computed flow hash for use on transmit
  *     @sk_filter: socket filtering instructions
  *     @sk_protinfo: private area, net family specific, when not using slab
  *     @sk_timer: sock cleanup timer
  *     @sk_stamp: time stamp of last packet received
  *     @sk_tsflags: SO_TIMESTAMPING socket options
  *     @sk_tskey: counter to disambiguate concurrent tstamp requests
  *     @sk_socket: Identd and reporting IO signals
  *     @sk_user_data: RPC layer private data
  *     @sk_frag: cached page frag
  *     @sk_peek_off: current peek_offset value
  *     @sk_send_head: front of stuff to transmit
  *     @sk_security: used by security modules
  *     @sk_mark: generic packet mark
  *     @sk_classid: this socket's cgroup classid
  *     @sk_cgrp: this socket's cgroup-specific proto data
  *     @sk_write_pending: a write to stream socket waits to start
  *     @sk_state_change: callback to indicate change in the state of the sock
  *     @sk_data_ready: callback to indicate there is data to be processed
  *     @sk_write_space: callback to indicate there is bf sending space available
  *     @sk_error_report: callback to indicate errors (e.g. %MSG_ERRQUEUE)
  *     @sk_backlog_rcv: callback to process the backlog
  *     @sk_destruct: called at sock freeing time, i.e. when all refcnt == 0
 */
struct sock {
        /*
         * Now struct inet_timewait_sock also uses sock_common, so please just
         * don't add nothing before this first member (__sk_common) --acme
         */
        struct sock_common      __sk_common;
#define sk_node                 __sk_common.skc_node
#define sk_nulls_node           __sk_common.skc_nulls_node
#define sk_refcnt               __sk_common.skc_refcnt
#define sk_tx_queue_mapping     __sk_common.skc_tx_queue_mapping

#define sk_dontcopy_begin       __sk_common.skc_dontcopy_begin
#define sk_dontcopy_end         __sk_common.skc_dontcopy_end
#define sk_hash                 __sk_common.skc_hash
#define sk_portpair             __sk_common.skc_portpair
#define sk_num                  __sk_common.skc_num
#define sk_dport                __sk_common.skc_dport
#define sk_addrpair             __sk_common.skc_addrpair
#define sk_daddr                __sk_common.skc_daddr
#define sk_rcv_saddr            __sk_common.skc_rcv_saddr
#define sk_family               __sk_common.skc_family
#define sk_state                __sk_common.skc_state
#define sk_reuse                __sk_common.skc_reuse
#define sk_reuseport            __sk_common.skc_reuseport
#define sk_ipv6only             __sk_common.skc_ipv6only
#define sk_bound_dev_if         __sk_common.skc_bound_dev_if
#define sk_bind_node            __sk_common.skc_bind_node
#define sk_prot                 __sk_common.skc_prot
#define sk_net                  __sk_common.skc_net
#define sk_v6_daddr             __sk_common.skc_v6_daddr
#define sk_v6_rcv_saddr __sk_common.skc_v6_rcv_saddr
#define sk_cookie               __sk_common.skc_cookie

        socket_lock_t           sk_lock;
        struct sk_buff_head     sk_receive_queue;
        /*
         * The backlog queue is special, it is always used with
         * the per-socket spinlock held and requires low latency
         * access. Therefore we special case it's implementation.
         * Note : rmem_alloc is in this structure to fill a hole
         * on 64bit arches, not because its logically part of
         * backlog.
         */
        struct {
                atomic_t        rmem_alloc;
                int             len;
                struct sk_buff  *head;
                struct sk_buff  *tail;
        } sk_backlog;
#define sk_rmem_alloc sk_backlog.rmem_alloc
        int                     sk_forward_alloc;
#ifdef CONFIG_RPS
        __u32                   sk_rxhash;
#endif
        u16                     sk_incoming_cpu;
        /* 16bit hole
         * Warned : sk_incoming_cpu can be set from softirq,
         * Do not use this hole without fully understanding possible issues.
         */

        __u32                   sk_txhash;
#ifdef CONFIG_NET_RX_BUSY_POLL
        unsigned int            sk_napi_id;
        unsigned int            sk_ll_usec;
#endif
        atomic_t                sk_drops;
        int                     sk_rcvbuf;

        struct sk_filter __rcu  *sk_filter;
        struct socket_wq __rcu  *sk_wq;

#ifdef CONFIG_XFRM
        struct xfrm_policy      *sk_policy[2];
#endif
        unsigned long           sk_flags;
        struct dst_entry        *sk_rx_dst;
        struct dst_entry __rcu  *sk_dst_cache;
        spinlock_t              sk_dst_lock;
        atomic_t                sk_wmem_alloc;
        atomic_t                sk_omem_alloc;
        int                     sk_sndbuf;
        struct sk_buff_head     sk_write_queue;
        kmemcheck_bitfield_begin(flags);
        unsigned int            sk_shutdown  : 2,
                                sk_no_check_tx : 1,
                                sk_no_check_rx : 1,
                                sk_userlocks : 4,
                                sk_protocol  : 8,
#define SK_PROTOCOL_MAX U8_MAX
                                sk_type      : 16;
        kmemcheck_bitfield_end(flags);
        int                     sk_wmem_queued;
        gfp_t                   sk_allocation;
        u32                     sk_pacing_rate; /* bytes per second */
        u32                     sk_max_pacing_rate;
        netdev_features_t       sk_route_caps;
        netdev_features_t       sk_route_nocaps;
        int                     sk_gso_type;
        unsigned int            sk_gso_max_size;
        u16                     sk_gso_max_segs;
        int                     sk_rcvlowat;
        unsigned long           sk_lingertime;
        struct sk_buff_head     sk_error_queue;
        struct proto            *sk_prot_creator;
        rwlock_t                sk_callback_lock;
        int                     sk_err,
                                sk_err_soft;
        u32                     sk_ack_backlog;
        u32                     sk_max_ack_backlog;
        __u32                   sk_priority;
#if IS_ENABLED(CONFIG_CGROUP_NET_PRIO)
        __u32                   sk_cgrp_prioidx;
#endif
        struct pid              *sk_peer_pid;
        const struct cred       *sk_peer_cred;
        long                    sk_rcvtimeo;
        long                    sk_sndtimeo;
        void                    *sk_protinfo;
        struct timer_list       sk_timer;
        ktime_t                 sk_stamp;tipc_family_ops
        u16                     sk_tsflags;
        u32                     sk_tskey;
        struct socket           *sk_socket;
        void                    *sk_user_data;
        struct page_frag        sk_frag;
        struct sk_buff          *sk_send_head;
        __s32                   sk_peek_off;
        int                     sk_write_pending;
#ifdef CONFIG_SECURITY
        void                    *sk_security;
#endif
        __u32                   sk_mark;
        u32                     sk_classid;
        struct cg_proto         *sk_cgrp;
        void                    (*sk_state_change)(struct sock *sk);
        void                    (*sk_data_ready)(struct sock *sk);
        void                    (*sk_write_space)(struct sock *sk);
        void                    (*sk_error_report)(struct sock *sk);
        int                     (*sk_backlog_rcv)(struct sock *sk,
                                                  struct sk_buff *skb);
        void                    (*sk_destruct)(struct sock *sk);
};


struct proto_ops {
        int             family;
        struct module   *owner;
        int             (*release)   (struct socket *sock);
        int             (*bind)      (struct socket *sock,
                                      struct sockaddr *myaddr,
                                      int sockaddr_len);
        int             (*connect)   (struct socket *sock,
                                      struct sockaddr *vaddr,
                                      int sockaddr_len, int flags);
        int             (*socketpair)(struct socket *sock1,
                                      struct socket *sock2);
        int             (*accept)    (struct socket *sock,
                                      struct socket *newsock, int flags);
        int             (*getname)   (struct socket *sock,
                                      struct sockaddr *addr,
                                      int *sockaddr_len, int peer);
        unsigned int    (*poll)      (struct file *file, struct socket *sock,
                                      struct poll_table_struct *wait);
        int             (*ioctl)     (struct socket *sock, unsigned int cmd,
                                      unsigned long arg);
#ifdef CONFIG_COMPAT
        int             (*compat_ioctl) (struct socket *sock, unsigned int cmd,
                                      unsigned long arg);
#endif
        int             (*listen)    (struct socket *sock, int len);
        int             (*shutdown)  (struct socket *sock, int flags);
        int             (*setsockopt)(struct socket *sock, int level,
                                      int optname, char __user *optval, unsigned int optlen);
        int             (*getsockopt)(struct socket *sock, int level,
                                      int optname, char __user *optval, int __user *optlen);
#ifdef CONFIG_COMPAT
        int             (*compat_setsockopt)(struct socket *sock, int level,
                                      int optname, char __user *optval, unsigned int optlen);
        int             (*compat_getsockopt)(struct socket *sock, int level,
                                      int optname, char __user *optval, int __user *optlen);
#endif
        int             (*sendmsg)   (struct socket *sock, struct msghdr *m,
                                      size_t total_len);
        /* Notes for implementing recvmsg:
         * ===============================
         * msg->msg_namelen should get updated by the recvmsg handlers
         * iff msg_name != NULL. It is by default 0 to prevent
         * returning uninitialized memory to user space.  The recvfrom
         * handlers can assume that msg.msg_name is either NULL or has
         * a minimum size of sizeof(struct sockaddr_storage).
         */
        int             (*recvmsg)   (struct socket *sock, struct msghdr *m,
                                      size_t total_len, int flags);
        int             (*mmap)      (struct file *file, struct socket *sock,
                                      struct vm_area_struct * vma);
        ssize_t         (*sendpage)  (struct socket *sock, struct page *page,
                                      int offset, size_t size, int flags);
        ssize_t         (*splice_read)(struct socket *sock,  loff_t *ppos,
                                       struct pipe_inode_info *pipe, size_t len, unsigned int flags);
        int             (*set_peek_off)(struct sock *sk, int val);
};

```

A: The relationship of layout of socket:

```
struct socket {}
----------------
				|
				V
struct sock {}
----------------

```

2. Why need to create a socket?
3. What's the meaning?
4. Why need to bind a socket with a port or port name(TIPC)?
A: For matching receiving packets.
5. TIPC 不存在socket 的未决链接？ Why?
A: 存在，当使用流socket时 是存在的
![Alt text](/pic/socket_no_define.png)
6. 数据报socket上使用connect()的作用？
7. socket套接字描述符和文件描述符是否一致？
A: 套接字描述和文件描述符在linux下是一样的，其实我就是想问当进程有套接字描述符和打开的文件描述符是否都是在那个集里面。当然测试后，其实都在同一个集合里面。
8. socket的fd也是在每进程中存在吗？即fd依附于进程控制块？
A: Yes
9. How to complete "syscall"?
10. What's the call chain of socket?
A:

```
(net/socket.c)
SYSCALL_DEFINE2(socketcall, int, call, unsigned long __user *, args)
	sys_socket
		|
		|---> SYSCALL_DEFINE3(socket, int, family, int, type, int, protocol)
						sock_create
							__sock_create
								sock_alloc /*create inode in vfs*/
								pf = rcu_dereference(net_families[family]) /* find the net_families based on family type*/
>>>>
static const struct net_proto_family __rcu *net_families[NPROTO] __read_mostly;
>>>>								
								err = pf->create(net, sock, protocol, kern); /* create the socket based on net_families[family] */								
						sock_map_fd
							sock_alloc_file /* allocate file and fd */
							fd_install /* Let fd point to file */
```
![Alt text](/pic/socket_chain.png)

11. Does TIPC has MAC address concept?

12. What's the relationship between process and socket?
A:

![Alt text](/pic/pft_socket.png)

13. How to init protocol family?
A:

1) Create the socket in VFS
```
(net/socket.c)
static struct file_system_type sock_fs_type = {
        .name =         "sockfs",
        .mount =        sockfs_mount,
        .kill_sb =      kill_anon_super,
};


static int __init sock_init(void)
{
        int err;
        /*
         *      Initialize the network sysctl infrastructure.
         */
        err = net_sysctl_init();
        if (err)
                goto out;

        /*
         *      Initialize skbuff SLAB cache
         */
        skb_init();

        /*
         *      Initialize the protocols module.
         */

        init_inodecache();

        err = register_filesystem(&sock_fs_type);
        if (err)
                goto out_fs;
        sock_mnt = kern_mount(&sock_fs_type);
        if (IS_ERR(sock_mnt)) {
                err = PTR_ERR(sock_mnt);
                goto out_mount;
        }

        /* The real protocol initialization is performed in later initcalls.
         */

#ifdef CONFIG_NETFILTER
        err = netfilter_init();
        if (err)
                goto out;
#endif

        ptp_classifier_init();

out:
        return err;

out_mount:
        unregister_filesystem(&sock_fs_type);
out_fs:
        goto out;
}
```
2) How to register the net families

```
/*
 * Define the net families
 *
 */

static const struct net_proto_family __rcu *net_families[NPROTO] __read_mostly;

/**
 *      sock_register - add a socket protocol handler
 *      @ops: description of protocol
 *              
 *      This function is called by a protocol handler that wants to
 *      advertise its address family, and have it linked into the
 *      socket interface. The value ops->family corresponds to the
 *      socket system call protocol family.
 */     
int sock_register(const struct net_proto_family *ops)
{
        int err;

        if (ops->family >= NPROTO) {
                pr_crit("protocol %d >= NPROTO(%d)\n", ops->family, NPROTO);
                return -ENOBUFS;
        }

        spin_lock(&net_family_lock);
        if (rcu_dereference_protected(net_families[ops->family],
                                      lockdep_is_held(&net_family_lock)))
                err = -EEXIST;
        else {
                rcu_assign_pointer(net_families[ops->family], ops);
                err = 0;
        }
        spin_unlock(&net_family_lock);

        pr_info("NET: Registered protocol family %d\n", ops->family);
        return err;
}
EXPORT_SYMBOL(sock_register);

```
3) Take TIPC as an example, TIPC Init
```
(net/tipc/socket.c)

static const struct net_proto_family tipc_family_ops = {
        .owner          = THIS_MODULE,
        .family         = AF_TIPC,
        .create         = tipc_sk_create
};


/**
 * tipc_socket_init - initialize TIPC socket interface
 *
 * Returns 0 on success, errno otherwise
 */
int tipc_socket_init(void)
{
        int res;

        res = proto_register(&tipc_proto, 1);
        if (res) {
                pr_err("Failed to register TIPC protocol type\n");
                goto out;
        }

        res = sock_register(&tipc_family_ops);
        if (res) {
                pr_err("Failed to register TIPC socket type\n");
                proto_unregister(&tipc_proto);
                goto out;
        }
 out:
        return res;
}




(net/tipc/core.c)
static int __init tipc_init(void)
{
        int err;

        pr_info("Activated (version " TIPC_MOD_VER ")\n");

        sysctl_tipc_rmem[0] = TIPC_CONN_OVERLOAD_LIMIT >> 4 <<
                              TIPC_LOW_IMPORTANCE;
        sysctl_tipc_rmem[1] = TIPC_CONN_OVERLOAD_LIMIT >> 4 <<
                              TIPC_CRITICAL_IMPORTANCE;
        sysctl_tipc_rmem[2] = TIPC_CONN_OVERLOAD_LIMIT;

        err = tipc_netlink_start();
        if (err)
                goto out_netlink;

        err = tipc_netlink_compat_start();
        if (err)
                goto out_netlink_compat;

        err = tipc_socket_init();
        if (err)
                goto out_socket;

        err = tipc_register_sysctl();
        if (err)
                goto out_sysctl;

        err = register_pernet_subsys(&tipc_net_ops);
        if (err)
                goto out_pernet;

        err = tipc_bearer_setup();
        if (err)
                goto out_bearer;

        pr_info("Started in single node mode\n");
        return 0;
out_bearer:
        unregister_pernet_subsys(&tipc_net_ops);
out_pernet:
        tipc_unregister_sysctl();
out_sysctl:
        tipc_socket_stop();
out_socket:
        tipc_netlink_compat_stop();
out_netlink_compat:
        tipc_netlink_stop();
out_netlink:
        pr_err("Unable to start in single node mode\n");
        return err;
}


```
4) Take ipv4 as an example, ipv4 Init
```
(net/ipv4/af_inet.c)
static const struct net_proto_family inet_family_ops = {
        .family = PF_INET,
        .create = inet_create,
        .owner  = THIS_MODULE,
};

static int __init inet_init(void)
{
        struct inet_protosw *q;
        struct list_head *r;
        int rc = -EINVAL;

        sock_skb_cb_check_size(sizeof(struct inet_skb_parm));

        rc = proto_register(&tcp_prot, 1);
        if (rc)
                goto out;

        rc = proto_register(&udp_prot, 1);
        if (rc)
                goto out_unregister_tcp_proto;

        rc = proto_register(&raw_prot, 1);
        if (rc)
                goto out_unregister_udp_proto;

        rc = proto_register(&ping_prot, 1);
        if (rc)
                goto out_unregister_raw_proto;

        /*      
         *      Tell SOCKET that we are alive...
         */

        (void)sock_register(&inet_family_ops);

#ifdef CONFIG_SYSCTL
        ip_static_sysctl_init();
#endif

        /*
         *      Add all the base protocols.
         */

        if (inet_add_protocol(&icmp_protocol, IPPROTO_ICMP) < 0)
                pr_crit("%s: Cannot add ICMP protocol\n", __func__);
        if (inet_add_protocol(&udp_protocol, IPPROTO_UDP) < 0)
                pr_crit("%s: Cannot add UDP protocol\n", __func__);
        if (inet_add_protocol(&tcp_protocol, IPPROTO_TCP) < 0)
                pr_crit("%s: Cannot add TCP protocol\n", __func__);
#ifdef CONFIG_IP_MULTICAST
        if (inet_add_protocol(&igmp_protocol, IPPROTO_IGMP) < 0)
                pr_crit("%s: Cannot add IGMP protocol\n", __func__);
        if (inet_add_protocol(&igmp_protocol, IPPROTO_IGMP) < 0)
                pr_crit("%s: Cannot add IGMP protocol\n", __func__);
#endif

        /* Register the socket-side information for inet_create. */
        for (r = &inetsw[0]; r < &inetsw[SOCK_MAX]; ++r)
                INIT_LIST_HEAD(r);

        for (q = inetsw_array; q < &inetsw_array[INETSW_ARRAY_LEN]; ++q)
                inet_register_protosw(q);

        /*
         *      Set the ARP module up
         */

        arp_init();

        /*
         *      Set the IP module up
         */

        ip_init();

        tcp_v4_init();

        /* Setup TCP slab cache for open requests. */
        tcp_init();

        /* Setup UDP memory threshold */
        udp_init();

        /* Add UDP-Lite (RFC 3828) */
        udplite4_register();

        ping_init();

        /*
         *      Set the ICMP layer up
         */

        if (icmp_init() < 0)
                panic("Failed to create the ICMP control socket.\n");

        /*
         *      Initialise the multicast router
         */
#if defined(CONFIG_IP_MROUTE)
        if (ip_mr_init())
                pr_crit("%s: Cannot init ipv4 mroute\n", __func__);
#endif
        if (init_inet_pernet_ops())
                pr_crit("%s: Cannot init ipv4 inet pernet ops\n", __func__);
        /*
         *      Initialise per-cpu ipv4 mibs
         */

        if (init_ipv4_mibs())
                pr_crit("%s: Cannot init ipv4 mibs\n", __func__);

        ipv4_proc_init();

        ipfrag_init();

        dev_add_pack(&ip_packet_type);

        rc = 0;
out:
        return rc;
out_unregister_raw_proto:
        proto_unregister(&raw_prot);
out_unregister_udp_proto:
        proto_unregister(&udp_prot);
out_unregister_tcp_proto:
        proto_unregister(&tcp_prot);
        goto out;
}

fs_initcall(inet_init);


```
14. Regarding bear in TIPC

```
(net/tipc/core.c)
tipc_init
	tipc_bearer_setup
		dev_add_pack(&tipc_packet_type)

static struct packet_type tipc_packet_type __read_mostly = {
        .type = htons(ETH_P_TIPC),
        .func = tipc_l2_rcv_msg,
};

/**
 * tipc_l2_rcv_msg - handle incoming TIPC message from an interface
 * @buf: the received packet
 * @dev: the net device that the packet was received on
 * @pt: the packet_type structure which was used to register this handler
 * @orig_dev: the original receive net device in case the device is a bond
 *
 * Accept only packets explicitly sent to this node, or broadcast packets;
 * ignores packets sent using interface multicast, and traffic sent to other
 * nodes (which can happen if interface is running in promiscuous mode).
 */
static int tipc_l2_rcv_msg(struct sk_buff *buf, struct net_device *dev,
                           struct packet_type *pt, struct net_device *orig_dev)
{
        struct tipc_bearer *b_ptr;

        rcu_read_lock();
        b_ptr = rcu_dereference_rtnl(dev->tipc_ptr);
        if (likely(b_ptr)) {
                if (likely(buf->pkt_type <= PACKET_BROADCAST)) {
                        buf->next = NULL;
                        tipc_rcv(dev_net(dev), buf, b_ptr);
                        rcu_read_unlock();
                        return NET_RX_SUCCESS;
                }
        }
        rcu_read_unlock();

        kfree_skb(buf);
        return NET_RX_DROP;
}

The tipc_rcv() is the entry into tipc protocol stack from l2.

/**
 * tipc_l2_send_msg - send a TIPC packet out over an L2 interface
 * @buf: the packet to be sent
 * @b_ptr: the bearer through which the packet is to be sent
 * @dest: peer destination address
 */
int tipc_l2_send_msg(struct net *net, struct sk_buff *buf,
                     struct tipc_bearer *b, struct tipc_media_addr *dest)
{
        struct sk_buff *clone;
        struct net_device *dev;
        int delta;

        dev = (struct net_device *)rcu_dereference_rtnl(b->media_ptr);
        if (!dev)
                return 0;

        clone = skb_clone(buf, GFP_ATOMIC);
        if (!clone)
                return 0;

        delta = dev->hard_header_len - skb_headroom(buf);
        if ((delta > 0) &&
            pskb_expand_head(clone, SKB_DATA_ALIGN(delta), 0, GFP_ATOMIC)) {
                kfree_skb(clone);
                return 0;
        }    

        skb_reset_network_header(clone);
        clone->dev = dev;
        clone->protocol = htons(ETH_P_TIPC);
        dev_hard_header(clone, dev, ETH_P_TIPC, dest->value,
                        dev->dev_addr, clone->len);
        dev_queue_xmit(clone);
        return 0;
}

```

15. Regarding neighbor discovery, how and what to do about it?

A:

In tipc protocol:
When a node is started it must make the rest of the cluster aware of its existence,
and itself learn the topology of the cluster. Once a neighbouring node has been
detected on a bearer, a signalling link is established towards it.

Thus, when and how does one node let other ones to know itself.

tipc_enable_bearer() will create a tipc_bearer and broadcast its existence.

```
(net/tipc/bearer.c)

static int tipc_enable_bearer(struct net *net, const char *name,
			      u32 disc_domain, u32 priority,
			      struct nlattr *attr[])
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_bearer *b_ptr;
	struct tipc_media *m_ptr;
	>>>>>
	m_ptr = tipc_media_find(b_names.media_name);
>>>>>>>>>>>>>
The possible medias.

static struct tipc_media * const media_info_array[] = {
	&eth_media_info,
#ifdef CONFIG_TIPC_MEDIA_IB
	&ib_media_info,
#endif
#ifdef CONFIG_TIPC_MEDIA_UDP
	&udp_media_info,
#endif
	NULL
};
>>>>>>>>>>>>>>
	>>>>>//create a tipc_bear
	b_ptr = kzalloc(sizeof(*b_ptr), GFP_ATOMIC);
	if (!b_ptr)
		return -ENOMEM;

	strcpy(b_ptr->name, name);
	b_ptr->media = m_ptr;
	res = m_ptr->enable_media(net, b_ptr, attr);
	if (res) {
		pr_warn("Bearer <%s> rejected, enable failure (%d)\n",
			name, -res);
		return -EINVAL;
	}

	b_ptr->identity = bearer_id;
	b_ptr->tolerance = m_ptr->tolerance;
	b_ptr->window = m_ptr->window;
	b_ptr->domain = disc_domain;
	b_ptr->net_plane = bearer_id + 'A';
	b_ptr->priority = priority;

	//broadcast its existence
	res = tipc_disc_create(net, b_ptr, &b_ptr->bcast_addr);
	if (res) {
		bearer_disable(net, b_ptr, false);
		pr_warn("Bearer <%s> rejected, discovery object creation failed\n",
			name);
		return -EINVAL;
	}

	rcu_assign_pointer(tn->bearer_list[bearer_id], b_ptr);

	pr_info("Enabled bearer <%s>, discovery domain %s, priority %u\n",
		name,
		tipc_addr_string_fill(addr_string, disc_domain), priority);
	return res;
}

```

16. Regarding inquire and subscrib, how to?

```
From the tipc protocol:
TIPC also provides a mechanism for inquiring or subscribing for the availability
of port names or ranges of port names.

And from the tipc demo:

void wait_for_server(struct tipc_name* name,int wait)
{
        struct sockaddr_tipc topsrv;
        struct tipc_subscr subscr = {{name->type,name->instance,name->instance},
                                     wait,TIPC_SUB_SERVICE,{}};
        struct tipc_event event;

        int sd = socket (AF_TIPC, SOCK_SEQPACKET,0);
        assert(sd > 0);

        memset(&topsrv,0,sizeof(topsrv));
		topsrv.family = AF_TIPC;
        topsrv.addrtype = TIPC_ADDR_NAME;
        topsrv.addr.name.name.type = TIPC_TOP_SRV;
        topsrv.addr.name.name.instance = TIPC_TOP_SRV;

        /* Connect to topology server: */

        if (0 > connect(sd,(struct sockaddr*)&topsrv,sizeof(topsrv))){
                perror("failed to connect to topology server");
                exit(1);
        }
        if (send(sd,&subscr,sizeof(subscr),0) != sizeof(subscr)){
                perror("failed to send subscription");
                exit(1);
        }
        /* Now wait for the subscription to fire: */
        if (recv(sd,&event,sizeof(event),0) != sizeof(event)){
                perrork("Failed to receive event");
                exit(1);
        }
        if (event.event != TIPC_PUBLISHED){
                printf("Server %u,%u not published within %u [s]\n",
                       name->type,name->instance,wait/1000);
                exit(1);
        }
        close(sd);
}
```
17. This function illustrate how to inquire.
Thus, where is TIPC_TOP_SRV?

```
(include/uapi/linux/tipc.h)
#define TIPC_TOP_SRV		1	/* topology service name type */

/*
 * Application-accessible port name types
 */

#define TIPC_CFG_SRV            0       /* configuration service name type */
#define TIPC_TOP_SRV            1       /* topology service name type */
#define TIPC_LINK_STATE         2       /* link state name type */
#define TIPC_RESERVED_TYPES     64      /* lowest user-publishable name type */

tipc_init_net() -> tipc_subsrc_start()

tipc_init_net
	tipc_subsrc_start
		tipc_server_start
			tipc_open_listening_sock
				tipc_register_callbacks
					sk->sk_data_ready = sock_data_ready()
					sk->sk_write_space = sock_write_space()

A important idea of current tipc server version is:
It use sk_data_ready callback to queue a work, then transfer the its work from BH context to process context.

```
