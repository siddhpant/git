#ifndef NOTES_EXTERNAL_H
#define NOTES_EXTERNAL_H

#include "run-command.h"

struct object_id;
struct strbuf;

struct external_notes_config {
	char *command;
	char *command_name_value;
	uint64_t read_timeout_ns;
	bool for_grep;
};

struct external_notes_process {
	struct child_process process;
	FILE *in;
	int out_fd;
	bool started;
	bool failed;
};

struct external_notes_state {
	struct external_notes_config config;
	struct external_notes_process process;
};

struct external_notes_state *external_notes_new(void);
void external_notes_free(struct external_notes_state *state);
void external_notes_reset(struct external_notes_state *state);

void set_external_notes_command(struct external_notes_state *state,
				const char *command);
bool external_notes_command_configured(const struct external_notes_state *state);

void set_external_notes_command_name(struct external_notes_state *state,
				     const char *name);
const char *external_notes_command_name(const struct external_notes_state *state);

void set_external_notes_command_timeout_ms(struct external_notes_state *state,
					   int timeout_ms);
int external_notes_command_timeout_ms(const struct external_notes_state *state);

void set_external_notes_for_grep(struct external_notes_state *state,
				 int enabled);
bool external_notes_for_grep_enabled(const struct external_notes_state *state);

int format_external_note(struct external_notes_state *state,
			 const struct object_id *object_oid,
			 struct strbuf *out);

#endif  /* NOTES_EXTERNAL_H */
