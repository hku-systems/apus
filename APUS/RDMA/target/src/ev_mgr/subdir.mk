# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/ev_mgr/ev_mgr.c \
../src/ev_mgr/check_point_thread.c 

OBJS += \
./src/ev_mgr/ev_mgr.o \
./src/ev_mgr/check_point_thread.o 


# Each subdirectory must supply rules for building sources it contributes
src/ev_mgr/%.o: ../src/ev_mgr/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc-4.8 -fPIC -rdynamic -std=gnu11 -DDEBUG=$(DEBUGOPT) -I"$(ROOT_DIR)/../.local/include" -O0 -g3 -Wall -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


