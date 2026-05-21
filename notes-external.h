#ifndef NOTES_EXTERNAL_H
#define NOTES_EXTERNAL_H

struct object_id;
struct strbuf;

void set_external_notes_command(const char *command);
bool external_notes_command_configured(void);
void reset_external_notes_command(void);

void set_external_notes_command_name(const char *name);
const char *external_notes_command_name(void);

void set_external_notes_command_timeout_ms(int timeout_ms);
int external_notes_command_timeout_ms(void);

void set_external_notes_for_grep(int enabled);
bool external_notes_for_grep_enabled(void);

int format_external_note(const struct object_id *object_oid,
			 struct strbuf *out);

#endif  /* NOTES_EXTERNAL_H */
