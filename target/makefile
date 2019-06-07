################################################################################
# Automatically-generated file. Do not edit!
################################################################################

include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include src/dare/subdir.mk
-include src/proxy/subdir.mk
-include src/db/subdir.mk
-include src/config-comp/subdir.mk
-include src/subdir.mk
-include subdir.mk
-include objects.mk

LIBS += $(DARE) -lev -ldb -lconfig -libverbs -lm

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: interpose.so

# Tool invocations
interpose.so: $(OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	gcc -shared -Wl,-soname,interpose.so $(OBJS) -Wall -o interpose.so $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
	@echo "##### CLEAN-UP DARE#####"
	-$(RM) $(RBTREE_OBJS)
	-$(RM) $(DARE_OBJS)
	-$(RM) $(DARE) $(RBTREE)
	@echo "########################"
	-@echo ' '
	-$(RM) $(OBJS)$(C_DEPS) interpose.so
	-@echo ' '

.PHONY: all clean dependents
