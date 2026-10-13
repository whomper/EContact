/*
 * people_cli.c - share/people.c on Linux, for the integration tests:
 *   people-cli INF TEXT                      the choices matching TEXT
 *   people-cli INF -complete LINE VALUE      the line completed
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../share/people.h"
#include "charset.h"

static void out(const char *atari)
{
	long n;
	char *u = atari_to_utf8(atari, (long)strlen(atari), &n);
	fputs(u, stdout);
	free(u);
}

int main(int argc, char **argv)
{
	PPICK *l;
	int n, i;
	if (argc < 3)
		return 2;
	people_inf(argv[1]);
	if (!strcmp(argv[2], "-complete") && argc >= 5) {
		char *r = people_complete(argv[3], argv[4]);
		printf("%s\n", r);
		free(r);
		return 0;
	}
	n = people_pick_list(argv[2], &l);
	for (i = 0; i < n; i++) {
		printf("%d\t", l[i].group);
		out(l[i].label);
		printf("\t");
		out(l[i].value);
		printf("\n");
	}
	people_pick_free(l, n);
	return 0;
}
