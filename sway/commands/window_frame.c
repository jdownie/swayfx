#include "sway/commands.h"
#include "sway/config.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"
#include "util.h"

static void arrange_frame_iter(struct sway_container *con, void *data) {
	con->frame_enabled = config->window_frame_enabled;
}

struct cmd_results *cmd_window_frame(int argc, char **argv) {
	struct cmd_results *error = checkarg(argc, "window_frame", EXPECTED_AT_LEAST, 1);

	if (error) {
		return error;
	}

	struct sway_container *con = config->handler_context.container;
	bool result = parse_boolean(argv[0], true);

	if (con == NULL) {
		config->window_frame_enabled = result;
		root_for_each_container(arrange_frame_iter, NULL);
		arrange_root();
	} else {
		con->frame_enabled = result;
		arrange_container(con);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}