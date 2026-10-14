#define _POSIX_C_SOURCE 200809L
#include <cairo.h>
#include <drm_fourcc.h>
#include <math.h>
#include <scenefx/types/wlr_scene.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include "sway/config.h"
#include "sway/frame.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/workspace.h"
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

	int tl_cw = tl ? (outset < tl_w ? outset : tl_w) : 0;
	int tl_ch = tl ? (outset < tl_h ? outset : tl_h) : 0;
	int tr_cw = tr ? (outset < tr_w ? outset : tr_w) : 0;
	int tr_ch = tr ? (outset < tr_h ? outset : tr_h) : 0;
	int bl_cw = bl ? (outset < bl_w ? outset : bl_w) : 0;
	int bl_ch = bl ? (outset < bl_h ? outset : bl_h) : 0;
	int br_cw = br ? (outset < br_w ? outset : br_w) : 0;
	int br_ch = br ? (outset < br_h ? outset : br_h) : 0;

	// Each side either faces the workspace edge (extend by the outer gap)
	// or a sibling window (extend by the full inner gap so neighbour frames
	// overlap and cover the whole gap).
	struct sway_workspace *ws = con->pending.workspace;
	int gap_left = 0, gap_top = 0, gap_right = 0, gap_bottom = 0;
	if (ws) {
		double ws_left = ws->x;
		double ws_top = ws->y;
		double ws_right = ws->x + ws->width;
		double ws_bottom = ws->y + ws->height;

		bool edge_left = fabs(con->pending.x - ws_left) < 0.5;
		bool edge_top = fabs(con->pending.y - ws_top) < 0.5;
		bool edge_right = fabs(con->pending.x + con->pending.width - ws_right) < 0.5;
		bool edge_bottom = fabs(con->pending.y + con->pending.height - ws_bottom) < 0.5;

		gap_left = edge_left ? ws->gaps_outer.left : ws->gaps_inner;
		gap_top = edge_top ? ws->gaps_outer.top : ws->gaps_inner;
		gap_right = edge_right ? ws->gaps_outer.right : ws->gaps_inner;
		gap_bottom = edge_bottom ? ws->gaps_outer.bottom : ws->gaps_inner;
	} else {
		gap_left = config->gaps_outer.left;
		gap_top = config->gaps_outer.top;
		gap_right = config->gaps_outer.right;
		gap_bottom = config->gaps_outer.bottom;
	}

	int frame_left = -outset - gap_left;
	int frame_top = -outset - gap_top;
	int frame_right = width + outset + gap_right;
	int frame_bottom = height + gap_bottom;

	// Top-left corner
	if (tl) {
		struct wlr_fbox src = {
			.x = 0,
			.y = 0,
			.width = tl_cw,
			.height = tl_ch,
		};
		wlr_scene_buffer_set_buffer(con->frame_top_left, tl->buffer);
		wlr_scene_buffer_set_source_box(con->frame_top_left, &src);
		wlr_scene_buffer_set_dest_size(con->frame_top_left, tl_cw, tl_ch);
		wlr_scene_node_set_position(&con->frame_top_left->node,
				frame_left, frame_top);
		wlr_scene_node_set_enabled(&con->frame_top_left->node, true);
	} else {
		wlr_scene_node_set_enabled(&con->frame_top_left->node, false);
	}

	// Top-right corner
	if (tr) {
		struct wlr_fbox src = {
			.x = tr_w - tr_cw,
			.y = 0,
			.width = tr_cw,
			.height = tr_ch,
		};
		wlr_scene_buffer_set_buffer(con->frame_top_right, tr->buffer);
		wlr_scene_buffer_set_source_box(con->frame_top_right, &src);
		wlr_scene_buffer_set_dest_size(con->frame_top_right, tr_cw, tr_ch);
		wlr_scene_node_set_position(&con->frame_top_right->node,
				frame_right - tr_cw, frame_top);
		wlr_scene_node_set_enabled(&con->frame_top_right->node, true);
	} else {
		wlr_scene_node_set_enabled(&con->frame_top_right->node, false);
	}

	// Bottom-left corner: top edge flush with window bottom
	if (bl) {
		struct wlr_fbox src = {
			.x = 0,
			.y = bl_h - bl_ch,
			.width = bl_cw,
			.height = bl_ch,
		};
		wlr_scene_buffer_set_buffer(con->frame_bottom_left, bl->buffer);
		wlr_scene_buffer_set_source_box(con->frame_bottom_left, &src);
		wlr_scene_buffer_set_dest_size(con->frame_bottom_left, bl_cw, bl_ch);
		wlr_scene_node_set_position(&con->frame_bottom_left->node,
				frame_left, frame_bottom);
		wlr_scene_node_set_enabled(&con->frame_bottom_left->node, true);
	} else {
		wlr_scene_node_set_enabled(&con->frame_bottom_left->node, false);
	}

	// Bottom-right corner: top edge flush with window bottom
	if (br) {
		struct wlr_fbox src = {
			.x = br_w - br_cw,
			.y = br_h - br_ch,
			.width = br_cw,
			.height = br_ch,
		};
		wlr_scene_buffer_set_buffer(con->frame_bottom_right, br->buffer);
		wlr_scene_buffer_set_source_box(con->frame_bottom_right, &src);
		wlr_scene_buffer_set_dest_size(con->frame_bottom_right, br_cw, br_ch);
		wlr_scene_node_set_position(&con->frame_bottom_right->node,
				frame_right - br_cw, frame_bottom);
		wlr_scene_node_set_enabled(&con->frame_bottom_right->node, true);
	} else {
		wlr_scene_node_set_enabled(&con->frame_bottom_right->node, false);
	}

	// Top edge (tiled horizontally, between top corners)
	{
		int x_start = frame_left + tl_cw;
		int x_end = frame_right - tr_cw;
		int top_span = x_end - x_start;
		if (top && top_span > 0 && top->width > 0 && top->height > 0) {
			int tile_count = (top_span + top->width - 1) / top->width;
			frame_ensure_tiles(&con->frame_top_tiles, con->frame_tree, tile_count);
			int x = x_start;
			for (int i = 0; i < tile_count; i++) {
				struct wlr_scene_buffer *sb = con->frame_top_tiles->items[i];
				int tile_w = top->width;
				if (i == tile_count - 1) {
					tile_w = top_span - i * top->width;
					if (tile_w <= 0) tile_w = 0;
				}
				wlr_scene_buffer_set_buffer(sb, top->buffer);
				wlr_scene_buffer_set_dest_size(sb, tile_w, top_h);
				wlr_scene_node_set_position(&sb->node, x, frame_top);
				wlr_scene_node_set_enabled(&sb->node, tile_w > 0);
				x += top->width;
			}
		} else {
			frame_clear_tiles(con->frame_top_tiles);
			con->frame_top_tiles = NULL;
		}
	}

	// Bottom edge (tiled horizontally, between bottom corners)
	{
		int x_start = frame_left + bl_cw;
		int x_end = frame_right - br_cw;
		int bottom_span = x_end - x_start;
		if (bottom && bottom_span > 0 && bottom->width > 0 && bottom->height > 0) {
			int tile_count = (bottom_span + bottom->width - 1) / bottom->width;
			frame_ensure_tiles(&con->frame_bottom_tiles, con->frame_tree, tile_count);
			int x = x_start;
			for (int i = 0; i < tile_count; i++) {
				struct wlr_scene_buffer *sb = con->frame_bottom_tiles->items[i];
				int tile_w = bottom->width;
				if (i == tile_count - 1) {
					tile_w = bottom_span - i * bottom->width;
					if (tile_w <= 0) tile_w = 0;
				}
				wlr_scene_buffer_set_buffer(sb, bottom->buffer);
				wlr_scene_buffer_set_dest_size(sb, tile_w, bottom_h);
				wlr_scene_node_set_position(&sb->node, x, frame_bottom);
				wlr_scene_node_set_enabled(&sb->node, tile_w > 0);
				x += bottom->width;
			}
		} else {
			frame_clear_tiles(con->frame_bottom_tiles);
			con->frame_bottom_tiles = NULL;
		}
	}

	// Left edge (tiled vertically, between left corners)
	{
		int y_start = frame_top + tl_ch;
		int y_end = frame_bottom;
		int left_span = y_end - y_start;
		if (left && left_span > 0 && left->width > 0 && left->height > 0) {
			int tile_count = (left_span + left->height - 1) / left->height;
			frame_ensure_tiles(&con->frame_left_tiles, con->frame_tree, tile_count);
			int y = y_start;
			for (int i = 0; i < tile_count; i++) {
				struct wlr_scene_buffer *sb = con->frame_left_tiles->items[i];
				int tile_h = left->height;
				if (i == tile_count - 1) {
					tile_h = left_span - i * left->height;
					if (tile_h <= 0) tile_h = 0;
				}
				wlr_scene_buffer_set_buffer(sb, left->buffer);
				wlr_scene_buffer_set_dest_size(sb, left_w, tile_h);
				wlr_scene_node_set_position(&sb->node, frame_left, y);
				wlr_scene_node_set_enabled(&sb->node, tile_h > 0);
				y += left->height;
			}
		} else {
			frame_clear_tiles(con->frame_left_tiles);
			con->frame_left_tiles = NULL;
		}
	}

	// Right edge (tiled vertically, between right corners)
	{
		int y_start = frame_top + tr_ch;
		int y_end = frame_bottom;
		int right_span = y_end - y_start;
		if (right && right_span > 0 && right->width > 0 && right->height > 0) {
			int tile_count = (right_span + right->height - 1) / right->height;
			frame_ensure_tiles(&con->frame_right_tiles, con->frame_tree, tile_count);
			int y = y_start;
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
						frame_right - right_w, y);
				wlr_scene_node_set_enabled(&sb->node, tile_h > 0);
				y += right->height;
			}
		} else {
			frame_clear_tiles(con->frame_right_tiles);
			con->frame_right_tiles = NULL;
		}
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
	con->frame_top_left = NULL;
	con->frame_top_right = NULL;
	con->frame_bottom_left = NULL;
	con->frame_bottom_right = NULL;

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
	arrange_container(con);
}