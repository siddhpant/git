# Helpers for scripts testing notes behavior.

# notes.externalCommand is run through a shell, so quote the path.
external_notes_command=$(
	printf "%s\n" "$TEST_DIRECTORY/helper/test-external-notes" |
	sed "s/'/'\\\\''/g; s/^/'/; s/$/'/"
)

# The helper above is a shell script. Few Windows CI tests (3 out of 10
# in matrix) are spending more than the production default timeout just
# starting the shell and exchanging the first response, so tests that
# are not about timeout behavior fail. So let us opt into a wider 1s
# deadline for Windows instead of 100ms.
external_notes_command_timeout_config=
if test_have_prereq MINGW
then
	_timeout_config="notes.externalCommandTimeoutMs=1000"
	external_notes_command_timeout_config="-c $_timeout_config"
fi
