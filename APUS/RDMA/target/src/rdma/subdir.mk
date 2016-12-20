################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/rdma/dare_ibv.c \
../src/rdma/dare_ibv_rc.c \
../src/rdma/dare_server.c

OBJS += \
./src/rdma/dare_ibv.o \
./src/rdma/dare_ibv_rc.o \
./src/rdma/dare_server.o \


# Each subdirectory must supply rules for building sources it contributes
src/rdma/%.o: ../src/rdma/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc-4.8 -fPIC -rdynamic -std=gnu11 -DDEBUG=$(DEBUGOPT) -I/usr/include/infiniband -O0 -g3 -Wall -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


