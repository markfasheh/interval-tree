VER=0.10-dev
RELEASE=v$(VER)

CC = gcc
CFLAGS = -Wall -ggdb -D_FILE_OFFSET_BITS=64

#CFILES=interval_tree.c  interval_tree_test.c  rbtree.c  rbtree_test.c
CFILES=interval_tree.c  interval_tree_test.c  rbtree.c
HEADERS=interval_tree_generic.h  interval_tree.h  rbtree_augmented.h  rbtree.h

objects = $(CFILES:.c=.o)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@ $(LIBRARY_FLAGS)

all: interval-tree-test
$(objects): $(HEADERS)
interval-tree-test: $(objects)
	$(CC) $(CFLAGS) $(objects) -o interval-tree-test $(LIBRARY_FLAGS)

clean:
	rm -fr $(objects) interval-tree-test
