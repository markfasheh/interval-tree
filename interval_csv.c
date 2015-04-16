#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "interval_tree.h"
#include "interval_tree_generic.h"

static char *prog = "interval_tree";

static struct rb_root root = RB_ROOT;
static int extents = 0;

static void usage(void)
{
	printf("%s [-e] file\n\n", prog);
	printf("Loads (start, end) pairs from 'file' into an interval tree,\n");
	printf("and print a count of total space referenced by those pairs.\n");
	printf("'file' must be in csv format.\n\n");
	printf("Switches:\n");
	printf("\t-e\tLoad values in extent format (start, len)\n\n");
}

static void print_nodes(void)
{
	struct interval_tree_node *n = interval_tree_iter_first(&root,
								0, -1UL);

	if (!n)
		return;

	printf("Tree nodes:");
	while (n) {
		printf(" (%lu, %lu)", n->start, n->last);
		n = interval_tree_iter_next(n, 0, -1UL);
	}
	printf("\n");
}

/*
 * Find all extents which overlap 'n', calculate the space
 * covered by them and remove those nodes from the tree.
 */
static unsigned long count_unique_bytes(struct interval_tree_node *n)
{
	struct interval_tree_node *tmp;
	unsigned long wstart = n->start;
	unsigned long wlast = n->last;

	printf("Count overlaps:");

	do {
		/*
		 * Expand our search window based on the lastest
		 * overlapping extent. Doing this will allow us to
		 * find all possible overlaps
		 */
		if (wstart > n->start)
			wstart = n->start;
		if (wlast < n->last)
			wlast = n->last;

		printf(" (%lu, %lu)", n->start, n->last);

		tmp = n;
		n = interval_tree_iter_next(n, wstart, wlast);

		interval_tree_remove(tmp, &root);
		free(tmp);
	} while (n);

	printf("; wstart: %lu wlast: %lu total: %lu\n", wstart,
	       wlast, wlast - wstart + 1);

	return wlast - wstart + 1;
}

/*
 * Get a total count of space covered in this tree, accounting for any
 * overlap by input intervals.
 */
static void add_unique_intervals(unsigned long *ret_bytes)
{
	unsigned long count = 0;
	struct interval_tree_node *n = interval_tree_iter_first(&root,
								0, -1UL);

	if (!n)
		goto out;

	while (n) {
		/*
		 * Find all extents which overlap 'n', calculate the space
		 * covered by them and remove those nodes from the tree.
		 */
		count += count_unique_bytes(n);

		/*
		 * Since count_unique_bytes will be emptying the tree,
		 * we can grab the first node here
		 */
		n = interval_tree_iter_first(&root, 0, -1UL);
	}

out:
	*ret_bytes = count;
}

#define LINE_LEN	1024
int main(int argc, char **argv)
{
	int c, ret;
	char *filename;
	char *s1, *s2;
	FILE *fp;
	char line[LINE_LEN];
	unsigned long unique_space;

	while ((c = getopt(argc, argv, "e?"))
	       != -1) {
		switch (c) {
		case 'e':
			extents = 1;
			break;
		case '?':
		default:
			usage();
			return 0;
		}
	}

	if ((argc - optind) != 1)
		usage();

	filename = argv[optind];

	fp = fopen(filename, "r");
	if (fp == NULL) {
		ret = errno;
		fprintf(stderr, "Error %d while opening \"%s\": %s\n",
			ret, filename, strerror(ret));
	}

	while (fgets(line, LINE_LEN, fp)) {
		struct interval_tree_node *n;

		n = calloc(1, sizeof(*n));
		if (!n) {
			ret = ENOMEM;
			fprintf(stderr, "Out of memory.\n");
			goto out;
		}

		s1 = strtok(line, ",");
		s2 = strtok(NULL, ",");
		if (!s1 || !s2)
			continue;

		n->start = atol(s1);
		n->last = atol(s2);
		if (extents) {
			/*
			 * in this case n->last was read as an extent
			 * len, turn it into an offset
			 */
			n->last = n->last + n->start - 1;
		}

		interval_tree_insert(n, &root);
	}

	print_nodes();
	printf("\n");

	add_unique_intervals(&unique_space);
	printf("Total nonoverapping space is %lu\n", unique_space);

	ret = 0;
out:
	fclose(fp);
	return ret;
}
