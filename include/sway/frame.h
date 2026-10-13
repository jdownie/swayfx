#ifndef _SWAY_FRAME_H
#define _SWAY_FRAME_H

#include <stdbool.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_scene.h>

struct sway_container;

enum window_frame_image {
	FRAME_IMAGE_TOP_LEFT = 0,
	FRAME_IMAGE_TOP,
	FRAME_IMAGE_TOP_RIGHT,
	FRAME_IMAGE_LEFT,
	FRAME_IMAGE_RIGHT,
	FRAME_IMAGE_BOTTOM_LEFT,
	FRAME_IMAGE_BOTTOM,
	FRAME_IMAGE_BOTTOM_RIGHT,
	FRAME_IMAGE_COUNT
};

struct frame_image {
	struct wlr_buffer *buffer;
	int width;
	int height;
};

struct frame_image *frame_image_load(const char *path);
void frame_image_unload(struct frame_image *img);

void frame_arrange(struct sway_container *con, int width, int height);
void frame_destroy_nodes(struct sway_container *con);
void frame_reload_images(struct sway_container *con);

bool container_has_frame(struct sway_container *con);
const char *frame_get_image_path(struct sway_container *con,
		enum window_frame_image idx);

#endif