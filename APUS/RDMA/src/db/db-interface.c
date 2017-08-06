#define _GNU_SOURCE

#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <db.h>
#include <pthread.h>
#include <inttypes.h>
#include <unistd.h>
#include <time.h>
#include "../include/db/db-interface.h"

#define CHECK_ERROR(e)\
	(((e) >= 0) ? (void)0 :\
	(fprintf(stderr, "%s: %d: %s failed: ", __FILE__, __LINE__, #e)\
    , perror(NULL), exit(EXIT_FAILURE)))

const char *db_path = "./DB_";
const char *dbname_prefix = "node_test_";

// #define USE_BDB

#ifdef USE_BDB
//#define DEBUG
//#define USE_ENV
#define ERROR 1
#define NAME_LENGTH 31
#define ARRAY_SIZE 10
//ARRAY_SIZE shoule be an even number
#define BILLION 1000000000UL

//constants
const int MAX_RES = 1;		//notice 2
const int MAX_PUT = 100000;
const uint32_t PAGESIZE = 32 * 1024;
const uint32_t CACHESIZE = 32 * 1024 * 1024;

//struct definition
typedef struct _db_info
{
	DB *dbp;
	char name[NAME_LENGTH];
}db_info;

//global variables
#ifdef USE_ENV
DB_ENV *db_env;
#endif
db_info bdb_array[ARRAY_SIZE];
struct
{
	db_info *slot_ptr;
	int slot_pos;
}store_db;						//notice : store_db is not a pointer but a struct
struct
{
	DB **all_db_handle;		//notice 1
	uint64_t sum;			//notice 5
}all_db;
int res;
int put_number;
pthread_mutex_t mutex, alldb_mtx;
pthread_cond_t empty, full;
pthread_rwlock_t rwlock;
pthread_spinlock_t pn_lock;		//notice 4

//function declaration
db * initialize_db(const char* db_name, uint32_t flag);
void * db_manage(void *arg);
void consume();
void switch_slot();
int store_record(db *arg, size_t key_size,void *key_data,size_t data_size,void *data);
void close_db(db *arg, uint32_t flags);
int retrieve_record(db *arg, size_t key_size,void *key_data,size_t *data_size,void **data);
void warm_up(DB *dbp);
uint64_t ato_uint64(char *str);

db * initialize_db(const char* db_name, uint32_t flag)
{
	int ret, i;
	pthread_t db_manage_id;
	char tmp[10];
	char *db_dir;

	db_dir = (char *)malloc(strlen(db_path) + strlen(db_name) + 1);
	memcpy(db_dir, db_path, strlen(db_path));
	memcpy(db_dir + strlen(db_path), db_name, strlen(db_name));
	db_dir[strlen(db_path) + strlen(db_name)] = '\0';


	if((ret = mkdir(db_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) != 0)
	{
		fprintf(stderr, "DB : Dir Creation failed: %s\n", strerror(errno));
		exit(ERROR);
	}

#ifdef USE_ENV
	if((ret = db_env_create(&db_env, 0)) != 0)
	{
		fprintf(stderr, "DB : Error creating env handle: %s\n", db_strerror(ret));
		exit(ERROR);
	}

	if((ret = db_env->open(db_env, db_dir, DB_CREATE|DB_INIT_CDB|DB_INIT_MPOOL|DB_THREAD, 0)) != 0)
	{
		fprintf(stderr, "DB : Environment open failed: %s\n", db_strerror(ret));
		exit(ERROR);
	}
#else
	if((ret = chdir(db_dir)) != 0)
	{
		fprintf(stderr, "DB : Dir Creation failed: %s\n", strerror(errno));
		exit(ERROR);
	}
#endif

	all_db.all_db_handle = (DB **)malloc(ARRAY_SIZE * sizeof(DB *));
	all_db.sum = ARRAY_SIZE;
	for(i = 0;i < ARRAY_SIZE;i++)
	{
		if((ret = db_create(&bdb_array[i].dbp,
#ifdef USE_ENV
							db_env,
#else
							NULL,
#endif
							flag)) != 0)
		{
			fprintf(stderr, "DB : Database %d Creation failed: %s\n", i, db_strerror(ret));
			exit(ERROR);
		}

		memcpy(bdb_array[i].name, dbname_prefix, strlen(dbname_prefix));
		sprintf(tmp, "%d", i);
		memcpy(bdb_array[i].name + strlen(dbname_prefix), tmp, strlen(tmp));
		bdb_array[i].name[strlen(dbname_prefix) + strlen(tmp)] = '\0';

		if((ret = bdb_array[i].dbp->set_pagesize(bdb_array[i].dbp, PAGESIZE)) != 0)
		{
			fprintf(stderr, "DB : Set pagesize for Database %d failed: %s\n", i, db_strerror(ret));
			exit(ERROR);
		}
		if((ret = bdb_array[i].dbp->set_cachesize(bdb_array[i].dbp, 0, CACHESIZE, 1)) != 0)
		{
			fprintf(stderr, "DB : Set cachesize for Database %d failed: %s\n", i, db_strerror(ret));
			exit(ERROR);
		}

		if((ret = bdb_array[i].dbp->open(bdb_array[i].dbp, NULL, bdb_array[i].name, NULL, DB_BTREE, DB_THREAD|DB_CREATE, 0)) != 0)
		{
			fprintf(stderr, "DB : Database %d Open failed: %s\n", i, db_strerror(ret));
			exit(ERROR);
		}

		warm_up(bdb_array[i].dbp);

#ifdef DEBUG
		printf("node_test_%d created.\n", i);
#endif
		all_db.all_db_handle[i] = bdb_array[i].dbp;
	}

	store_db.slot_ptr = bdb_array;
	store_db.slot_pos = 0;

	res = MAX_RES;
	put_number = 0;

	pthread_cond_init(&empty, NULL);
	pthread_cond_init(&full, NULL);
	pthread_rwlock_init(&rwlock, NULL);
	pthread_spin_init(&pn_lock, PTHREAD_PROCESS_PRIVATE);

	//create db_manage thread
	if((ret = pthread_create(&db_manage_id, NULL, db_manage, (void *)flag)) != 0)
	{
		fprintf(stderr, "DB : db_manage thread creation failed: %s\n", strerror(errno));
		exit(ERROR);
	}

	return NULL;
}

void * db_manage(void *arg)
{
	int ret, i;
	uint32_t flag = (uint32_t)arg;
	int start_index;
	DB **tmp;
	char str[10];
	pthread_t db_manage_id;

	while(1)
	{
		pthread_mutex_lock(&mutex);
		while(res == MAX_RES)
			pthread_cond_wait(&empty, &mutex);

		//create new databases and update bdb_array
		pthread_mutex_lock(&alldb_mtx);

		if((all_db.sum / (ARRAY_SIZE / 2)) % 2 == 0)
			start_index = 0;
		else
			start_index = ARRAY_SIZE / 2;

		tmp = (DB **)malloc(sizeof(DB *) * (all_db.sum + ARRAY_SIZE / 2));
		for(i = 0;i < all_db.sum;i++)
		{
			tmp[i] = all_db.all_db_handle[i];
		}
		free(all_db.all_db_handle);		//notice
		all_db.all_db_handle = tmp;

		for(i = start_index;i < start_index + ARRAY_SIZE / 2;i++)
		{
			if((ret = db_create(&bdb_array[i].dbp,
#ifdef USE_ENV
								db_env,
#else
								NULL,
#endif
								flag)) != 0)
			{
				fprintf(stderr, "DB : Database %"PRIu64" Creation failed: %s\n", all_db.sum, db_strerror(ret));
				exit(ERROR);
			}

			sprintf(str, "%"PRIu64"", all_db.sum);
			memcpy(bdb_array[i].name + strlen(dbname_prefix), str, strlen(str));
			bdb_array[i].name[strlen(dbname_prefix) + strlen(str)] = '\0';

			if((ret = bdb_array[i].dbp->set_pagesize(bdb_array[i].dbp, PAGESIZE)) != 0)
			{
				fprintf(stderr, "DB : Set pagesize for Database %"PRIu64" failed: %s\n", all_db.sum, db_strerror(ret));
				exit(ERROR);
			}
			if((ret = bdb_array[i].dbp->set_cachesize(bdb_array[i].dbp, 0, CACHESIZE, 1)) != 0)
			{
				fprintf(stderr, "DB : Set cachesize for Database %"PRIu64" failed: %s\n", all_db.sum, db_strerror(ret));
				exit(ERROR);
			}

			if((ret = bdb_array[i].dbp->open(bdb_array[i].dbp, NULL, bdb_array[i].name, NULL, DB_BTREE, DB_THREAD|DB_CREATE, 0)) != 0)
			{
				fprintf(stderr, "DB : Database %"PRIu64" Open failed: %s\n", all_db.sum, db_strerror(ret));
				exit(ERROR);
			}

			warm_up(bdb_array[i].dbp);
#ifdef DEBUG
			printf("node_test_%"PRIu64" created.\n", all_db.sum);
#endif

			all_db.all_db_handle[all_db.sum] = bdb_array[i].dbp;
			all_db.sum++;
		}
		pthread_mutex_unlock(&alldb_mtx);

		//finished

		res++;
		pthread_cond_signal(&full);
		pthread_mutex_unlock(&mutex);
	}
}

void consume()
{
	pthread_mutex_lock(&mutex);
	while(res == 0)
		pthread_cond_wait(&full, &mutex);
	switch_slot();
	res--;
	pthread_cond_signal(&empty);
	pthread_mutex_unlock(&mutex);
}

void switch_slot()
{
	store_db.slot_pos = (1 + store_db.slot_pos) % ARRAY_SIZE;

	pthread_rwlock_wrlock(&rwlock);
	store_db.slot_ptr = &bdb_array[store_db.slot_pos];
	pthread_rwlock_unlock(&rwlock);

	pthread_spin_lock(&pn_lock);
	put_number = 0;
	pthread_spin_unlock(&pn_lock);
}

//#ifdef DEBUG
//int store_record(db *arg, size_t key_size,void *key_data,size_t data_size,void *data, uint64_t *diff1, uint64_t *diff2, uint64_t *diff3, uint64_t *diff4)
//#else
int store_record(db *arg, size_t key_size,void *key_data,size_t data_size,void *data)
//#endif
{
//#ifdef DEBUG
//	struct timespec start_time, end_time;
//#endif
	int ret = 1;
	DBT key,db_data;
	db_info *pdb_info;

	memset(&key,0,sizeof(key));
	memset(&db_data,0,sizeof(db_data));
	key.data = key_data;
	key.size = key_size;
	db_data.data = data;
	db_data.size = data_size;

//#ifdef DEBUG
//	clock_gettime(CLOCK_MONOTONIC, &start_time);
//#endif
	pthread_rwlock_rdlock(&rwlock);
	pdb_info = store_db.slot_ptr;
	pthread_rwlock_unlock(&rwlock);
//#ifdef DEBUG
//	clock_gettime(CLOCK_MONOTONIC, &end_time);
//	*diff1 = BILLION * (end_time.tv_sec - start_time.tv_sec) + end_time.tv_nsec - start_time.tv_nsec;
//#endif

/*#ifdef DEBUG
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	ret = pdb_info->dbp->put(pdb_info->dbp, NULL, &key, &db_data, DB_AUTO_COMMIT);
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	*diff2 = BILLION * (end_time.tv_sec - start_time.tv_sec) + end_time.tv_nsec - start_time.tv_nsec;
	if(ret != 0)
#else*/
	if((ret = pdb_info->dbp->put(pdb_info->dbp, NULL, &key, &db_data, DB_AUTO_COMMIT)) != 0)
//#endif
	{
		fprintf(stderr, "DB : Store record failed: %s\n", db_strerror(ret));
	}
	else
	{
//#ifdef DEBUG
//		clock_gettime(CLOCK_MONOTONIC, &start_time);
//#endif
		pthread_spin_lock(&pn_lock);
		put_number++;
		pthread_spin_unlock(&pn_lock);
//#ifdef DEBUG
//		clock_gettime(CLOCK_MONOTONIC, &end_time);
//		*diff3 = BILLION * (end_time.tv_sec - start_time.tv_sec) + end_time.tv_nsec - start_time.tv_nsec;
//#endif

//#ifdef DEBUG
//		clock_gettime(CLOCK_MONOTONIC, &start_time);
//#endif
		if (put_number == MAX_PUT)
		{
			if ((store_db.slot_pos + 1) % (ARRAY_SIZE / 2) == 0)
				consume();
			else
				switch_slot();
		}
//#ifdef DEBUG
//		clock_gettime(CLOCK_MONOTONIC, &end_time);
//		*diff4 = BILLION * (end_time.tv_sec - start_time.tv_sec) + end_time.tv_nsec - start_time.tv_nsec;
//#endif
	}

	return ret;
}

//what will happen if it is called?
//notice
void close_db(db *arg, uint32_t flags)
{
	pthread_mutex_lock(&alldb_mtx);

	//close all DB handles
#ifdef USE_ENV
	db_env->close(db_env, DB_FORCESYNC);
#else
	uint64_t i;
	for(i = 0;i < all_db.sum;i++)
		if(all_db.all_db_handle[i] != NULL)
			all_db.all_db_handle[i]->close(all_db.all_db_handle[i], flags);
#endif

	//destroy the sychronization variables
	pthread_cond_destroy(&empty);
	pthread_cond_destroy(&full);
	pthread_rwlock_destroy(&rwlock);
	pthread_spin_destroy(&pn_lock);

	//free all_db_handle
	free(all_db.all_db_handle);

	//pthread_mutex_unlock(&alldb_mtx);

	return;
}

//notice : has the buffer be allocated before calling this function?
int retrieve_record(db *arg, size_t key_size,void *key_data,size_t *data_size,void **data)
{
	int ret = 1;
	DBT key,db_data;
	db_info *pdb_info;
	uint64_t i, index;

	memset(&key,0,sizeof(key));
	memset(&db_data,0,sizeof(db_data));

	key.data = key_data;
	key.size = key_size;

	db_data.flags = DB_DBT_MALLOC;

	pthread_rwlock_rdlock(&rwlock);
	pdb_info = store_db.slot_ptr;
	pthread_rwlock_unlock(&rwlock);

	index = ato_uint64(pdb_info->name + strlen(dbname_prefix));

	pthread_mutex_lock(&alldb_mtx);
	for(i = index;i >= 0;i--)
	{
		if((ret = all_db.all_db_handle[i]->get(all_db.all_db_handle[i], NULL, &key, &db_data, 0)) != 0)
		{
			if(ret == DB_NOTFOUND)
				continue;
			else
			{
				fprintf(stderr, "DB : Retrieve record failed: %s\n", db_strerror(ret));
				return ret;
			}
		}
		else
			break;
	}
	pthread_mutex_unlock(&alldb_mtx);

	if(i < 0)
		return DB_NOTFOUND;
	else
	{
		//not sure whether the following is correct
		*data_size = db_data.size;
		*data = db_data.data;
		return 0;
	}
}

void warm_up(DB *dbp)
{
	DBT key, data;
	uint64_t key_data = 0, data_data = 0;

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));

	key.size = sizeof(uint64_t);
	key.data = &key_data;
	data.size = sizeof(uint64_t);
	data.data = &data_data;

	dbp->put(dbp, NULL, &key, &data, DB_AUTO_COMMIT);
	dbp->del(dbp, NULL, &key, 0);
}

uint64_t ato_uint64(char *str)
{
	uint64_t val = 0;

	while((*str) != '\0')
	{
		val = val * 10 + (*str) - 48;
		str++;
	}

	return val;
}

#else
#include <sys/types.h>
#include <fcntl.h>

#define DEBUG
#define TIME

#define SECTOR_SIZE 512
#define MIN(a, b) ((a) >= (b) ? (b) : (a))

typedef struct data_t {
  size_t size;
  void*  opaque;
} data_t;

typedef struct entry_t {
  data_t* id;
  off_t   offset;
  size_t  size;
  struct entry_t* prev;
  struct entry_t* next;
} entry_t;

int db_fd;
entry_t* head = NULL;
#define ENTRY_HEAD (head)
#define ENTRY_TAIL (head->prev)

#define DL_RSEARCH(head, out, elt, cmp)                                                            \
  DL_SEARCH2(head, out, elt, cmp, prev)

pthread_mutex_t mtx;

static inline const int id_compare(const entry_t* e1, const entry_t* e2) {
  return memcmp(e1->id->opaque,
                e2->id->opaque,
                MIN(e1->id->size, e2->id->size));
}

entry_t* get_entry(data_t* id) {
#ifdef DEBUG
  printf("\t[DEBUG] In get_entry.\n");
  fflush(stdout);
#endif
  entry_t* res = (entry_t*) malloc(sizeof(entry_t));
  entry_t tar = {.id = id};
  CHECK_ERROR(pthread_mutex_lock(&mtx));
  DL_RSEARCH(ENTRY_HEAD, res, &tar, id_compare);
  CHECK_ERROR(pthread_mutex_unlock(&mtx));
  return res;
}

const off_t alloc_entry(const data_t* id, const size_t val_size) {
#ifdef DEBUG
  printf("\t[DEBUG] In alloc_entry.\n");
  fflush(stdout);
#endif
  entry_t* new_entry = (entry_t*) malloc(sizeof(entry_t));
  new_entry->id = (data_t*) malloc(sizeof(data_t));
  new_entry->id->size = id->size;
  new_entry->id->opaque = malloc(new_entry->id->size);
  memcpy(new_entry->id->opaque, id->opaque, new_entry->id->size);
  new_entry->size = val_size;
  CHECK_ERROR(pthread_mutex_lock(&mtx));
  new_entry->offset = ENTRY_HEAD ? ENTRY_TAIL->offset + ENTRY_TAIL->size : 0;
  DL_APPEND(ENTRY_HEAD, new_entry);
  CHECK_ERROR(pthread_mutex_unlock(&mtx));
  return new_entry->offset;
}

static inline const size_t aligned_size(const size_t val_size) {
  size_t remainder = val_size % SECTOR_SIZE;
  if (remainder == 0)
    return val_size;
  return val_size + SECTOR_SIZE - remainder;
}

db* initialize_db(const char *db_name, uint32_t flags) {
#ifdef DEBUG
  printf("\t[DEBUG] In initialize_db.\n");
  fflush(stdout);
#endif
  size_t dir_size = strlen(db_path) + strlen(db_name) + 1;
  char* db_dir = (char *)malloc(dir_size);
  memset(db_dir, 0, dir_size);
  strcat(db_dir, db_path);
  strcat(db_dir, db_name);

  CHECK_ERROR(mkdir(db_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH));
  CHECK_ERROR(chdir(db_dir));
#ifdef DEBUG
  printf("\t[DEBUG] Creating file.\n");
  fflush(stdout);
#endif
  CHECK_ERROR(db_fd = open("node_test_0",
                           flags | O_RDWR | O_CREAT | O_DIRECT,
                           0664));
#ifdef DEBUG
  printf("\t[DEBUG] File created.\n");
  printf("\t[DEBUG] Initializing mutex lock.\n");
  fflush(stdout);
#endif
  CHECK_ERROR(pthread_mutex_init(&mtx, NULL));
#ifdef DEBUG
  printf("\t[DEBUG] Initializing completed.\n");
  fflush(stdout);
#endif
  return NULL;
}

void close_db(db *_, uint32_t flags) {
#ifdef DEBUG
  printf("\t[DEBUG] In close_db.\n");
  fflush(stdout);
#endif
  CHECK_ERROR(fsync(db_fd));
  CHECK_ERROR(close(db_fd));
  CHECK_ERROR(pthread_mutex_destroy(&mtx));
}

int store_record(db *_,
                 size_t id_size,  void * id_data,
                 size_t val_size, void * val_data) {
#ifdef DEBUG
  printf("\t[DEBUG] In store_record.\n");
  fflush(stdout);
#endif
#ifdef TIME
  struct timeval start, end;
  gettimeofday(&start, NULL);
#endif
  data_t id = {
    .size = id_size,
    .opaque = id_data,
  };
  const off_t offset = alloc_entry(&id, val_size);
  void * aligned_buffer;
  const size_t aligned_val_size = aligned_size(val_size);
#ifdef DEBUG
  printf("\t[DEBUG] allocated offset: %d.\n", offset);
  printf("\t[DEBUG] aligned size: %d.\n", aligned_val_size);
  fflush(stdout);
#endif
  CHECK_ERROR(posix_memalign(&aligned_buffer, SECTOR_SIZE, aligned_val_size));
  memset(aligned_buffer, 0, aligned_val_size);
  memmove(aligned_buffer, val_data, val_size);
  CHECK_ERROR(pwrite(db_fd, aligned_buffer, aligned_val_size, offset));
  free(aligned_buffer);
#ifdef TIME
  gettimeofday(&end, NULL);
  fprintf(stdout, "\t%lu",
          (end.tv_sec - start.tv_sec) * 1000000 +
          (end.tv_usec - start.tv_usec));
  fflush(stdout);
#endif
  return EXIT_SUCCESS;
}

int retrieve_record(db *_,
                    size_t  id_size,  void *  id_data,
                    size_t *val_size, void ** val_data) {
#ifdef DEBUG
  printf("\t[DEBUG] In retrieve_record.\n");
  fflush(stdout);
#endif
  data_t id = {
    .size = id_size,
    .opaque = id_data,
  };
  entry_t* entry = get_entry(&id);
  if (entry == NULL) {
    fprintf(stderr, "Get entry failed.\n");
    exit(EXIT_FAILURE);
  }
  *val_size = entry->size;
  void * aligned_buffer;
  const size_t aligned_val_size = aligned_size(*val_size);
#ifdef DEBUG
  printf("\t[DEBUG] found offset: %d.\n", entry->offset);
  printf("\t[DEBUG] aligned size: %d.\n", aligned_val_size);
  fflush(stdout);
#endif
  CHECK_ERROR(posix_memalign(&aligned_buffer, SECTOR_SIZE, aligned_val_size));
  memset(aligned_buffer, 0, aligned_val_size);
  CHECK_ERROR(pread(db_fd, aligned_buffer, aligned_val_size, entry->offset));
  memmove(*val_data, aligned_buffer, *val_size);
  free(aligned_buffer);
  free(entry);
  return EXIT_SUCCESS;
}

#endif
