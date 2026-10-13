#include <string.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/frame.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "log.h"
#include "stringop.h"

static struct cmd_results *cmd_window_frame_image(int argc, char **argv,
		enum window_frame_image idx) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "window_frame_image", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	struct sway_container *con = config->handler_context.container;
	char *path = join_args(argv, argc);

	if (con == NULL) {
		char **cfg_path = NULL;
		switch (idx) {
		case FRAME_IMAGE_TOP_LEFT: cfg_path = &config->window_frame_image_top_left; break;
		case FRAME_IMAGE_TOP: cfg_path = &config->window_frame_image_top; break;
		case FRAME_IMAGE_TOP_RIGHT: cfg_path = &config->window_frame_image_top_right; break;
		case FRAME_IMAGE_LEFT: cfg_path = &config->window_frame_image_left; break;
		case FRAME_IMAGE_RIGHT: cfg_path = &config->window_frame_image_right; break;
		case FRAME_IMAGE_BOTTOM_LEFT: cfg_path = &config->window_frame_image_bottom_left; break;
		case FRAME_IMAGE_BOTTOM: cfg_path = &config->window_frame_image_bottom; break;
		case FRAME_IMAGE_BOTTOM_RIGHT: cfg_path = &config->window_frame_image_bottom_right; break;
		default: free(path); return cmd_results_new(CMD_FAILURE, "Invalid frame image index");
		}
		free(*cfg_path);
		*cfg_path = strdup(path);
		free(path);
		arrange_root();
	} else {
		free(con->frame_image_paths[idx]);
		con->frame_image_paths[idx] = strdup(path);
		free(path);
		arrange_container(con);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_window_frame_image_top_left(int argc, char **argv) {
	return cmd_window_frame_image(argc, argv, FRAME_IMAGE_TOP_LEFT);
}
struct cmd_results *cmd_window_frame_image_top(int argc, char **argv) {
	return cmd_window_frame_image(argc, argv, FRAME_IMAGE_TOP);
}
struct cmd_results *cmd_window_frame_image_top_right(int argc, char **argv) {
	return cmd_window_frame_image(argc, argv, FRAME_IMAGE_TOP_RIGHT);
}
struct cmd_results *cmd_window_frame_image_left(int argc, char **argv) {
	return cmd_window_frame_image(argc, argv, FRAME_IMAGE_LEFT);
}
struct cmd_results *cmd_window_frame_image_right(int argc, char **argv) {
	return cmd_window_frame_image(argc, argv, FRAME_IMAGE_RIGHT);
}
struct cmd_results *cmd_window_frame_image_bottom_left(int argc, char **argv) {
	return cmd_window_frame_image(argc, argv, FRAME_IMAGE_BOTTOM_LEFT);
}
struct cmd_results *cmd_window_frame_image_bottom(int argc, char **argv) {
	return cmd_window_frame_image(argc, argv, FRAME_IMAGE_BOTTOM);
}
struct cmd_results *cmd_window_frame_image_bottom_right(int argc, char **argv) {
	return cmd_window_frame_image(argc, argv, FRAME_IMAGE_BOTTOM_RIGHT);
}