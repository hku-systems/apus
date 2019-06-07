# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/db/db-interface.c

OBJS += \
./src/db/db-interface.o


# Each subdirectory must supply rules for building sources it contributes
src/db/%.o: ../src/db/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -fPIC -rdynamic -std=gnu99 -DDEBUG=$(DEBUGOPT) -O0 -g3 -Wall -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


