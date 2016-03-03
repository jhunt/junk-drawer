/* ==[ %{ROOT}/private.h ]========================================= */
#include "clockwork.h"

/*
  Keep track of 'seen' files, by dev/inode

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

	struct list     ls;            /* For stacking %{LANGUAGE}_files */
} %{LANGUAGE}_file;


/**
  User data for reentrant lexer / parser

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
} %{LANGUAGE}_context;

/**************************************************************/

#define LANG yyget_extra(yyscanner)
void %{LANGUAGE}_warning(void*, const char *fmt, ...);
void %{LANGUAGE}_error(void*, const char *fmt, ...);

void %{LANGUAGE}_include(const char *path, %{LANGUAGE}_context*);
int  %{LANGUAGE}_include_done(%{LANGUAGE}_context*);
/* ==[ %{ROOT}/lexer_impl.c ]====================================== */
#include <libgen.h>
#include <stdio.h>
#include <glob.h>
#include <sys/stat.h>
#include <stdarg.h>

void %{LANGUAGE}_warning(void *user, const char *fmt, ...);
void %{LANGUAGE}_error(void *user, const char *fmt, ...);

/**************************************************************/

static FILE* %{LANGUAGE}_open(const char *path, %{LANGUAGE}_context *ctx)
{
	FILE *io;
	struct stat st;
	%{LANGUAGE}_file *seen;

	if (stat(path, &st) != 0) {
		/* NOTE: strerror not reentrant */
		%{LANGUAGE}_error(ctx, "can't stat %s: %s", path, strerror(errno));
		return NULL;
	}

	if (!S_ISREG(st.st_mode)) {
		%{LANGUAGE}_error(ctx, "can't open %s: not a regular file", path);
		return NULL;
	}

	/* Combination of st_dev and st_ino is guaranteed to be unique
	   on any system conforming to the POSIX specifications... */
	for_each_node(seen, &ctx->fseen, ls) {
		if (seen->st_dev == st.st_dev
		 && seen->st_ino == st.st_ino) {
			%{LANGUAGE}_warning(ctx, "skipping %s (already seen)", path);
			return NULL;
		}
	}

	io = fopen(path, "r");
	if (!io) {
		%{LANGUAGE}_error(ctx, "can't open %s: %s", path, strerror(errno));
		return NULL;
	}

	seen = malloc(sizeof(%{LANGUAGE}_file));
	if (!seen) {
		%{LANGUAGE}_error(ctx, "out of memory");
		fclose(io);
		return NULL;
	}

	seen->st_dev = st.st_dev;
	seen->st_ino = st.st_ino;
	seen->io     = io;
	list_init(&(seen->ls));
	list_add_tail(&seen->ls, &ctx->fseen);
	return io;
}

static int %{LANGUAGE}_close(%{LANGUAGE}_context *ctx)
{
	%{LANGUAGE}_file *seen;
	for_each_node_r(seen, &ctx->fseen, ls) {
		if (seen && seen->io) {
			fclose(seen->io);
			seen->io = NULL;
			return 0;
		}
	}
	return -1;
}

static void %{LANGUAGE}_process_file(const char *path, %{LANGUAGE}_context *ctx)
{
	FILE *io;
	void *buf;

	io = %{LANGUAGE}_open(path, ctx);
	if (!io) { /* already seen or some other error */
		return; /* bail; %{LANGUAGE}_check_file already printed warnings */
	}

	buf = yy%{LANGUAGE}_create_buffer(io, YY_BUF_SIZE, (ctx)->scanner);
	yy%{LANGUAGE}push_buffer_state(buf, ctx->scanner);

	stringlist_add(ctx->files, path);
	ctx->file = ctx->files->strings[ctx->files->num-1];
}

/* Resolves a relative path spec (path) based on the parent
   directory of the current file being processed.  On the first
   run (when current_file is NULL) returns a copy of path,
   unmodified. */
static char* %{LANGUAGE}_resolve_path_spec(const char *current_file, const char *path)
{
	char *file_dup, *base, *full_path;

	if (!current_file || path[0] == '/') {
		return strdup(path);
	}

	/* make a copy of current_file, since dirname(3)
	   _may_ modify its path argument. */
	file_dup = strdup(current_file);
	base = dirname(file_dup);

	full_path = string("%s/%s", base, path);
	free(file_dup);

	return full_path;
}

/**************************************************************/

void %{LANGUAGE}_warning(void *user, const char *fmt, ...)
{
	char buf[256];
	va_list args;
	%{LANGUAGE}_context *ctx = (%{LANGUAGE}_context*)user;

	va_start(args, fmt);
	if (vsnprintf(buf, 256, fmt, args) < 0) {
		CRITICAL("%s:%u: error: vsnprintf failed in %{LANGUAGE}_warning",
		                ctx->file, yy%{LANGUAGE}get_lineno(ctx->scanner));
		ctx->errors++; /* treat this as an error */
	} else {
		CRITICAL("%s:%u: warning: %s", ctx->file, yy%{LANGUAGE}get_lineno(ctx->scanner), buf);
		ctx->warnings++; 
	}
}

void %{LANGUAGE}_error(void *user, const char *fmt, ...)
{
	char buf[256];
	va_list args;
	%{LANGUAGE}_context *ctx = (%{LANGUAGE}_context*)user;

	va_start(args, fmt);
	if (vsnprintf(buf, 256, fmt, args) < 0) {
		CRITICAL("%s:%u: error: vsnprintf failed in %{LANGUAGE}_error",
		                ctx->file, yy%{LANGUAGE}get_lineno(ctx->scanner));
	} else {
		CRITICAL("%s:%u: error: %s", ctx->file, yy%{LANGUAGE}get_lineno(ctx->scanner), buf);
	}
	ctx->errors++;
}

void %{LANGUAGE}_include(const char *path, %{LANGUAGE}_context *ctx)
{
	glob_t expansion;
	size_t i;
	char *full_path;
	int rc;

	full_path = %{LANGUAGE}_resolve_path_spec(ctx->file, path);
	if (!full_path) {
		%{LANGUAGE}_error(ctx, "Unable to resolve absolute path of %s", path);
		return;
	}

	rc = glob(full_path, GLOB_MARK, NULL, &expansion);
	free(full_path);

	switch (rc) {
	case GLOB_NOMATCH:
		/* Treat path as a regular file path, not a glob */
		%{LANGUAGE}_process_file(path, ctx);
		return;
	case GLOB_NOSPACE:
		%{LANGUAGE}_error(ctx, "out of memory in glob expansion");
		return;
	case GLOB_ABORTED:
		%{LANGUAGE}_error(ctx, "read aborted during glob expansion");
		return;
	}

	/* Process each the sorted list in gl_pathv, in reverse, so
	   that when buffers are popped off the stack, they come out
	   in alphabetical order.

	   n.b. size_t is unsigned; (size_t)(-1) > 0 */
	for (i = expansion.gl_pathc; i > 0; i--) {
		%{LANGUAGE}_process_file(expansion.gl_pathv[i-1], ctx);
	}
	globfree(&expansion);
}

int %{LANGUAGE}_include_done(%{LANGUAGE}_context *ctx)
{
	stringlist_remove(ctx->files, ctx->file);
	%{LANGUAGE}_close(ctx);

	if (ctx->files->num == 0) {
		return -1; /* no more files */
	}

	yy%{LANGUAGE}pop_buffer_state(ctx->scanner);
	ctx->file = ctx->files->strings[ctx->files->num-1];
	return 0; /* keep going */
}
/* ==[ %{ROOT}/parser.h ]======================================= */
/*
  Copyright 2011-2013 James Hunt <james@niftylogic.com>

  This file is part of Clockwork.

  Clockwork is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Clockwork is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Clockwork.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _%{LANGUAGE}_PARSER_H
#define _%{LANGUAGE}_PARSER_H

void* %{LANGUAGE}_parse_file(const char *path, void *data);

#endif
/* ==[ %{ROOT}/parser.c ]======================================= */
/*
  Copyright 2011-2013 James Hunt <james@niftylogic.com>

  This file is part of Clockwork.

  Clockwork is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Clockwork is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Clockwork.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <sys/stat.h>

#include "private.h"
#include "parser.h"
#include "../clockwork.h"
#include "../cog.h"

void* %{LANGUAGE}_parse_file(const char *path, void *data)
{
	%{LANGUAGE}_context ctx;
	struct stat init_stat;
	%{LANGUAGE}_file *seen, *tmp;

	/* check the file first. */
	if (stat(path, &init_stat) != 0) {
		//return NULL;
		return;
	}

	ctx.data = data;
	ctx.file = NULL;
	ctx.warnings = ctx.errors = 0;
	ctx.files = stringlist_new(NULL);
	list_init(&ctx.fseen);

	yy%{LANGUAGE}lex_init_extra(&ctx, &ctx.scanner);
	%{LANGUAGE}_include_file(path, &ctx);
	yy%{LANGUAGE}parse(&ctx);

	yy%{LANGUAGE}lex_destroy(ctx.scanner);
	stringlist_free(ctx.files);
	for_each_node_safe(seen, tmp, &ctx.fseen, ls) {
		free(seen);
	}

	if (ctx.errors > 0) {
		return NULL;
	}

	return ctx.data;
}
