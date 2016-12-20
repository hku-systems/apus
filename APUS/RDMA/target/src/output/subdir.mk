################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/output/output.c \
../src/output/adlist.c \
../src/output/crc64.c \
../src/output/decision.c \


OBJS += \
./src/output/output.o \
./src/output/adlist.o \
./src/output/crc64.o \
./src/output/decision.o \


# Each subdirectory must supply rules for building sources it contributes
src/output/%.o: ../src/output/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc-4.8 -fPIC -rdynamic -std=gnu11 -DDEBUG=$(DEBUGOPT) -O0 -g3 -Wall -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


