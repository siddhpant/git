#include "test-tool.h"
#include "notes-external.h"

int cmd__notes_external_config_reset(int argc, const char **argv UNUSED)
{
	if (argc != 1)
		die("usage: test-tool notes-external-config-reset");

	set_external_notes_command("helper");
	set_external_notes_command_name("label");
	set_external_notes_command_timeout_ms(250);
	set_external_notes_for_grep(1);
	reset_external_notes_command();

	printf("configured=%d\n", external_notes_command_configured());
	printf("name=%s\n", external_notes_command_name());
	printf("timeout_ms=%d\n", external_notes_command_timeout_ms());
	printf("grep=%d\n", external_notes_for_grep_enabled());
	return 0;
}
