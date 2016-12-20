#ifndef CLOCK_H
#define CLOCK_H
#include "./common-header.h"

typedef struct clock_handler_t {
	struct timespec clocks[4];
	int counter;
}clock_handler;

void clock_init(clock_handler *c_k);
void clock_add(clock_handler *c_k);
void clock_display(FILE* output, clock_handler *c_k, uint8_t type, size_t data_size);

#endif 
