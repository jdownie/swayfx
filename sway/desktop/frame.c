#define _POSIX_C_SOURCE 200809L
#include <cairo.h>
#include <drm_fourcc.h>
#include <scenefx/types/wlr_scene.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include "sway/config.h"
#include "sway/frame.h"
#include "sway/tree/container.h"
#include "list.h"
#include "log.h"

struct pixel_buffer {
	struct wlr_buffer base;
	unsigned char *data;
	size_t size;
};

static void pixel_buffer_destroy(struct wlr_buffer *wlr_buffer) {
	struct pixel_buffer *buf = wl_container_of(wlr_buffer, buf, base);
	wlr_buffer_finish(wlr_buffer);
	free(buf->data);
	free(buf);
}

static bool pixel_buffer_get_dmabuf(struct wlr_buffer *wlr_buffer,
		struct wlr_dmabuf_attributes *attribs) {
	return false;
}

static bool pixel_buffer_get_shm(struct wlr_buffer *wlr_buffer,
		struct wlr_shm_attributes *attribs) {
	return false;
}

static bool pixel_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
		uint32_t flags, void **data, uint32_t *format, size_t *stride) {
	struct pixel_buffer *buf = wl_container_of(wlr_buffer, buf, base);
	*data = buf->data;
	*format = DRM_FORMAT_ARGB8888;
	*stride = buf->base.width * 4;
	return true;
}

static void pixel_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
}

static const struct wlr_buffer_impl pixel_buffer_impl = {
	.destroy = pixel_buffer_destroy,
	.get_dmabuf = pixel_buffer_get_dmabuf,
	.get_shm = pixel_buffer_get_shm,
	.begin_data_ptr_access = pixel_buffer_begin_data_ptr_access,
	.end_data_ptr_access = pixel_buffer_end_data_ptr_access,
};

static struct wlr_buffer *pixel_buffer_create(int width, int height,
		unsigned char *data, size_t data_size) {
	struct pixel_buffer *buf = calloc(1, sizeof(*buf));
	if (!buf) {
		return NULL;
	}
	buf->data = data;
	buf->size = data_size;
	wlr_buffer_init(&buf->base, &pixel_buffer_impl, width, height);
	return &buf->base;
}

struct frame_image *frame_image_load(const char *path) {
	if (!path) {
		return NULL;
	}

	cairo_surface_t *surface = cairo_image_surface_create_from_png(path);
	cairo_status_t status = cairo_surface_status(surface);
	if (status != CAIRO_STATUS_SUCCESS) {
		sway_log(SWAY_ERROR, "Failed to load frame image '%s': %s",
				path, cairo_status_to_string(status));
		cairo_surface_destroy(surface);
		return NULL;
	}

	int width = cairo_image_surface_get_width(surface);
	int height = cairo_image_surface_get_height(surface);
	int stride = cairo_image_surface_get_stride(surface);
	unsigned char *src_data = cairo_image_surface_get_data(surface);

	if (width <= 0 || height <= 0) {
		cairo_surface_destroy(surface);
		return NULL;
	}

	size_t data_size = (size_t)height * stride;
	unsigned char *data_copy = malloc(data_size);
	if (!data_copy) {
		cairo_surface_destroy(surface);
		return NULL;
	}
	memcpy(data_copy, src_data, data_size);
	cairo_surface_destroy(surface);

	struct wlr_buffer *buffer = pixel_buffer_create(width, height,
			data_copy, data_size);
	if (!buffer) {
		free(data_copy);
		return NULL;
	}

	struct frame_image *img = calloc(1, sizeof(*img));
	if (!img) {
		wlr_buffer_drop(buffer);
		free(data_copy);
		return NULL;
	}
	img->buffer = buffer;
	img->width = width;
	img->height = height;
	return img;
}

void frame_image_unload(struct frame_image *img) {
	if (!img) {
		return;
	}
	wlr_buffer_drop(img->buffer);
	free(img);
}

static struct frame_image *frame_load_cached(struct sway_container *con,
		enum window_frame_image idx) {
	const char *path = frame_get_image_path(con, idx);
	if (!path || !path[0]) {
		return NULL;
	}
	return frame_image_load(path);
}

static void frame_clear_tiles(list_t *tiles) {
	if (!tiles) {
		return;
	}
	for (int i = 0; i < tiles->length; i++) {
		struct wlr_scene_buffer *sb = tiles->items[i];
		wlr_scene_node_destroy(&sb->node);
	}
	list_free(tiles);
}

static void frame_ensure_tiles(list_t **tiles_ptr,
		struct wlr_scene_tree *parent, int count) {
	if (!*tiles_ptr) {
		*tiles_ptr = create_list();
	}
	list_t *tiles = *tiles_ptr;

	while (tiles->length > count) {
		struct wlr_scene_buffer *sb = tiles->items[tiles->length - 1];
		wlr_scene_node_destroy(&sb->node);
		list_del(tiles, tiles->length - 1);
	}
	while (tiles->length < count) {
		struct wlr_scene_buffer *sb = wlr_scene_buffer_create(parent, NULL);
		if (sb) {
			list_add(tiles, sb);
		}
	}
}

static void frame_ensure_corner(struct wlr_scene_buffer **node,
		struct wlr_scene_tree *parent) {
	if (!*node) {
		*node = wlr_scene_buffer_create(parent, NULL);
	}
}

bool container_has_frame(struct sway_container *con) {
	return con->frame_enabled &&
		(frame_get_image_path(con, FRAME_IMAGE_TOP_LEFT) ||
		 frame_get_image_path(con, FRAME_IMAGE_TOP) ||
		 frame_get_image_path(con, FRAME_IMAGE_TOP_RIGHT) ||
		 frame_get_image_path(con, FRAME_IMAGE_LEFT) ||
		 frame_get_image_path(con, FRAME_IMAGE_RIGHT) ||
		 frame_get_image_path(con, FRAME_IMAGE_BOTTOM_LEFT) ||
		 frame_get_image_path(con, FRAME_IMAGE_BOTTOM) ||
		 frame_get_image_path(con, FRAME_IMAGE_BOTTOM_RIGHT));
}

const char *frame_get_image_path(struct sway_container *con,
		enum window_frame_image idx) {
	if (idx >= FRAME_IMAGE_COUNT) {
		return NULL;
	}
	if (con->frame_image_paths[idx]) {
		return con->frame_image_paths[idx];
	}
	switch (idx) {
	case FRAME_IMAGE_TOP_LEFT: return config->window_frame_image_top_left;
	case FRAME_IMAGE_TOP: return config->window_frame_image_top;
	case FRAME_IMAGE_TOP_RIGHT: return config->window_frame_image_top_right;
	case FRAME_IMAGE_LEFT: return config->window_frame_image_left;
	case FRAME_IMAGE_RIGHT: return config->window_frame_image_right;
	case FRAME_IMAGE_BOTTOM_LEFT: return config->window_frame_image_bottom_left;
	case FRAME_IMAGE_BOTTOM: return config->window_frame_image_bottom;
	case FRAME_IMAGE_BOTTOM_RIGHT: return config->window_frame_image_bottom_right;
	default: return NULL;
	}
}

void frame_arrange(struct sway_container *con, int width, int height) {
	if (!con->view) {
		return;
	}

	int outset = con->frame_outset > 0 ? con->frame_outset : config->window_frame_outset;

	frame_ensure_corner(&con->frame_top_left, con->scene_tree);
	frame_ensure_corner(&con->frame_top_right, con->scene_tree);
	frame_ensure_corner(&con->frame_bottom_left, con->scene_tree);
	frame_ensure_corner(&con->frame_bottom_right, con->scene_tree);

	struct frame_image *tl = frame_load_cached(con, FRAME_IMAGE_TOP_LEFT);
	struct frame_image *tr = frame_load_cached(con, FRAME_IMAGE_TOP_RIGHT);
	struct frame_image *bl = frame_load_cached(con, FRAME_IMAGE_BOTTOM_LEFT);
	struct frame_image *br = frame_load_cached(con, FRAME_IMAGE_BOTTOM_RIGHT);
	struct frame_image *top = frame_load_cached(con, FRAME_IMAGE_TOP);
	struct frame_image *bottom = frame_load_cached(con, FRAME_IMAGE_BOTTOM);
	struct frame_image *left = frame_load_cached(con, FRAME_IMAGE_LEFT);
	struct frame_image *right = frame_load_cached(con, FRAME_IMAGE_RIGHT);

	int tl_w = tl ? tl->width : 0;
	int tl_h = tl ? tl->height : 0;
	int tr_w = tr ? tr->width : 0;
	int tr_h = tr ? tr->height : 0;
	int bl_w = bl ? bl->width : 0;
	int bl_h = bl ? bl->height : 0;
	int br_w = br ? br->width : 0;
	int br_h = br ? br->height : 0;
	int top_h = top ? top->height : 0;
	int bottom_h = bottom ? bottom->height : 0;
	int left_w = left ? left->width : 0;
	int right_w = right ? right->width : 0;

	// Top-left corner
	if (tl) {
		wlr_scene_buffer_set_buffer(con->frame_top_left, tl->buffer);
		wlr_scene_buffer_set_dest_size(con->frame_top_left, tl_w, tl_h);
		wlr_scene_node_set_position(&con->frame_top_left->node,
				-outset, -outset);
		wlr_scene_node_set_enabled(&con->frame_top_left->node, true);
	} else {
		wlr_scene_node_set_enabled(&con->frame_top_left->node, false);
	}

	// Top-right corner
	if (tr) {
		wlr_scene_buffer_set_buffer(con->frame_top_right, tr->buffer);
		wlr_scene_buffer_set_dest_size(con->frame_top_right, tr_w, tr_h);
		wlr_scene_node_set_position(&con->frame_top_right->node,
				width + outset - tr_w, -outset);
		wlr_scene_node_set_enabled(&con->frame_top_right->node, true);
	} else {
		wlr_scene_node_set_enabled(&con->frame_top_right->node, false);
	}

	// Bottom-left corner
	if (bl) {
		wlr_scene_buffer_set_buffer(con->frame_bottom_left, bl->buffer);
		wlr_scene_buffer_set_dest_size(con->frame_bottom_left, bl_w, bl_h);
		wlr_scene_node_set_position(&con->frame_bottom_left->node,
				-outset, height + outset - bl_h);
		wlr_scene_node_set_enabled(&con->frame_bottom_left->node, true);
	} else {
		wlr_scene_node_set_enabled(&con->frame_bottom_left->node, false);
	}

	// Bottom-right corner
	if (br) {
		wlr_scene_buffer_set_buffer(con->frame_bottom_right, br->buffer);
		wlr_scene_buffer_set_dest_size(con->frame_bottom_right, br_w, br_h);
		wlr_scene_node_set_position(&con->frame_bottom_right->node,
				width + outset - br_w, height + outset - br_h);
		wlr_scene_node_set_enabled(&con->frame_bottom_right->node, true);
	} else {
		wlr_scene_node_set_enabled(&con->frame_bottom_right->node, false);
	}

	// Top edge (tiled horizontally)
	int top_span = width + 2 * outset - tl_w - tr_w;
	if (top && top_span > 0 && top->width > 0 && top->height > 0) {
		int tile_count = (top_span + top->width - 1) / top->width;
		frame_ensure_tiles(&con->frame_top_tiles, con->scene_tree, tile_count);
		int x = -outset + tl_w;
		for (int i = 0; i < tile_count; i++) {
			struct wlr_scene_buffer *sb = con->frame_top_tiles->items[i];
			int tile_w = top->width;
			if (i == tile_count - 1) {
				tile_w = top_span - i * top->width;
				if (tile_w <= 0) tile_w = 0;
			}
			wlr_scene_buffer_set_buffer(sb, top->buffer);
			wlr_scene_buffer_set_dest_size(sb, tile_w, top_h);
			wlr_scene_node_set_position(&sb->node, x, -outset);
			wlr_scene_node_set_enabled(&sb->node, tile_w > 0);
			x += top->width;
		}
	} else {
		frame_clear_tiles(con->frame_top_tiles);
		con->frame_top_tiles = NULL;
	}

	// Bottom edge (tiled horizontally)
	int bottom_span = width + 2 * outset - bl_w - br_w;
	if (bottom && bottom_span > 0 && bottom->width > 0 && bottom->height > 0) {
		int tile_count = (bottom_span + bottom->width - 1) / bottom->width;
		frame_ensure_tiles(&con->frame_bottom_tiles, con->scene_tree, tile_count);
		int x = -outset + bl_w;
		for (int i = 0; i < tile_count; i++) {
			struct wlr_scene_buffer *sb = con->frame_bottom_tiles->items[i];
			int tile_w = bottom->width;
			if (i == tile_count - 1) {
				tile_w = bottom_span - i * bottom->width;
				if (tile_w <= 0) tile_w = 0;
			}
			wlr_scene_buffer_set_buffer(sb, bottom->buffer);
			wlr_scene_buffer_set_dest_size(sb, tile_w, bottom_h);
			wlr_scene_node_set_position(&sb->node, x,
					height + outset - bottom_h);
			wlr_scene_node_set_enabled(&sb->node, tile_w > 0);
			x += bottom->width;
		}
	} else {
		frame_clear_tiles(con->frame_bottom_tiles);
		con->frame_bottom_tiles = NULL;
	}

	// Left edge (tiled vertically)
	int left_span = height + 2 * outset - tl_h - bl_h;
	if (left && left_span > 0 && left->width > 0 && left->height > 0) {
		int tile_count = (left_span + left->height - 1) / left->height;
		frame_ensure_tiles(&con->frame_left_tiles, con->scene_tree, tile_count);
		int y = -outset + tl_h;
		for (int i = 0; i < tile_count; i++) {
			struct wlr_scene_buffer *sb = con->frame_left_tiles->items[i];
			int tile_h = left->height;
			if (i == tile_count - 1) {
				tile_h = left_span - i * left->height;
				if (tile_h <= 0) tile_h = 0;
			}
			wlr_scene_buffer_set_buffer(sb, left->buffer);
			wlr_scene_buffer_set_dest_size(sb, left_w, tile_h);
			wlr_scene_node_set_position(&sb->node, -outset, y);
			wlr_scene_node_set_enabled(&sb->node, tile_h > 0);
			y += left->height;
		}
	} else {
		frame_clear_tiles(con->frame_left_tiles);
		con->frame_left_tiles = NULL;
	}

	// Right edge (tiled vertically)
	int right_span = height + 2 * outset - tr_h - br_h;
	if (right && right_span > 0 && right->width > 0 && right->height > 0) {
		int tile_count = (right_span + right->height - 1) / right->height;
		frame_ensure_tiles(&con->frame_right_tiles, con->scene_tree, tile_count);
		int y = -outset + tr_h;
		for (int i = 0; i < tile_count; i++) {
			struct wlr_scene_buffer *sb = con->frame_right_tiles->items[i];
			int tile_h = right->height;
			if (i == tile_count - 1) {
				tile_h = right_span - i * right->height;
				if (tile_h <= 0) tile_h = 0;
			}
			wlr_scene_buffer_set_buffer(sb, right->buffer);
			wlr_scene_buffer_set_dest_size(sb, right_w, tile_h);
			wlr_scene_node_set_position(&sb->node,
					width + outset - right_w, y);
			wlr_scene_node_set_enabled(&sb->node, tile_h > 0);
			y += right->height;
		}
	} else {
		frame_clear_tiles(con->frame_right_tiles);
		con->frame_right_tiles = NULL;
	}

	frame_image_unload(tl);
	frame_image_unload(tr);
	frame_image_unload(bl);
	frame_image_unload(br);
	frame_image_unload(top);
	frame_image_unload(bottom);
	frame_image_unload(left);
	frame_image_unload(right);
}

void frame_destroy_nodes(struct sway_container *con) {
	if (con->frame_top_left) {
		wlr_scene_node_destroy(&con->frame_top_left->node);
		con->frame_top_left = NULL;
	}
	if (con->frame_top_right) {
		wlr_scene_node_destroy(&con->frame_top_right->node);
		con->frame_top_right = NULL;
	}
	if (con->frame_bottom_left) {
		wlr_scene_node_destroy(&con->frame_bottom_left->node);
		con->frame_bottom_left = NULL;
	}
	if (con->frame_bottom_right) {
		wlr_scene_node_destroy(&con->frame_bottom_right->node);
		con->frame_bottom_right = NULL;
	}
	frame_clear_tiles(con->frame_top_tiles);
	con->frame_top_tiles = NULL;
	frame_clear_tiles(con->frame_bottom_tiles);
	con->frame_bottom_tiles = NULL;
	frame_clear_tiles(con->frame_left_tiles);
	con->frame_left_tiles = NULL;
	frame_clear_tiles(con->frame_right_tiles);
	con->frame_right_tiles = NULL;

	for (int i = 0; i < FRAME_IMAGE_COUNT; i++) {
		free(con->frame_image_paths[i]);
		con->frame_image_paths[i] = NULL;
	}
}

void frame_reload_images(struct sway_container *con) {
	if (!con->view) {
		return;
	}
	frame_arrange(con, con->current.content_width + con->current.border_thickness * 2,
			con->current.content_height + con->current.border_thickness);
}