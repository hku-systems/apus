#include "../include/rdma/dare.h"
#include "../include/rdma/dare_ibv.h"
#include "../include/rdma/dare_ibv_rc.h"
#include "../include/rdma/dare_server.h"

/* InfiniBand device */
dare_ib_device_t *dare_ib_device;
#define IBDEV dare_ib_device
#define SRV_DATA ((dare_server_data_t*)dare_ib_device->udata)

/* ================================================================== */
/* local function - prototypes */

static dare_ib_device_t* init_one_device(struct ibv_device* ib_dev);
static void free_ib_device();

/* ================================================================== */

int dare_init_ib_device()
{
    int i;
    int num_devs;
    struct ibv_device **ib_devs = NULL;
    
    ib_devs = ibv_get_device_list(&num_devs);
    if (0 == num_devs) {
        error_return(1, log_fp, "No HCAs available\n");
    }
    if (NULL == ib_devs) {
        error_return(1, log_fp, "Get device list returned NULL\n");
    }   
    
    for (i = 0; i < num_devs; i++) {
        IBDEV = init_one_device(ib_devs[i]);
        if (NULL != IBDEV) {
            break;
        }
    }
    
    ibv_free_device_list(ib_devs);
    
    if (NULL == IBDEV) {
        return 1;
    }

    return 0; 
}

int dare_init_ib_srv_data( void *data )
{
    int i;
    IBDEV->udata = data;
     
    for (i = 0; i < SRV_DATA->config.len; i++) {
        SRV_DATA->config.servers[i].ep = malloc(sizeof(dare_ib_ep_t));
        if (NULL == SRV_DATA->config.servers[i].ep) {
            error_return(1, log_fp, "Cannot allocate EP\n");
        }
        memset(SRV_DATA->config.servers[i].ep, 0, sizeof(dare_ib_ep_t));
    }
    
    return 0;
}

int dare_init_ib_rc()
{
    return rc_init();
}

static dare_ib_device_t *init_one_device( struct ibv_device* ib_dev )
{
    int i;
    dare_ib_device_t *device = NULL;
    struct ibv_context *dev_context = NULL;
    
    /* Open up the device */
    dev_context = ibv_open_device(ib_dev);
    if (NULL == dev_context) {
        goto error;
    }
    
    /* Allocate new device */
    device = (dare_ib_device_t*)malloc(sizeof(dare_ib_device_t));
    if (NULL == device) {
        goto error;
    }
    memset(device, 0, sizeof(dare_ib_device_t));
    
    /* Init device */
    device->ib_dev = ib_dev;
    device->ib_dev_context = dev_context;
    
    /* Get device's attributes */
    if(ibv_query_device(device->ib_dev_context, &device->ib_dev_attr)){
        goto error;
    }

    info(log_fp, "# max_qp_wr=%d\n", device->ib_dev_attr.max_qp_wr);
    info(log_fp, "# max_qp_rd_atom=%d\n", device->ib_dev_attr.max_qp_rd_atom);
    
    info(log_fp, "# HCA %s supports maximum %d WRs.\n", ibv_get_device_name(device->ib_dev), device->ib_dev_attr.max_qp_wr);

    /* Find port */
    device->port_num = 0;
    for (i = 1; i <= device->ib_dev_attr.phys_port_cnt; i++) {
        struct ibv_port_attr ib_port_attr;
        if (ibv_query_port(device->ib_dev_context, i, &ib_port_attr)) {
            goto error;
        }
        if (IBV_PORT_ACTIVE != ib_port_attr.state) {
            continue;
        }

        /* find index of pkey 0xFFFF */
        uint16_t pkey, j;
        for (j = 0; j < device->ib_dev_attr.max_pkeys; j++) {
            if (ibv_query_pkey(device->ib_dev_context, i, j, &pkey)) {
                goto error;
            }
            pkey = ntohs(pkey);
            info(log_fp, "# pkey_value = %"PRIu16" for index %"PRIu16"\n",
                 pkey, j);
            if (pkey == 0xFFFF) {
                device->pkey_index = j;
                break;
            }
        }

        device->mtu = ib_port_attr.max_mtu;
        info(log_fp, "# ib_port_attr.active_mtu = %"PRIu32" (%d bytes)\n", ib_port_attr.active_mtu, mtu_value(ib_port_attr.active_mtu));
        info(log_fp, "# device->mtu = %"PRIu32" (%d bytes)\n", device->mtu, mtu_value(device->mtu));

        device->port_num = i;
        info(log_fp, "# device->port_num = %"PRIu8"\n", device->port_num);
        device->lid = ib_port_attr.lid;
        info(log_fp, "# ib_port_attr.lid = %"PRIu16"\n", ib_port_attr.lid);

        device->gid_idx = 0;
        break;
    }
    if (0 == device->port_num) {
        goto error;
    }
      
    return device;

error:
    if (NULL != device) {
        free(device);
    }

    if (NULL != dev_context) {
        ibv_close_device(dev_context);
    }
    return NULL;      
}

void dare_ib_destroy_ep(uint32_t idx)
{
    dare_ib_ep_t *ep = (dare_ib_ep_t*)SRV_DATA->config.servers[idx].ep;
    if (NULL == ep) {
        return;
    }
    // Note: RC QPs are already destroyed in rc_free
    free(ep);
    SRV_DATA->config.servers[idx].ep = NULL;
}


void dare_ib_srv_shutdown()
{
    /* Free RC resources */
    rc_free();
    
    if (NULL != SRV_DATA) {
        if (NULL != SRV_DATA->config.servers) {
            uint32_t i, size = SRV_DATA->config.len;
            for (i = 0; i < size; i++) {
                dare_ib_destroy_ep(i);
            }
        }
    }
    
    free_ib_device();
}

static void free_ib_device()
{        
    if (NULL != IBDEV) {
    
        if (NULL != IBDEV->ib_dev_context) {
            ibv_close_device(IBDEV->ib_dev_context);
        }
    
        free(IBDEV);
        IBDEV = NULL;
    }
}

int find_max_inline(struct ibv_context *context, struct ibv_pd *pd, uint32_t *max_inline_arg)
{
    int rc;
    struct ibv_qp *qp = NULL;
    struct ibv_cq *cq = NULL;
    struct ibv_qp_init_attr init_attr;
    uint32_t max_inline_data;
    
    *max_inline_arg = 0;

    /* Make a dummy CQ */
    cq = ibv_create_cq(context, 1, NULL, NULL, 0);
    if (NULL == cq) {
        return 1;
    }
    
    /* Setup the QP attributes */
    memset(&init_attr, 0, sizeof(init_attr));
    init_attr.qp_type = IBV_QPT_RC;
    init_attr.send_cq = cq;
    init_attr.recv_cq = cq;
    init_attr.srq = 0;
    init_attr.cap.max_send_sge = 1;
    init_attr.cap.max_recv_sge = 1;
    init_attr.cap.max_recv_wr = 1;
    
    init_attr.cap.max_inline_data = max_inline_data = 1 << 20;
    rc = 1;
    while (max_inline_data > 0) {
        qp = ibv_create_qp(pd, &init_attr);
        if (NULL != qp) {
            *max_inline_arg = max_inline_data;
            ibv_destroy_qp(qp);
            rc = 0;
            break;
        }
        max_inline_data >>= 1;
        init_attr.cap.max_inline_data = max_inline_data;
    }
    
    /* Destroy the temp CQ */
    if (NULL != cq) {
        ibv_destroy_cq(cq);
    }

    return rc;
}
