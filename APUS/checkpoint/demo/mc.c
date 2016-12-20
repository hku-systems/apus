/*
 * Receiver (-m is the multicast address, often the IP of the receiver):
 * ./mc -m 192.168.1.12
 *
 * Sender (-m is the multicast address, often the IP of the receiver):
 * ./mc -s -m 192.168.1.12
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <rdma/rdma_verbs.h>
#define VERB_ERR( verb, ret ) \
	fprintf( stderr, "%s returned %d errno %d\n", verb, ret, errno )
/* Default parameter values */
#define DEFAULT_PORT		"51216"
#define DEFAULT_MSG_COUNT	4
#define DEFAULT_MSG_LENGTH	64
/* Resources used in the example */
struct context
{
/* User parameters */
	int	sender;
	char	*bind_addr;
	char	*mcast_addr;
	char	*server_port;
	int	msg_count;
	int	msg_length;
/* Resources */
	struct sockaddr			mcast_sockaddr;
	struct rdma_cm_id		*id;
	struct rdma_event_channel	*channel;
	struct ibv_pd			*pd;
	struct ibv_cq			*cq;
	struct ibv_mr			*mr;
	char				*buf;
	struct ibv_ah			*ah;
	uint32_t			remote_qpn;
	uint32_t			remote_qkey;
	pthread_t			cm_thread;
};


/*
 * Function: cm_thread
 *
 * Input:
 * arg The context object
 *
 * Output:
 * none
 *
 * Returns:
 * NULL
 *
 * Description:
 * Reads any CM events that occur during the sending of data
 * and prints out the details of the event
 */
static void *cm_thread( void *arg )
{
	struct rdma_cm_event	*event;
	int			ret;
	struct context		*ctx = (struct context *) arg;
	while ( 1 )
	{
		ret = rdma_get_cm_event( ctx->channel, &event );
		if ( ret )
		{
			VERB_ERR( "rdma_get_cm_event", ret );
			break;
		}
		printf( "event %s, status %d\n",
			rdma_event_str( event->event ), event->status );
		rdma_ack_cm_event( event );
	}
	return(NULL);
}


/*
 * Function: get_cm_event
 *
 * Input:
 * channel The event channel
 * type The event type that is expected
 *
 * Output:
 * out_ev The event will be passed back to the caller, if desired
 * Set this to NULL and the event will be acked automatically
 * Otherwise the caller must ack the event using rdma_ack_cm_event
 *
 * Returns:
 * 0 on success, non-zero on failure
 *
 * Description:
 * Waits for the next CM event and check that is matches the expected
 * type.
 */
int get_cm_event( struct rdma_event_channel *channel,
		  enum rdma_cm_event_type type,
		  struct rdma_cm_event **out_ev )
{
	int			ret	= 0;
	struct rdma_cm_event	*event	= NULL;
	ret = rdma_get_cm_event( channel, &event );
	if ( ret )
	{
		VERB_ERR( "rdma_resolve_addr", ret );
		return(-1);
	}
/* Verify the event is the expected type */
	if ( event->event != type )
	{
		printf( "event: %s, status: %d\n",
			rdma_event_str( event->event ), event->status );
		ret = -1;
	}
/* Pass the event back to the user if requested */
	if ( !out_ev )
		rdma_ack_cm_event( event );
	else
		*out_ev = event;
	return(ret);
}


/*
 * Function: resolve_addr
 *
 * Input:
 * ctx The context structure
 *
 * Output:
 * none
 *
 * Returns:
 * 0 on success, non-zero on failure
 *
 * Description:
 * Resolves the multicast address and also binds to the source address
 * if one was provided in the context
 */
int resolve_addr( struct context *ctx )
{
	int			ret;
	struct rdma_addrinfo	*bind_rai	= NULL;
	struct rdma_addrinfo	*mcast_rai	= NULL;
	struct rdma_addrinfo	hints;
	memset( &hints, 0, sizeof(hints) );
	hints.ai_port_space = RDMA_PS_UDP;
	if ( ctx->bind_addr )
	{
		hints.ai_flags	= RAI_PASSIVE;
                printf("%s\n", ctx->bind_addr);
		ret		= rdma_getaddrinfo( ctx->bind_addr, NULL, &hints, &bind_rai );
		if ( ret )
		{
			VERB_ERR( "rdma_getaddrinfo (bind)", ret );
			return(ret);
		}
	}
	hints.ai_flags	= 0;
	ret		= rdma_getaddrinfo( ctx->mcast_addr, NULL, &hints, &mcast_rai );
	if ( ret )
	{
		VERB_ERR( "rdma_getaddrinfo (mcast)", ret );
		return(ret);
	}
	if ( ctx->bind_addr )
	{
/* bind to a specific adapter if requested to do so */
		ret = rdma_bind_addr( ctx->id, bind_rai->ai_src_addr );
		if ( ret )
		{
			VERB_ERR( "rdma_bind_addr", ret );
			return(ret);
		}


/* A PD is created when we bind. Copy it to the context so it can
 * be used later on */
		ctx->pd = ctx->id->pd;
	}
	ret = rdma_resolve_addr( ctx->id, (bind_rai) ? bind_rai->ai_src_addr : NULL,
				 mcast_rai->ai_dst_addr, 2000 );
	if ( ret )
	{
		VERB_ERR( "rdma_resolve_addr", ret );
		return(ret);
	}
	ret = get_cm_event( ctx->channel, RDMA_CM_EVENT_ADDR_RESOLVED, NULL );
	if ( ret )
	{
		return(ret);
	}
	memcpy( &ctx->mcast_sockaddr,
		mcast_rai->ai_dst_addr,
		sizeof(struct sockaddr) );
	return(0);
}


/*
 * Function: create_resources
 *
 * Input:
 * ctx The context structure
 *
 * Output:
 * none
 *
 * Returns:
 * 0 on success, non-zero on failure
 *
 * Description:
 * Creates the PD, CQ, QP and MR
 */
int create_resources( struct context *ctx )
{
	int			ret, buf_size;
	struct ibv_qp_init_attr attr;
	memset( &attr, 0, sizeof(attr) );


/* If we are bound to an address, then a PD was already allocated
 * to the CM ID */
	if ( !ctx->pd )
	{
		ctx->pd = ibv_alloc_pd( ctx->id->verbs );
		if ( !ctx->pd )
		{
			VERB_ERR( "ibv_alloc_pd", -1 );
			return(ret);
		}
	}
	ctx->cq = ibv_create_cq( ctx->id->verbs, 2, 0, 0, 0 );
	if ( !ctx->cq )
	{
		VERB_ERR( "ibv_create_cq", -1 );
		return(ret);
	}
	attr.qp_type		= IBV_QPT_UD;
	attr.send_cq		= ctx->cq;
	attr.recv_cq		= ctx->cq;
	attr.cap.max_send_wr	= ctx->msg_count;
	attr.cap.max_recv_wr	= ctx->msg_count;
	attr.cap.max_send_sge	= 1;
	attr.cap.max_recv_sge	= 1;
	ret			= rdma_create_qp( ctx->id, ctx->pd, &attr );
	if ( ret )
	{
		VERB_ERR( "rdma_create_qp", ret );
		return(ret);
	}


/* The receiver must allow enough space in the receive buffer for
 * the GRH */
	buf_size	= ctx->msg_length + (ctx->sender ? 0 : sizeof(struct ibv_grh) );
	ctx->buf	= calloc( 1, buf_size );
	memset( ctx->buf, 0x00, buf_size );
/* Register our memory region */
	ctx->mr = rdma_reg_msgs( ctx->id, ctx->buf, buf_size );
	if ( !ctx->mr )
	{
		VERB_ERR( "rdma_reg_msgs", -1 );
		return(-1);
	}
	return(0);
}


/*
 * Function: destroy_resources
 *
 * Input:
 * ctx The context structure
 *
 * Output:
 * none
 *
 * Returns:
 * 0 on success, non-zero on failure
 *
 * Description:
 * Destroys AH, QP, CQ, MR, PD and ID
 */
void destroy_resources( struct context *ctx )
{
	if ( ctx->ah )
		ibv_destroy_ah( ctx->ah );
	if ( ctx->id->qp )
		rdma_destroy_qp( ctx->id );
	if ( ctx->cq )
		ibv_destroy_cq( ctx->cq );
	if ( ctx->mr )
		rdma_dereg_mr( ctx->mr );
	if ( ctx->buf )
		free( ctx->buf );
	if ( ctx->pd && ctx->id->pd == NULL )
		ibv_dealloc_pd( ctx->pd );
	rdma_destroy_id( ctx->id );
}


/*
 * Function: post_send
 *
 * Input:
 * ctx The context structure
 *
 * Output:
 * none
 *
 * Returns:
 * 0 on success, non-zero on failure
 *
 * Description:
 * Posts a UD send to the multicast address
 */
int post_send( struct context *ctx )
{
	int			ret;
	struct ibv_send_wr	wr, *bad_wr;
	struct ibv_sge		sge;
	memset( ctx->buf, 0x12, ctx->msg_length ); /* set the data to non-zero */
	sge.length	= ctx->msg_length;
	sge.lkey	= ctx->mr->lkey;
	sge.addr	= (uint64_t) ctx->buf;


/* Multicast requires that the message is sent with immediate data
 * and that the QP number is the contents of the immediate data */
	wr.next			= NULL;
	wr.sg_list		= &sge;
	wr.num_sge		= 1;
	wr.opcode		= IBV_WR_SEND_WITH_IMM;
	wr.send_flags		= IBV_SEND_SIGNALED;
	wr.wr_id		= 0;
	wr.imm_data		= htonl( ctx->id->qp->qp_num );
	wr.wr.ud.ah		= ctx->ah;
	wr.wr.ud.remote_qpn	= ctx->remote_qpn;
	wr.wr.ud.remote_qkey	= ctx->remote_qkey;
	ret			= ibv_post_send( ctx->id->qp, &wr, &bad_wr );
	if ( ret )
	{
		VERB_ERR( "ibv_post_send", ret );
		return(-1);
	}
	return(0);
}


/*
 * Function: get_completion
 *
 * Input:
 * ctx The context structure
 *
 * Output:
 * none
 *
 * Returns:
 * 0 on success, non-zero on failure
 *
 * Description:
 * Waits for a completion and verifies that the operation was successful
 */
int get_completion( struct context *ctx )
{
	int		ret;
	struct ibv_wc	wc;
	do
	{
		ret = ibv_poll_cq( ctx->cq, 1, &wc );
		if ( ret < 0 )
		{
			VERB_ERR( "ibv_poll_cq", ret );
			return(-1);
		}
	}
	while ( ret == 0 );
	if ( wc.status != IBV_WC_SUCCESS )
	{
		printf( "work completion status %s\n",
			ibv_wc_status_str( wc.status ) );
		return(-1);
	}
	return(0);
}


/*
 * Function: main
 *
 * Input:
 * argc The number of arguments
 * argv Command line arguments
 *
 * Output:
 * none
 *
 * Returns:
 * 0 on success, non-zero on failure
 *
 * Description:
 * Main program to demonstrate multicast functionality.
 * Both the sender and receiver create a UD Queue Pair and join the
 * specified multicast group (ctx.mcast_addr). If the join is successful,
 * the sender must create an Address Handle (ctx.ah). The sender then posts
 * the specified number of sends (ctx.msg_count) to the multicast group.
 * The receiver waits to receive each one of the sends and then both sides
 * leave the multicast group and cleanup resources.
 */
int main( int argc, char** argv )
{
	int			ret, op, i;
	struct context		ctx;
	struct ibv_port_attr	port_attr;
	struct rdma_cm_event	*event;
	char			buf[40];
	memset( &ctx, 0, sizeof(ctx) );
	ctx.sender	= 0;
	ctx.msg_count	= DEFAULT_MSG_COUNT;
	ctx.msg_length	= DEFAULT_MSG_LENGTH;
	ctx.server_port = DEFAULT_PORT;
/* Read options from command line */
	while ( (op = getopt( argc, argv, "shb:m:p:c:l:" ) ) != -1 )
	{
		switch ( op )
		{
		case 's':
			ctx.sender = 1;
			break;
		case 'b':
			ctx.bind_addr = optarg;
			break;
		case 'm':
			ctx.mcast_addr = optarg;
			break;
		case 'p':
			ctx.server_port = optarg;
			break;
		case 'c':
			ctx.msg_count = atoi( optarg );
			break;
		case 'l':
			ctx.msg_length = atoi( optarg );
			break;
		default:
			printf( "usage: %s -m mc_address\n", argv[0] );
			printf( "\t[-s[ender mode]\n" );
			printf( "\t[-b bind_address]\n" );
			printf( "\t[-p port_number]\n" );
			printf( "\t[-c msg_count]\n" );
			printf( "\t[-l msg_length]\n" );
			exit( 1 );
		}
	}
	if ( ctx.mcast_addr == NULL )
	{
		printf( "multicast address must be specified with -m\n" );
		exit( 1 );
	}
	ctx.channel = rdma_create_event_channel();
	if ( !ctx.channel )
	{
		VERB_ERR( "rdma_create_event_channel", -1 );
		exit( 1 );
	}
	ret = rdma_create_id( ctx.channel, &ctx.id, NULL, RDMA_PS_UDP );
	if ( ret )
	{
		VERB_ERR( "rdma_create_id", -1 );
		exit( 1 );
	}
	ret = resolve_addr( &ctx );
	if ( ret )
		goto out;
/* Verify that the buffer length is not larger than the MTU */
	ret = ibv_query_port( ctx.id->verbs, ctx.id->port_num, &port_attr );
	if ( ret )
	{
		VERB_ERR( "ibv_query_port", ret );
		goto out;
	}
	if ( ctx.msg_length > (1 << port_attr.active_mtu + 7) )
	{
		printf( "buffer length %d is larger then active mtu %d\n",
			ctx.msg_length, 1 << (port_attr.active_mtu + 7) );
		goto out;
	}
	ret = create_resources( &ctx );
	if ( ret )
		goto out;
	if ( !ctx.sender )
	{
		for ( i = 0; i < ctx.msg_count; i++ )
		{
			ret = rdma_post_recv( ctx.id, NULL, ctx.buf,
					      ctx.msg_length + sizeof(struct ibv_grh),
					      ctx.mr );
			if ( ret )
			{
				VERB_ERR( "rdma_post_recv", ret );
				goto out;
			}
		}
	}
/* Join the multicast group */
	ret = rdma_join_multicast( ctx.id, &ctx.mcast_sockaddr, NULL );
	if ( ret )
	{
		VERB_ERR( "rdma_join_multicast", ret );
		goto out;
	}
/* Verify that we successfully joined the multicast group */
	ret = get_cm_event( ctx.channel, RDMA_CM_EVENT_MULTICAST_JOIN, &event );
	if ( ret )
		goto out;
	inet_ntop( AF_INET6, event->param.ud.ah_attr.grh.dgid.raw, buf, 40 );
	printf( "joined dgid: %s, mlid 0x%x, sl %d\n", buf,
		event->param.ud.ah_attr.dlid, event->param.ud.ah_attr.sl );
	ctx.remote_qpn	= event->param.ud.qp_num;
	ctx.remote_qkey = event->param.ud.qkey;
	if ( ctx.sender )
	{
/* Create an address handle for the sender */
		ctx.ah = ibv_create_ah( ctx.pd, &event->param.ud.ah_attr );
		if ( !ctx.ah )
		{
			VERB_ERR( "ibv_create_ah", -1 );
			goto out;
		}
	}
	rdma_ack_cm_event( event );
/* Create a thread to handle any CM events while messages are exchanged */
	pthread_create( &ctx.cm_thread, NULL, cm_thread, &ctx );
	if ( !ctx.sender )
		printf( "waiting for messages...\n" );
	for ( i = 0; i < ctx.msg_count; i++ )
	{
		if ( ctx.sender )
		{
			ret = post_send( &ctx );
			if ( ret )
				goto out;
		}
		ret = get_completion( &ctx );
		if ( ret )
			goto out;
		if ( ctx.sender )
			printf( "sent message %d\n", i + 1 );
		else
			printf( "received message %d\n", i + 1 );
	}
out:
	ret = rdma_leave_multicast( ctx.id, &ctx.mcast_sockaddr );
	if ( ret )
		VERB_ERR( "rdma_leave_multicast", ret );
	destroy_resources( &ctx );
	return(ret);
}



