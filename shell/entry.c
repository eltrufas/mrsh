#define _XOPEN_SOURCE 700
#include <mrsh/builtin.h>
#include <mrsh/entry.h>
#include <mrsh/shell.h>
#include <mrsh/parser.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "builtin.h"

static char *expand_parameter(struct mrsh_state *state, const char *src) {
	struct mrsh_parser *parser = mrsh_parser_with_data(src, strlen(src));
	if (parser == NULL) {
		return NULL;
	}
	struct mrsh_word *word = mrsh_parse_word(parser);
	mrsh_parser_destroy(parser);
	if (word == NULL) {
		return NULL;
	}
	mrsh_run_word(state, &word);
	char *str = mrsh_word_str(word);
	mrsh_word_destroy(word);
	return str;
}

char *mrsh_get_ps1(struct mrsh_state *state, int next_history_id) {
	// TODO: Replace ! with next history ID
	const char *ps1 = mrsh_env_get(state, "PS1", NULL);
	if (ps1 != NULL) {
		return expand_parameter(state, ps1);
	}
	char *p = malloc(3);
	sprintf(p, "%s", getuid() ? "$ " : "# ");
	return p;
}

char *mrsh_get_ps2(struct mrsh_state *state) {
	// TODO: Replace ! with next history ID
	const char *ps2 = mrsh_env_get(state, "PS2", NULL);
	if (ps2 != NULL) {
		return expand_parameter(state, ps2);
	}
	return strdup("> ");
}

char *mrsh_get_ps4(struct mrsh_state *state) {
	const char *ps4 = mrsh_env_get(state, "PS4", NULL);
	if (ps4 != NULL) {
		return expand_parameter(state, ps4);
	}
	return strdup("+ ");
}

bool mrsh_populate_env(struct mrsh_state *state, char **environ) {
	for (size_t i = 0; environ[i] != NULL; ++i) {
		char *eql = strchr(environ[i], '=');
		size_t klen = eql - environ[i];
		char *key = strndup(environ[i], klen);
		char *val = &eql[1];
		mrsh_env_set(state, key, val, MRSH_VAR_ATTRIB_EXPORT);
		free(key);
	}

	mrsh_env_set(state, "IFS", " \t\n", MRSH_VAR_ATTRIB_NONE);

	pid_t ppid = getppid();
	char ppid_str[24];
	snprintf(ppid_str, sizeof(ppid_str), "%d", ppid);
	mrsh_env_set(state, "PPID", ppid_str, MRSH_VAR_ATTRIB_NONE);

	// TODO check if path is well-formed, has . or .., and handle symbolic links
	const char *pwd = mrsh_env_get(state, "PWD", NULL);
	if (pwd == NULL || strlen(pwd) >= PATH_MAX) {
		char cwd[PATH_MAX];
		if (getcwd(cwd, PATH_MAX) == NULL) {
			perror("getcwd");
			return false;
		}
		mrsh_env_set(state, "PWD", cwd,
				MRSH_VAR_ATTRIB_EXPORT | MRSH_VAR_ATTRIB_READONLY);
	} else {
		mrsh_env_set(state, "PWD", pwd,
				MRSH_VAR_ATTRIB_EXPORT | MRSH_VAR_ATTRIB_READONLY);
	}

	mrsh_env_set(state, "OPTIND", "1", MRSH_VAR_ATTRIB_NONE);
	return true;
}

static void source_file(struct mrsh_state *state, char *path) {
	if (access(path, F_OK) == -1) {
		return;
	}
	char *env_argv[] = { ".", path };
	mrsh_run_builtin(state, sizeof(env_argv) / sizeof(env_argv[0]), env_argv);
}

void mrsh_source_profile(struct mrsh_state *state) {
	source_file(state, "/etc/profile");

	char path[PATH_MAX];
	int n = snprintf(path, sizeof(path), "%s/.profile", getenv("HOME"));
	if (n == sizeof(path)) {
		fprintf(stderr, "Warning: $HOME/.profile is longer than PATH_MAX\n");
		return;
	}
	source_file(state, path);
}

void mrsh_source_env(struct mrsh_state *state) {
	char *path = getenv("ENV");
	if (path == NULL) {
		return;
	}
	if (getuid() != geteuid() || getgid() != getegid()) {
		return;
	}
	path = expand_parameter(state, path);
	if (path[0] != '/') {
		fprintf(stderr, "Error: $ENV is not an absolute path; "
				"this is undefined behavior.\n");
		fprintf(stderr, "Continuing without sourcing it.\n");
	} else {
		source_file(state, path);
	}
	free(path);
}
