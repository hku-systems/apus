# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/util/common-structure.c \
../src/util/clock.c

OBJS += \
./src/util/common-structure.o \
./src/util/clock.o


# Each subdirectory must supply rules for building sources it contributes
src/util/%.o: ../src/util/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc-4.8 -fPIC -rdynamic -std=gnu11 -DDEBUG=$(DEBUGOPT) -I"$(ROOT_DIR)/../.local/include" -O0 -g3 -Wall -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


