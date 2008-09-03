#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include "common.h"


static int log_ready = 0;
static FILE *log_fp = NULL;
static pthread_mutex_t log_mutex;


int
log_init(void)
{
    int err;
    struct timeval tv;
    struct tm* tm;
    char log_filename[PATH_MAX];
    char time_str[200];


    if(log_ready != 0 || log_fp != NULL)
	return -1;

    memset(&tv, 0, sizeof(struct timeval));

    err = pthread_mutex_init(&log_mutex, NULL);
    if(err != 0) {
	fprintf(stderr, "(common) failed initializing log mutex!\n");
	return -1;
    }

    gettimeofday(&tv, NULL);
    tm = localtime(&tv.tv_sec);


    /*
     * Format the date and time, down to a single second. 
     */

    strftime(time_str, 200, "%Y-%m-%d_%H:%M:%S", tm);


    /*
     * Print the formatted time, in seconds, followed by a decimal point
     *  and the milliseconds. 
     */

    snprintf(log_filename, PATH_MAX, "/tmp/%s.log", time_str);
    fprintf(stderr, "(common) initializing log: %s\n", log_filename);

    log_fp = fopen(log_filename, "w+");
    if(log_fp == NULL) {
	perror("(common) fopen");
	return -1;
    }

    log_ready = 1;

    return 0;
}


int
log_message(char *message) {
    int err;
    struct timeval tv;
    struct tm *tm;
    char ftime_str[200];

    if(log_ready == 0 || log_fp == NULL || message == NULL)
	return -1;

    err = pthread_mutex_lock(&log_mutex);
    if(err < 0) {
	fprintf(stderr, "(common) pthread_mutex_lock returned "
		"error: %d\n", err);
	return -1;
    }

    memset(&tv, 0, sizeof(struct timeval));
    gettimeofday(&tv, NULL);

    tm = localtime(&tv.tv_sec);


    /*
     * Format the date and time, down to a single second. 
     */

    strftime(ftime_str, 200, "%Y-%m-%d_%H:%M:%S", tm);

    err = fprintf(log_fp, "%s.%.6u: %s\n", ftime_str, (unsigned int)tv.tv_usec,
		  message);
    if(err <= 0) {
	perror("(common) fwrite");
	pthread_mutex_unlock(&log_mutex);
	return -1;
    }

    fflush(log_fp);

    err = pthread_mutex_unlock(&log_mutex);
    if(err < 0) {
	fprintf(stderr, "(common) pthread_mutex_unlock returned "
		"error: %d\n", err);
	return -1;
    }

    return 0;
}

int
log_append_file(char *filename) {
    int bytes, err;
    struct stat buf;
    FILE *fp;

    if(log_ready == 0 || log_fp == NULL || filename == NULL)
	return -1;

    memset(&buf, 0, sizeof(struct stat));
    err = stat(filename, &buf);
    if(err < 0) {
	fprintf(stderr, "(common) couldn't stat log file %s\n", filename);
	return -1;
    }

    bytes = buf.st_size;
    fp = fopen(filename, "r");
    if(fp == NULL) {
	perror("(common) fopen");
	return -1;
    }

    err = pthread_mutex_lock(&log_mutex);
    if(err < 0) {
	fprintf(stderr, "(common) pthread_mutex_lock returned error: %d\n",err);
	fclose(fp);
	return -1;
    }

    while(bytes > 0) {
	int num_read;
	char str[ARG_MAX];

	num_read = fread(str, 1, ARG_MAX, fp);
	if(num_read <= 0) 
	    continue;

	fwrite(str, num_read, 1, log_fp);

	bytes-=num_read;
    }

    fflush(log_fp);

    err = pthread_mutex_unlock(&log_mutex);
    if(err < 0) {
	fprintf(stderr, "(common) pthread_mutex_unlock returned error: %d\n",
		err);
	fclose(fp);
	return -1;
    }

    fclose(fp);

    return 0;
}

void
log_deinit(void) {
    int err;

    err = pthread_mutex_lock(&log_mutex);
    if(err < 0)
	fprintf(stderr, "(common) pthread_mutex_lock returned error: %d\n",err);

    fprintf(stderr, "(common) deinitializing log\n");

    log_ready = 0;
    if(log_fp != NULL) {
	fclose(log_fp);
	log_fp = NULL;
    }

    err = pthread_mutex_unlock(&log_mutex);
    if(err < 0)
	fprintf(stderr, "(common) pthread_mutex_unlock returned error: %d\n",
		err);
}


char *compress_file(const char *filename)
{
    int err;
    char command[ARG_MAX];
    char *nf;

    if (!filename || *filename == '\0')
	return NULL;

    nf = malloc(strlen(filename) + strlen(".gz") + 1);
    if (!nf) return NULL;

    strcpy(nf, filename);
    strcat(nf, ".gz");

    snprintf(command, ARG_MAX, "gzip -c -9 %s > %s", filename, nf);
    err = system(command);
    if(err < 0) {
	perror("system");
	return NULL;
    }
    return nf;
}


char *decompress_file(const char *filename)
{
    int err;
    char command[ARG_MAX];
    char *nf, *p;

    if (!filename || *filename == '\0')
	return NULL;

    snprintf(command, ARG_MAX, "gunzip %s", filename);
    err = system(command);
    if(err < 0) {
	perror("system");
	return NULL;
    }

    // remove .gz extension
    nf = strdup(filename);
    p = strrchr(nf, '.');
    if (p) *p = '\0';

    return nf;
}
