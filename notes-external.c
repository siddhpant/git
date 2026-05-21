#include "git-compat-util.h"
#include "gettext.h"
#include "hex.h"
#include "notes-external.h"
#include "run-command.h"
#include "sigchain.h"
#include "strbuf.h"
#include "trace.h"

#define convert_ms_to_ns(ms) (uint64_t)(ms) * 1000000ULL
#define convert_ns_to_ms(ns) (uint64_t)(ns) / 1000000ULL
#define EXTERNAL_NOTES_KILL_GRACE_NS 100000000ULL	/* 100 ms = 10^8 ns */
#define EXTERNAL_NOTES_READ_CHUNK_SIZE 16384	/* (16 * 1024) bytes */

/* Configuration helpers. */

static struct {
	char *command;
	char *command_name_value;
	uint64_t read_timeout_ns;
	bool for_grep;
	bool started;
	bool failed;
} config = {
	.read_timeout_ns = convert_ms_to_ns(100),
};

void set_external_notes_command(const char *command)
{
	FREE_AND_NULL(config.command);
	if (command && *command)
		config.command = xstrdup(command);
}

bool external_notes_command_configured(void)
{
	return config.command && !config.failed;
}

void reset_external_notes_command(void)
{
	if (config.started)
		BUG("cannot reset external notes config while cmd is running");

	FREE_AND_NULL(config.command);
	FREE_AND_NULL(config.command_name_value);
	config.read_timeout_ns = convert_ms_to_ns(100);
	config.for_grep = false;
	config.failed = false;
}

void set_external_notes_command_name(const char *name)
{
	FREE_AND_NULL(config.command_name_value);
	if (name && *name)
		config.command_name_value = xstrdup(name);
}

const char *external_notes_command_name(void)
{
	return config.command_name_value ?
		config.command_name_value : "external";
}

void set_external_notes_command_timeout_ms(int timeout_ms)
{
	if (timeout_ms < 0)
		BUG("negative notes.externalCommandTimeoutMs");

	config.read_timeout_ns = convert_ms_to_ns(timeout_ms);
}

int external_notes_command_timeout_ms(void)
{
	return (int)convert_ns_to_ms(config.read_timeout_ns);
}

void set_external_notes_for_grep(int enabled)
{
	config.for_grep = (bool)enabled;
}

bool external_notes_for_grep_enabled(void)
{
	return config.for_grep;
}

/* Process management helpers. */

static struct child_process external_notes_process = CHILD_PROCESS_INIT;
static FILE *external_notes_in;
static int external_notes_out_fd = -1;

static void mute_routine(const char *msg UNUSED, va_list params UNUSED)
{
	/* do nothing */
}

static void close_external_notes_process_pipes(struct child_process *process)
{
	sigchain_push(SIGPIPE, SIG_IGN);

	if (external_notes_in) {
		fclose(external_notes_in);
		external_notes_in = NULL;
	} else {
		close(process->in);
	}

	if (external_notes_out_fd >= 0) {
		close(external_notes_out_fd);
		external_notes_out_fd = -1;
	} else {
		close(process->out);
	}

	sigchain_pop(SIGPIPE);
}

/* We set this as callback later, so can't have void argument. */
static void cleanup_external_notes_process(struct child_process *process)
{
	report_fn old_error = NULL;

	/**
	 * The helper may still be sleeping with its pipes open, or may not
	 * exit promptly after EOF. Ask it to stop, then use a bounded wait
	 * that escalates if it ignores the signal.
	 */
	kill(process->pid, SIGTERM);
	old_error = get_error_routine();
	set_error_routine(mute_routine);

	close_external_notes_process_pipes(process);
	finish_command_with_timeout(process, EXTERNAL_NOTES_KILL_GRACE_NS);

	if (old_error)
		set_error_routine(old_error);

	config.started = false;
}

static void stop_external_notes_process(void)
{
	if (!config.started)
		return;

	external_notes_process.clean_on_exit = 0;
	cleanup_external_notes_process(&external_notes_process);
	child_process_init(&external_notes_process);
}

static int fail_external_notes_command(void)
{
	if (!config.failed)
		warning(_("notes.externalCommand failed: %s"),
			config.command);

	config.failed = true;
	stop_external_notes_process();
	return -1;
}

static int start_external_notes_command(void)
{
	struct child_process *cmd = &external_notes_process;

	if (config.started)
		return 0;

	if (!config.command || config.failed)
		return -1;

	child_process_init(cmd);
	strvec_push(&cmd->args, config.command);
	cmd->use_shell = 1;
	cmd->in = -1;
	cmd->out = -1;
	cmd->clean_on_exit = 1;
	cmd->clean_on_exit_handler = cleanup_external_notes_process;
	cmd->trace2_child_class = "notes-external";

	if (start_command(cmd))
		return fail_external_notes_command();

	external_notes_in = xfdopen(cmd->in, "wb");
	external_notes_out_fd = cmd->out;
	config.started = true;
	return 0;
}

/* Command parser. Essentially the main() function of this file. */
int format_external_note(const struct object_id *object_oid,
			 struct strbuf *note_buf)
{
	struct strbuf status = STRBUF_INIT;
	char commit_id_hex_str[GIT_MAX_HEXSZ + 1];
	const char *arg;
	char *end;
	char ch;
	unsigned long len;
	uint64_t deadline_ns;
	bool input_fail;
	int ret = 0;

	if (start_external_notes_command())
		return -1;

	/* Fetch the commit ID hex. */
	oid_to_hex_r(commit_id_hex_str, object_oid);

	/* Pass the input to the external command. */
	sigchain_push(SIGPIPE, SIG_IGN);
	input_fail = fprintf(external_notes_in, "%s\n", commit_id_hex_str) < 0
		     || fflush(external_notes_in) != 0;
	sigchain_pop(SIGPIPE);

	if (input_fail)
		goto out_fail;

	if (config.read_timeout_ns == 0)
		deadline_ns = 0;
	else
		deadline_ns = getnanotime() + config.read_timeout_ns;

	/**
	 * The output for each commit is either of the two:
	 * 	"{commit id} missing\n"
	 * 	"{commit id} ok {num_bytes}\n{str_of_num_bytes}\n"
	 *
	 * We can have "\r\n" instead of "\n" due to Windows.
	 */

	/* Read the first line with its delimiter. */
	if (strbuf_getwholeline_fd_deadline(&status, external_notes_out_fd,
					    '\n', deadline_ns) == EOF)
		goto out_fail;

	/* Reject EOF-terminated partial lines. */
	if (!status.len || status.buf[status.len - 1] != '\n')
		goto out_fail;

	/**
	 * Strip LF and then optional CR so both LF and CRLF protocol lines
	 * are accepted.
	 */
	strbuf_setlen(&status, status.len - 1);
	strbuf_strip_suffix(&status, "\r");

	/* Check if line starts with the commit ID. */
	if (!skip_prefix(status.buf, commit_id_hex_str, &arg))
		goto out_fail;

	if (*arg++ != ' ')  /* After commit ID there should be a space. */
		goto out_fail;

	if (strcmp(arg, "missing") == 0)  /* No note available. */
		goto out_success;  /* Ending newline is already ensured. */

	if (!skip_prefix(arg, "ok ", &arg))  /* Neither missing nor ok. */
		goto out_fail;

	/* We are in "ok" case. */

	/* The next thing is length of the note. It must be unsigned digits. */
	if (!isdigit(*arg))
		goto out_fail;

	/* Get the length of note. */
	errno = 0;
	len = strtoul(arg, &end, 10);
	if (errno != 0 || *end != '\0' || end == arg)
		goto out_fail;

	/* Ending newline is already ensured. */

	/* Read the trailing note in bounded-chunks. */
	while (note_buf->len < len) {
		ssize_t got;
		size_t remaining = len - note_buf->len;
		size_t want = remaining < EXTERNAL_NOTES_READ_CHUNK_SIZE ?
			      remaining : EXTERNAL_NOTES_READ_CHUNK_SIZE;

		strbuf_grow(note_buf, want);

		got = read_in_full_deadline(external_notes_out_fd,
					    note_buf->buf + note_buf->len,
					    want, deadline_ns);
		if (got < 0 || (size_t)got != want)
			goto out_fail;

		strbuf_setlen(note_buf, note_buf->len + (size_t)got);
	}

	/* Ensure the ending newline (LF/CRLF) after the note. */
	if (xread_deadline(external_notes_out_fd, &ch, 1, deadline_ns) != 1)
		goto out_fail;

	if (ch != '\n') {  /* Not a LF. */
		if (ch != '\r')  /* Not a CRLF. */
			goto out_fail;

		/* We have '\r', let's read the next char. */
		if (xread_deadline(external_notes_out_fd, &ch, 1,
				   deadline_ns) != 1)
			goto out_fail;

		if (ch != '\n')  /* Not a CRLF. */
			goto out_fail;
	}

	goto out_success;

out_fail:
	ret = fail_external_notes_command();
out_success:
	strbuf_release(&status);
	return ret;
}
