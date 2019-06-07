################################################################################
# Automatically-generated file. Do not edit!
################################################################################

CC = gcc

ifeq ($(DEBUGOPT),1)
	FLAGS        = -fPIC -rdynamic -std=gnu99 -DDEBUG -I"$(ROOT_DIR)/../src/include/dare" -I"$(ROOT_DIR)/../utils/rbtree/include" -I/usr/include
else
	FLAGS        = -fPIC -rdynamic -std=gnu99 -I"$(ROOT_DIR)/../src/include/dare" -I"$(ROOT_DIR)/../utils/rbtree/include" -I/usr/include
endif
CFLAGS       = #-Wall -Wunused-function #-Wextra
LDFLAGS      = -L/usr/lib -libverbs

PREFIX = $(ROOT_DIR)/src/dare
DARE_LIBPATH = $(PREFIX)/lib

DARE_HEADERS = $(shell echo $(ROOT_DIR)/../src/include/dare/*.h)
DARE_SRCS = $(shell echo $(ROOT_DIR)/../src/dare/*.c)
DARE_OBJS = $(DARE_SRCS:.c=.o)
DARE = $(DARE_LIBPATH)/libdare.a

RBTREE_HEADERS = $(shell echo $(ROOT_DIR)/../utils/rbtree/include/*.h)
RBTREE_SRCS = $(shell echo $(ROOT_DIR)/../utils/rbtree/src/*.c)
RBTREE_OBJS = $(RBTREE_SRCS:.c=.o)
RBTREE = $(DARE_LIBPATH)/librbtree.a

all: dare

$(RBTREE): rbtree_print $(RBTREE_OBJS) $(RBTREE_HEADERS)
	mkdir -pm 755 $(DARE_LIBPATH)
	ar -rcs $@ $(RBTREE_OBJS)
	@echo "##############################"
	@echo
rbtree_print:
	@echo "##### BUILDING Red-Black Tree #####"
	
dare: FLAGS += -I/usr/local/include
dare: LDFLAGS += /usr/local/lib/libev.a
dare: $(DARE) 
$(DARE): $(RBTREE) dare_print $(DARE_OBJS) $(DARE_HEADERS) 
	mkdir -pm 755 $(DARE_LIBPATH)
	ar -rcs $@ $(DARE_OBJS) $(RBTREE_OBJS)
	@echo "##############################"
	@echo
dare_print:
	@echo "##### BUILDING DARE #####"

%.o: %.c $(HEADERS)
	$(CC) $(FLAGS) $(CFLAGS) -c -o $@ $<
	 
.PHONY : all
