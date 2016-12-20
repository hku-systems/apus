#include "../include/util/clock.h"

#define BILLION 1000000000L

void clock_init(clock_handler *c_k)
{
	c_k->counter = 0;
}

void clock_add(clock_handler *c_k)
{
	struct timespec clock_time;
	clock_gettime(CLOCK_MONOTONIC, &clock_time);
	c_k->clocks[c_k->counter] = clock_time;
	c_k->counter++;
}

void clock_display(FILE* output, clock_handler *c_k, uint8_t type, size_t data_size)
{
	uint64_t diff;
	struct timespec start_time, end_time;
	char tmp[64], str[256];
	memset(str, 0, sizeof(str));
	int i;
	for (i = 0; i < c_k->counter; i++)
	{
		end_time = c_k->clocks[i];
		if (i != 0)
		{
			diff = BILLION * (end_time.tv_sec - start_time.tv_sec) + end_time.tv_nsec - start_time.tv_nsec;
			sprintf(tmp, "%llu ", (long long unsigned int)diff);
			strcat(str, tmp);
		}
		start_time = end_time;
	}
	//sprintf(tmp, "%zu,%"PRIu8"", type, data_size);
	//strcat(str, tmp);
	safe_rec_log(output, "%s\n", str);
}
