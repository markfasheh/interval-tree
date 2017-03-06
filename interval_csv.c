#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <sys/param.h>

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

#include <ctype.h>

#include "interval_tree_generic.h"
#include "compiler.h"

static char *prog = "interval_tree";

static struct rb_root root = RB_ROOT;
static int extents = 0;

static void usage(void)
{
	printf("%s [-e] [-S search start] [-E search end] file\n\n", prog);
	printf("Loads (start, end) pairs from 'file' into an interval tree,\n");
	printf("and print a count of total space referenced by those pairs.\n");
	printf("'file' must be in csv format.\n\n");
	printf("Switches:\n");
	printf("\t-e\tLoad values in extent format (start, len)\n");
	printf("\t-S\tStart search from this value (defaults to 0)\n");
	printf("\t-E\tEnd search from this value (defaults to ULONG_MAX)\n");
	printf("\n");
}

struct extent_tree_node {
	struct rb_node		node;
	struct endpoint_key		*subtree_last;
	struct endpoint_key		*start;
	struct endpoint_key		*end;
};

struct endpoint_key {
	char *val;
	/* len is set but not currently used since *val is null terminated */
	int len;
};

static char *ltostr(unsigned long value)
{
	int needed = snprintf(NULL, 0, "%lu", value);
	char *ret = malloc(needed);

	if (ret)
		snprintf(ret, needed, "%lu", value);

	return ret;
}

static char *clean_whitespace(char *s)
{
	char *end;

	if (!s)
		return NULL;

	end = &s[strlen(s) - 1];
	while (!isdigit(*s) && *s != '\0')
		s++;
	if (*s == '\0')
		return NULL;

	while (!isdigit(*end) && end > s)
		end--;

	*(end+1) = '\0';

	return s;
}

static struct extent_tree_node *new_extent_tree_node(char *start, char *end,
						     int from_extent)
{
	struct extent_tree_node *n;

	n = calloc(1, sizeof(*n));
	if (!n) {
		fprintf(stderr, "Out of memory.\n");
		goto out;
	}

	n->start = calloc(1, sizeof(struct endpoint_key));
	if (!n->start) {
		free(n->start);
		free(n);
		n = NULL;
		goto out;
	}
	n->end = calloc(1, sizeof(struct endpoint_key));
	if (!n->end) {
		free(n->end);
		free(n);
		n = NULL;
		goto out;
	}

	n->start->val = strdup(start);
	if (from_extent) {
		unsigned long tmpstart = atoi(start);
		unsigned long tmpend = atoi(end);

		tmpend = tmpend + tmpstart - 1;
		n->end->val = ltostr(tmpend);
	} else {
		n->end->val = strdup(end);
	}

	if (!n->start || !n->end) {
		fprintf(stderr, "Out of memory.\n");
		n = NULL;
		goto out;
	}

	n->start->len = strlen(n->start->val);
	n->end->len = strlen(n->end->val);

out:
	return n;
}

static inline int cmp_endpoint_keys(struct endpoint_key *a, struct endpoint_key *b)
{
	unsigned long val1, val2;

	val1 = atol(a->val);
	val2 = atol(b->val);
	if (val1 < val2)
		return -1;
	else if (val1 > val2)
		return 1;
	return 0;
}

static void init_endpoint_key(struct endpoint_key *key, char *val)
{
	key->val = val;
	key->len = strlen(val);
}

#define START(n) ((n)->start)
#define LAST(n)  ((n)->end)

KEYED_INTERVAL_TREE_DEFINE(struct extent_tree_node, node, struct endpoint_key *, subtree_last, START, LAST, cmp_endpoint_keys, static, extent_tree);

static void print_nodes(char *startstr, char *endstr)
{
	struct endpoint_key start, end;
	struct extent_tree_node *n;

	init_endpoint_key(&start, startstr);
	init_endpoint_key(&end, endstr);

	n = extent_tree_iter_first(&root, &start, &end);
	printf("Tree nodes:");
	while (n) {
		printf(" (%s, %s)", n->start->val, n->end->val);
		n = extent_tree_iter_next(n, &start, &end);
	}
	printf("\n");
}

/*
 * Find all extents which overlap 'n', calculate the space
 * covered by them and remove those nodes from the tree.
 */
static unsigned long count_unique_bytes(struct extent_tree_node *n)
{
	struct extent_tree_node *tmp;
	struct endpoint_key *wstart = n->start;
	struct endpoint_key *wend = n->end;
	unsigned long total;

	printf("Count overlaps:");

	do {
		/*
		 * Expand our search window based on the lastest
		 * overlapping extent. Doing this will allow us to
		 * find all possible overlaps
		 */
		if (cmp_endpoint_keys(wstart, n->start) > 0)
			wstart = n->start;
		if (cmp_endpoint_keys(wend, n->end) < 0)
			wend = n->end;

		printf(" (%s, %s)", n->start->val, n->end->val);

		tmp = n;
		n = extent_tree_iter_next(n, wstart, wend);

		extent_tree_remove(tmp, &root);
		free(tmp);
	} while (n);

	total = atol(wend->val) - atol(wstart->val) + 1;
	printf("; wstart: %s wend: %s total: %lu\n", wstart->val,
	       wend->val, total);

	return total;
}

/*
 * Get a total count of space covered in this tree, accounting for any
 * overlap by input intervals.
 */
static void add_unique_intervals(unsigned long *ret_bytes, char *str_start,
				 char *str_end)
{
	unsigned long count = 0;
	struct endpoint_key start;
	struct endpoint_key end;
	struct extent_tree_node *n;

	init_endpoint_key(&start, str_start);
	init_endpoint_key(&end, str_end);

	n = extent_tree_iter_first(&root, &start, &end);

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
		n = extent_tree_iter_first(&root, &start, &end);
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
	char *start = "0";
	char *end = "18446744073709551615";/* ULONG_MAX (on 64-bit only!) */

	while ((c = getopt(argc, argv, "?eS:E:"))
	       != -1) {
		switch (c) {
		case 'e':
			extents = 1;
			break;
		case 'S':
			start = strdup(optarg);
			printf("start: %lu\n", atol(start));
			break;
		case 'E':
			end = strdup(optarg);
			printf("end: %lu\n", atol(end));
			break;
		case '?':
		default:
			usage();
			return 0;
		}
	}

	if ((argc - optind) != 1) {
		usage();
		return 1;
	}

	filename = argv[optind];

	fp = fopen(filename, "r");
	if (fp == NULL) {
		ret = errno;
		fprintf(stderr, "Error %d while opening \"%s\": %s\n",
			ret, filename, strerror(ret));
	}

	while (fgets(line, LINE_LEN, fp)) {
		struct extent_tree_node *n;

		s1 = clean_whitespace(strtok(line, ","));
		s2 = clean_whitespace(strtok(NULL, ","));
		if (!s1 || !s2)
			continue;

		n = new_extent_tree_node(s1, s2, extents);
		if (!n) {
			ret = ENOMEM;
			goto out;
		}
		extent_tree_insert(n, &root);
	}

	print_nodes(start, end);
	printf("\n");

	add_unique_intervals(&unique_space, start, end);
	printf("Total nonoverapping space is %lu\n", unique_space);

	ret = 0;
out:
	fclose(fp);
	return ret;
}
