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
