#include "../include/util/common-structure.h"

int view_stamp_comp(view_stamp* op1,view_stamp* op2){
    if(op1->view_id<op2->view_id){
        return -1;
    }else if(op1->view_id>op2->view_id){
        return 1;
    }else{
        if(op1->req_id>op2->req_id){
            return 1;
        }else if(op1->req_id<op2->req_id){
            return -1;
        }else{
            return 0;
        }
    }
}

/* view stamp to long */
uint64_t vstol(view_stamp* vs){
    uint64_t result = ((uint64_t)vs->req_id)&0xFFFFFFFFl;
    uint64_t temp = (uint64_t)vs->view_id&0xFFFFFFFFl;
    result += temp<<32;
    return result;
};

view_stamp ltovs(uint64_t record_no){
    view_stamp vs;
    uint64_t temp;
    temp = record_no&0x00000000FFFFFFFFl;
    vs.req_id = temp;
    vs.view_id = (record_no>>32)&0x00000000FFFFFFFFl;
    return vs;
}