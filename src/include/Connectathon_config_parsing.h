#ifndef _CONFIG_PARSING_H
#define _CONFIG_PARSING_H

enum test_number {
	ONE = 1,
	TWO,
	THREE,
	FOUR,
	FIVE,
	SIX,
	SEVEN,
	EIGHT,
	NINE
};

struct btest {
	enum test_number num;
	enum test_number num2;

	int levels;
	int files;
	int dirs;
	int count;
	int size;
	int blocksize;

	char *bigfile;

	char *fname;
	char *dname;
	char *nname;
	char *sname;

	struct btest *nextbtest;
};

struct testparam {
	char *dirtest;
	char *logfile;
	struct btest *btest;
};

void btest_init_defaults(struct btest *b);
void testparam_init_defaults(struct testparam *t);

void free_testparam(struct testparam *t);

char *get_test_directory(struct testparam *t);
char *get_log_file(struct testparam *t);
struct btest *get_btest_args(struct testparam *param, enum test_number k);

struct testparam *readin_config(char *fname);

#endif
