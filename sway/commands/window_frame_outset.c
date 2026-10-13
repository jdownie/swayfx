#include <string.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "log.h"

struct cmd_results *cmd_window_frame_outset(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "window_frame_outset", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	char *inv;
	int value = strtol(argv[0], &inv, 10);
	if (*inv != '\0' || value < 0) {
		return cmd_results_new(CMD_FAILURE, "Invalid size specified");
	}

	struct sway_container *con = config->handler_context.container;
	if (con == NULL) {
		config->window_frame_outset = value;
		arrange_root();
	} else {
		con->frame_outset = value;
		arrange_container(con);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}