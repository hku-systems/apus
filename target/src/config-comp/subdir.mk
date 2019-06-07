# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/config-comp/config-dare.c \
../src/config-comp/config-proxy.c 


OBJS += \
./src/config-comp/config-dare.o \
./src/config-comp/config-proxy.o 


# Each subdirectory must supply rules for building sources it contributes
src/config-comp/%.o: ../src/config-comp/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -fPIC -rdynamic -std=gnu99 -DDEBUG=$(DEBUGOPT) -O0 -g3 -Wall -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


