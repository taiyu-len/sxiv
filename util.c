/* Copyright 2011 Bert Muennich
 *
 * This file is part of sxiv.
 *
 * sxiv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * sxiv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with sxiv.  If not, see <http://www.gnu.org/licenses/>.
 */
#define _GNU_SOURCE
#include "sxiv.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

const char *progname;

void* emalloc(size_t size)
{
	void *ptr;
	
	ptr = malloc(size);
	if (ptr == NULL)
		error(EXIT_FAILURE, errno, NULL);
	return ptr;
}

void* erealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (ptr == NULL)
		error(EXIT_FAILURE, errno, NULL);
	return ptr;
}

char* estrdup(const char *s)
{
	char *d;
	size_t n = strlen(s) + 1;

	d = malloc(n);
	if (d == NULL)
		error(EXIT_FAILURE, errno, NULL);
	memcpy(d, s, n);
	return d;
}

void error(int eval, int err, const char* fmt, ...)
{
	va_list ap;

	if (eval == 0 && options->quiet)
		return;

	fflush(stdout);
	fprintf(stderr, "%s: ", progname);
	va_start(ap, fmt);
	if (fmt != NULL)
		vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (err != 0)
		fprintf(stderr, "%s%s", fmt != NULL ? ": " : "", strerror(err));
	fputc('\n', stderr);

	if (eval != 0)
		exit(eval);
}

void size_readable(float *size, const char **unit)
{
	const char *units[] = { "", "K", "M", "G" };
	int i;

	for (i = 0; i < ARRLEN(units) && *size > 1024.0; i++)
		*size /= 1024.0;
	*unit = units[MIN(i, ARRLEN(units) - 1)];
}

static
int r_skip_dotfiles(const struct dirent *d)
{
	if (d->d_name[0] == '.')
		return 0;
	return 1;
}

static
int r_no_skip_dotfiles(const struct dirent *d)
{
	if (STREQ(d->d_name, ".") || STREQ(d->d_name, ".."))
		return 0;
	return 1;
}

typedef int (*filter_t)(const struct dirent *);

static
filter_t r_filter(bool skip_dotfiles)
{
	if (skip_dotfiles)
		return r_skip_dotfiles;
	else
		return r_no_skip_dotfiles;
}

int r_opendir(r_dir_t *rdir, const char *dirname, bool recursive)
{
	size_t len;

	if (*dirname == '\0')
		return -1;


	rdir->name  = NULL;
	rdir->recursive = recursive;
	rdir->i = 0;
	rdir->end = 0;

	rdir->stcap = 512;
	rdir->stack = (char**) emalloc(rdir->stcap * sizeof(char*));
	rdir->stlen = 1;
	rdir->stack[0] = estrdup(dirname);

	len = strlen(rdir->stack[0]);
	if (rdir->stack[0][len-1] == '/')
		rdir->stack[0][len-1] = '\0';

	return 0;
}

int r_closedir(r_dir_t *rdir)
{
	if (rdir->list) {
		for (; rdir->i < rdir->end; rdir->i++) {
			free(rdir->list[rdir->i]);
		}
		free(rdir->list);
	}

	if (rdir->stack != NULL) {
		while (rdir->stlen > 0)
			free(rdir->stack[--rdir->stlen]);
		free(rdir->stack);
		rdir->stack = NULL;
	}

	free(rdir->name);
	rdir->name = NULL;

	return 0;
}

char* r_readdir(r_dir_t *rdir, bool skip_dotfiles)
{
	size_t len;
	char *filename;
	struct dirent *dentry;
	struct stat fstats;


	while (true) {
		if (rdir->name && rdir->i == rdir->end) {
			/* finish scanning rdir->name */
			free(rdir->name);
			free(rdir->list);
			rdir->name = NULL;
			rdir->list = NULL;
		}
		if (rdir->name == NULL) {
			/* open next subdirectory on stack if there are any */
			if (rdir->stlen == 0) {
				break;
			}
			rdir->name = rdir->stack[--rdir->stlen];
			rdir->end = scandir(rdir->name, &rdir->list,
					  r_filter(skip_dotfiles),
					  versionsort);
			rdir->i = 0;
			if (rdir->end < 0) {
				error(0, errno, "%s", rdir->name);
				rdir->end = 0;
			}
			continue;
		}

		/* get filename for next entry in rdir->list */
		dentry = rdir->list[rdir->i++];
		len = strlen(rdir->name) + strlen(dentry->d_name) + 2;
		filename = (char*) emalloc(len);
		snprintf(filename, len, "%s/%s", rdir->name, dentry->d_name);
		free(dentry);

		if (stat(filename, &fstats) < 0)
			continue;
		if (S_ISDIR(fstats.st_mode)) {
			if (! rdir->recursive)
				continue;
			/* put subdirectory on the stack */
			if (rdir->stlen == rdir->stcap) {
				rdir->stcap *= 2;
				rdir->stack = (char**) erealloc(
					rdir->stack,
					rdir->stcap * sizeof(char*));
			}
			rdir->stack[rdir->stlen++] = filename;
			continue;
		}
		return filename;
	}
	return NULL;
}

int r_mkdir(char *path)
{
	char c, *s = path;
	struct stat st;

	while (*s != '\0') {
		if (*s == '/') {
			s++;
			continue;
		}
		for (; *s != '\0' && *s != '/'; s++);
		c = *s;
		*s = '\0';
		if (mkdir(path, 0755) == -1)
			if (errno != EEXIST || stat(path, &st) == -1 || !S_ISDIR(st.st_mode))
				return -1;
		*s = c;
	}
	return 0;
}

