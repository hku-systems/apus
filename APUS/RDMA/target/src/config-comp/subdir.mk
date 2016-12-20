# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/config-comp/config-comp.c \
../src/config-comp/config-mgr.c 

OBJS += \
./src/config-comp/config-comp.o \
./src/config-comp/config-mgr.o 


# Each subdirectory must supply rules for building sources it contributes
src/config-comp/%.o: ../src/config-comp/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc-4.8 -fPIC -rdynamic -std=gnu11 -DDEBUG=$(DEBUGOPT) -I"$(ROOT_DIR)/../.local/include" -O0 -g3 -Wall -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


