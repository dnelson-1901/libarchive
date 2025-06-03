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
#include <string.h>
#ifdef TEST
#include <err.h>
#endif

/*
 * libarchive's error reporting is much more detailed than stdio's, so all
 * we can really do is return -1 or NULL on errors and hope that errno is
 * enough to diagnose the problem.
 */

/* copied from tar/creation_set.c */
static int
auto_filter(struct archive *a, const char *name)
{
	const char *suffix;

	/* A pair of suffix and compression name. */
	static const struct {
		const char *suffix;
		const char *name;
	} tbl[] = {
		{ ".Z",		"compress" },
		{ ".b64",	"b64encode" },
		{ ".bz2",	"bzip2" },
		{ ".gz",	"gzip" },
		{ ".grz",	"grzip" },
		{ ".lrz",	"lrzip" },
		{ ".lz",	"lzip" },
		{ ".lz4",	"lz4" },
		{ ".lzo",	"lzop" },
		{ ".lzma",	"lzma" },
		{ ".uu",	"uuencode" },
		{ ".xz",	"xz" },
		{ ".zst",	"zstd"},
		{ NULL,		NULL }
	};

	int i;

	/* See if indicates compression/filter type such as ".gz" */
	suffix = strrchr(name, '.');
	if (suffix == NULL)
		return (ARCHIVE_OK);
	for (i = 0; tbl[i].suffix != NULL; i++) {
		if (strcmp(tbl[i].suffix, suffix) == 0)
			return archive_write_add_filter_by_name(a, tbl[i].name);
	}
	return (ARCHIVE_OK);
}

static int
xlaread(void *cookie, char *data, int size)
{
	la_ssize_t r;

	r = archive_read_data((struct archive *)cookie, (void*)data, size);
	if (r >= 0)
		return (r);
#ifdef TEST
	warnx("libarchive read message: %d/%s\n", archive_errno((struct archive *)cookie), archive_error_string((struct archive *)cookie));
#endif
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
#ifdef TEST
	warnx("libarchive write message: %d/%s\n", archive_errno((struct archive *)cookie), archive_error_string((struct archive *)cookie));
#endif
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
#ifdef TEST
	warnx("libarchive close message: %d/%s\n", archive_errno((struct archive *)cookie), archive_error_string((struct archive *)cookie));
#endif
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
			r = archive_read_support_format_empty(a);
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
		if (r == ARCHIVE_OK || r == ARCHIVE_EOF)
		{
			FILE *f = funopen(a, xlaread, NULL, NULL, xlaclose);
			/* archive_read_next_header returns ARCHIVE_EOF immediately when
			 * opening a compressed empty file.  This will cause a call to
			 * archive_read_data to fail, so we have to set EOF on the file
			 * pointer here to prevent the read callback from being called.
			 */
			if (r == ARCHIVE_EOF && f != NULL)
			{
				f->_flags |= __SEOF;
			}
			return f;
		}
	} else {
		a = archive_write_new();
		if (a == NULL)
			return (NULL);

		r = archive_write_set_format_raw(a);
		if (r == ARCHIVE_OK) {
			/* enable filter if the suffix is one we recognize */
			r = auto_filter(a, fname);
		}
		if (r == ARCHIVE_OK || r == ARCHIVE_WARN) {
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
#ifdef TEST
	warnx("libarchive open message: %d/%s\n", archive_errno(a), archive_error_string(a));
#endif
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

int main(int argc, char* argv[])
{
	FILE *f;
	int r;
	int save_errno;

	char buf[1024] = "hellothere";
	char *filename = "test.zst";

	if (argc > 1)
		filename = argv[1];
	if (argc > 2)
		strlcpy(buf, argv[2], sizeof(buf));

	f = archive_fopen(filename, "w");
	if (f == NULL)
		err(1, "archive_fopen/w failed");
	r = fwrite(buf, 1, strlen(buf), f);
	printf("fwrite=%d\n", r);
	r = fclose(f);
	printf("fclose=%d\n", r);

	f = archive_fopen(filename, "r");
	if (f == NULL)
		err(1, "archive_fopen/r failed");
	r = fread(buf, 1, sizeof(buf), f);
	save_errno = errno;
	printf("fread=%d, eof=%d, err=%d, errno=%d/%s\n", r, feof(f), ferror(f), save_errno, strerror(save_errno));

	printf("<%s>\n", buf);
	fclose(f);
}

#endif
