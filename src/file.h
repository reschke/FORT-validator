#ifndef SRC_FILE_H_
#define SRC_FILE_H_

#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include "uri.h"

/*
 * The entire contents of the file, loaded into a buffer.
 *
 * Instances of this struct are expected to live on the stack.
 */
struct file_contents {
	unsigned char *buffer;
	size_t buffer_size;
};

int file_open(struct rpki_uri const *, FILE **, struct stat *);
void file_close(FILE *file);

int file_load(struct rpki_uri const *, struct file_contents *);
void file_free(struct file_contents *);

#endif /* SRC_FILE_H_ */