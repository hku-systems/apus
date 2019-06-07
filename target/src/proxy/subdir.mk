# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/proxy/proxy.c

OBJS += \
./src/proxy/proxy.o


# Each subdirectory must supply rules for building sources it contributes
src/proxy/%.o: ../src/proxy/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -fPIC -rdynamic -std=gnu99 -DDEBUG=$(DEBUGOPT) -O0 -g3 -Wall -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


