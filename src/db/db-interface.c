#include <stdlib.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <db.h>
#include "../include/db/db-interface.h"
#include "../include/util/debug.h"

const char* db_dir="./.db";

u_int32_t pagesize = 32 * 1024;
u_int cachesize = 32 * 1024 * 1024;

struct db_t{
    DB* bdb_ptr;
};

uint32_t records_len;

db* initialize_db(const char* db_name,uint32_t flag){
    db* db_ptr=NULL;
    DB* b_db;
    int ret;
    /* Initialize the DB handle */
    if((ret = db_create(&b_db,NULL,flag))!=0){
        err_log("DB : %s.\n",db_strerror(ret));
        goto db_init_return;
    }
    
    if((ret = b_db->set_pagesize(b_db,pagesize))!=0){
        goto db_init_return;
    }
    if((ret = b_db->set_cachesize(b_db, 0, cachesize, 1))!=0){
        goto db_init_return;
    }

    if((ret = b_db->open(b_db,NULL,db_name,NULL,DB_RECNO,DB_THREAD|DB_CREATE,0))!=0){
        //b_db->err(b_db,ret,"%s","test.db");
        goto db_init_return;
    }
    db_ptr = (db*)(malloc(sizeof(db)));
    db_ptr->bdb_ptr = b_db;

db_init_return:
    if(db_ptr!=NULL){
        //debug_log("DB Initialization Finished\n");
        ;
    }
    return db_ptr;
}

void close_db(db* db_p,uint32_t mode){
    if(db_p!=NULL){
        if(db_p->bdb_ptr!=NULL){
            db_p->bdb_ptr->close(db_p->bdb_ptr,mode);
            db_p->bdb_ptr=NULL;
        }
        free(db_p);
        db_p = NULL;
    }
    return;
}

int store_record(db* db_p,size_t data_size,void* data){
    int ret = 1;
    if((NULL==db_p)||(NULL==db_p->bdb_ptr)){
        if(db_p == NULL){
          err_log("DB store_record : db_p is null.\n");
        } else{
          err_log("DB store_recor : db_p->bdb_ptr is null.\n");
        }
        goto db_store_return;
    }
    DB* b_db = db_p->bdb_ptr;
    DBT key,db_data;
    memset(&db_data,0,sizeof(db_data));
    db_data.data = data;
    db_data.size = data_size;

    records_len += data_size;

    memset(&key,0,sizeof(key));
    key.flags = DB_DBT_MALLOC;
    if ((ret=b_db->put(b_db,NULL,&key,&db_data,DB_AUTO_COMMIT|DB_APPEND))==0){
        //debug_log("db : %ld record stored. \n",*(uint64_t*)key_data);
        //b_db->sync(b_db,0);
    }
    else{
        err_log("DB : %s.\n",db_strerror(ret));
        //debug_log("db : can not save record %ld from database.\n",*(uint64_t*)key_data);
        //b_db->err(b_db,ret,"DB->Put");
    }
db_store_return:
    return ret;
}

void dump_records(db* db_p, void* buf){
    DB* b_db = db_p->bdb_ptr;
    DBT key, data;
    DBC *dbcp;
    int ret;

    uint32_t len = 0;

    /* Acquire a cursor for the database. */
    if ((ret = b_db->cursor(b_db, NULL, &dbcp, 0)) != 0) {
        b_db->err(b_db, ret, "DB->cursor");
    }

    /* Re-initialize the key/data pair. */
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    /* Walk through the database and print out the key/data pairs. */
    while ((ret = dbcp->c_get(dbcp, &key, &data, DB_NEXT)) == 0) {
        //debug_log("%lu : %.*s\n", *(u_long *)key.data, (int)data.size, (char *)data.data);
        memcpy((char*)buf+len, data.data, data.size);
        len += data.size;
    }
    if (ret != DB_NOTFOUND)
        b_db->err(b_db, ret, "DBcursor->get");

    /* Close the cursor. */
    if ((ret = dbcp->c_close(dbcp)) != 0) {
        b_db->err(b_db, ret, "DBcursor->close");
    }
}


uint32_t get_records_len()
{
    return records_len;
}