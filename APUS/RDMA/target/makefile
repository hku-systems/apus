################################################################################
# Automatically-generated file. Do not edit!
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include src/util/subdir.mk
-include src/db/subdir.mk
-include src/consensus/subdir.mk
-include src/replica-sys/subdir.mk
-include src/ev_mgr/subdir.mk
-include src/config-comp/subdir.mk
-include src/rdma/subdir.mk
-include src/output/subdir.mk
-include src/subdir.mk
-include subdir.mk
-include objects.mk

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: interpose.so

# Tool invocations
interpose.so: $(OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	gcc-4.8  -shared -Wl,-soname,interpose.so $(OBJS) -Wall -o interpose.so $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '
	@file ./$@

# Other Targets
clean:
	-$(RM) $(OBJS)$(C_DEPS) interpose.so
	-@echo ' '

.PHONY: all clean dependents
