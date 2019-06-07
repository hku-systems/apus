#ifndef DB_INTERFACE_H
#define DB_INTERFACE_H
#include <stdint.h>
#include <sys/types.h>

typedef struct db_t db;

db* initialize_db(const char* db_name,uint32_t flag);

void close_db(db*,uint32_t);

int store_record(db*,size_t,void*);

// the caller is responsible to release the memory

void dump_records(db*,void*);
uint32_t get_records_len();
#endif
