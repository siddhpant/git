#include "test-tool.h"
#include "notes-external.h"

int cmd__notes_external_config_reset(int argc, const char **argv UNUSED)
{
	struct external_notes_state *state;

	if (argc != 1)
		die("usage: test-tool notes-external-config-reset");

	state = external_notes_new();
	set_external_notes_command(state, "helper");
	set_external_notes_command_name(state, "label");
	set_external_notes_command_timeout_ms(state, 250);
	set_external_notes_for_grep(state, 1);
	external_notes_reset(state);

	printf("configured=%d\n", external_notes_command_configured(state));
	printf("name=%s\n", external_notes_command_name(state));
	printf("timeout_ms=%d\n", external_notes_command_timeout_ms(state));
	printf("grep=%d\n", external_notes_for_grep_enabled(state));
	external_notes_free(state);
	return 0;
}
