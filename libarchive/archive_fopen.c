#if 0 /* Self-compiling C source code.  Run "sh file.c"
set -ex
${CC-cc} -Wall -g -O $0 -o ${0%.c} -DTEST -larchive
exit 0

*/
#endif

#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <stdio.h>

/*
 * libarchive's error reporting is much more detailed than stdio's, so all
 * we can really do is return -1 or NULL on errors and hope that errno is
 * enough to diagnose the problem.
 */

static int
xlaread(void *cookie, char *data, int size)
{
	la_ssize_t r;

	r = archive_read_data((struct archive *)cookie, (void*)data, size);
	if (r >= 0)
		return (r);
	errno = archive_errno((struct archive *)cookie);
	return (-1);
}

static int
xlawrite(void *cookie, const char *data, int size)
{
	la_ssize_t r;

	r = archive_write_data((struct archive *)cookie, (void*)data, size);
	if (r >= 0)
		return (r);
	errno = archive_errno((struct archive *)cookie);
	return (-1);
}

static int
xlaclose(void *cookie)
{
	int r;

	r = archive_free((struct archive *)cookie);
	if (r == ARCHIVE_OK)
		return (0);
	errno = archive_errno((struct archive *)cookie);
	return (-1);
}

static FILE *
xlaopen(const char *fname, int fd, const char *mode)
{
	struct archive *a;
	struct archive_entry *ae;
	int r, save_errno;

	if (*mode == 'r') {
		a = archive_read_new();
		if (a == NULL)
			return (NULL);
		r = archive_read_support_format_raw(a);
		if (r == ARCHIVE_OK)
			r = archive_read_support_filter_all(a);
		if (r == ARCHIVE_OK) {
			if (fname != NULL)
				r = archive_read_open_filename(a, fname, 65536);
			else
				r = archive_read_open_fd(a, fd, 65536);
		}
		if (r == ARCHIVE_OK)
			r = archive_read_next_header(a, &ae);
		if (r == ARCHIVE_OK)
			return (funopen(a, xlaread, NULL, NULL, xlaclose));
	} else {
		a = archive_write_new();
		if (a == NULL)
			return (NULL);

		r = archive_write_set_format_raw(a);
		if (r == ARCHIVE_OK)
			r = archive_write_add_filter_zstd(a);
		if (r == ARCHIVE_OK) {
			if (fname != NULL)
				r = archive_write_open_filename(a, fname);
			else
				r = archive_write_open_fd(a, fd);
		}
		if (r == ARCHIVE_OK) {
			ae = archive_entry_new();
			if (ae == NULL) {
				archive_write_free(a);
				return (NULL);
			}
			archive_entry_set_filetype(ae, AE_IFREG);
			r = archive_write_header(a, ae);
		}
		if (r == ARCHIVE_OK) {
			archive_entry_free(ae);
			return (funopen(a, NULL, xlawrite, NULL, xlaclose));
		}
	}

	save_errno = archive_errno(a);
	archive_free(a);
	errno = save_errno;
	return (NULL);
}

FILE *
archive_fopen(const char *fname, const char *mode)
{
	return (xlaopen(fname, -1, mode));
}

FILE *
archive_fdopen(int fd, const char *mode)
{
	return (xlaopen(NULL, fd, mode));
}

#ifdef TEST

#include <err.h>

int main(void)
{
	FILE *f;
	int r;

	char buf[1024] = "";

	f = archive_fopen("test.gz", "w");
	if (f == NULL)
		err(1, "archive_fopen/w failed");
	r = fwrite("hellothere", 1, 10, f);
	printf("fwrite=%d\n", r);
	r = fclose(f);
	printf("fclose=%d\n", r);

	f = archive_fopen("test.gz", "r");
	if (f == NULL)
		err(1, "archive_fopen/r failed");
	r = fread(buf, 1, sizeof(buf), f);
	printf("fread=%d, eof=%d, err=%d\n", r, feof(f), ferror(f));
	if (r == 0)
		printf("%m\n");

	printf("<%s>\n", buf);
	fclose(f);
}
#endif
