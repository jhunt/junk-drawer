#ifndef _LANG_H
#define _LANG_H

/*
  lang_file - Keep track of 'seen' files, by dev/inode

  Whenver the lexer encounters an 'include' macro, it needs
  to evaluate whether or not it has already seen that file,
  to avoid inclusion loops.  It also needs to keep track of
  the FILE* associated with the included file, so that it can
  be closed when the file has been completely read (otherwise,
  we leak the memory attached to the file handle).

  Device ID and Inode must be used in conjunction, in case an
  include macro reaches across filesystems / devices.
 */
typedef struct {
	dev_t           st_dev;        /* Device ID (filesystem) */
	ino_t           st_ino;        /* File Inode number */
	FILE           *io;            /* Open file handle */

	struct list     ls;            /* For stacking parser_files */
} lang_file;

/**
  lang_context - User data for reentrant lexer / parser

  This structure bundles a bunch of variables that we need to keep
  track of while lexing and parsing.  An instance of this structure
  gets passed to yyparse, which propagates it through to yylex.

  The scanner member is an opaque pointer, allocated by yylex_init and
  freed by yylex_destroy.  It contains whatever Flex needs it to, and is
  used in place of global variables (like yylineno, yyleng, and friends).

  The data member is a pointer that can be used by the grammar, and the
  callers of the parser to store and retrieve some application-specific
  and language-specific data.
 */
typedef struct {
	void              *scanner;  /* lexer variable store; used instead of globals */
	void              *data;     /* user data */

	unsigned int       warnings; /* Number of times spec_parser_warning called */
	unsigned int       errors;   /* Number of times spec_parser_error called */

	const char        *file;     /* Name of the current file being parsed */
	struct stringlist *files;    /* "Stack" of file names processed so far */
	struct list        fseen;    /* List of device ID / inode pairs already include'd */
} lang_context;


void lang_warning(void*, const char *fmt, ...);
void lang_error(void*, const char *fmt, ...);

FILE *lang_open(const char *path, lang_context*);
int lang_close(lang_context*);

void lang_process_file(const char *path, lang_context*);
char* lang_resolve_path_spec(const char *current_file, const char *path);

void lang_include(const char *path, lang_context*);
int  lang_include_done(lang_context*);

#endif
