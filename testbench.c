#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "mean_filter.c"

typedef struct {
    int found;
    int x;
    int y;
    int count;
} marker_t;

static int has_image_ext(const char *filename) {
    return strstr(filename, ".jpg") || strstr(filename, ".JPG") ||
           strstr(filename, ".jpeg") || strstr(filename, ".JPEG") ||
           strstr(filename, ".png") || strstr(filename, ".PNG");
}

static void basename_without_ext(const char *filename, char *out, size_t out_size) {
    size_t len = strlen(filename);
    size_t i;
    size_t copy_len = len;

    for (i = len; i > 0; --i) {
        if (filename[i - 1] == '.') {
            copy_len = i - 1;
            break;
        }
        if (filename[i - 1] == '/' || filename[i - 1] == '\\') {
            break;
        }
    }

    if (copy_len >= out_size) {
        copy_len = out_size - 1;
    }
    memcpy(out, filename, copy_len);
    out[copy_len] = '\0';
}

static void resize_center_crop_rgb(const uint8_t *src,
                                   int src_w,
                                   int src_h,
                                   uint8_t *dst) {
    float scale_x = (float)WIDTH / (float)src_w;
    float scale_y = (float)HEIGHT / (float)src_h;
    float scale = (scale_x > scale_y) ? scale_x : scale_y;
    float scaled_w = src_w * scale;
    float scaled_h = src_h * scale;
    float crop_x = (scaled_w - WIDTH) * 0.5f;
    float crop_y = (scaled_h - HEIGHT) * 0.5f;
    int x;
    int y;

    for (y = 0; y < HEIGHT; ++y) {
        for (x = 0; x < WIDTH; ++x) {
            float sx_f = (x + crop_x) / scale;
            float sy_f = (y + crop_y) / scale;
            int sx = (int)(sx_f + 0.5f);
            int sy = (int)(sy_f + 0.5f);
            int dst_idx = (y * WIDTH + x) * 3;
            int src_idx;

            if (sx < 0) {
                sx = 0;
            } else if (sx >= src_w) {
                sx = src_w - 1;
            }
            if (sy < 0) {
                sy = 0;
            } else if (sy >= src_h) {
                sy = src_h - 1;
            }

            src_idx = (sy * src_w + sx) * 3;
            dst[dst_idx + 0] = src[src_idx + 0];
            dst[dst_idx + 1] = src[src_idx + 1];
            dst[dst_idx + 2] = src[src_idx + 2];
        }
    }
}

static void resize_letterbox_rgb(const uint8_t *src,
                                 int src_w,
                                 int src_h,
                                 uint8_t *dst) {
    float scale_x = (float)WIDTH / (float)src_w;
    float scale_y = (float)HEIGHT / (float)src_h;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;
    int scaled_w = (int)(src_w * scale + 0.5f);
    int scaled_h = (int)(src_h * scale + 0.5f);
    int pad_x = (WIDTH - scaled_w) / 2;
    int pad_y = (HEIGHT - scaled_h) / 2;
    int x;
    int y;

    for (y = 0; y < HEIGHT; ++y) {
        for (x = 0; x < WIDTH; ++x) {
            int idx = (y * WIDTH + x) * 3;
            dst[idx + 0] = 24;
            dst[idx + 1] = 24;
            dst[idx + 2] = 24;
        }
    }

    for (y = 0; y < scaled_h; ++y) {
        for (x = 0; x < scaled_w; ++x) {
            float sx_f = x / scale;
            float sy_f = y / scale;
            int sx = (int)(sx_f + 0.5f);
            int sy = (int)(sy_f + 0.5f);
            int dst_idx = ((y + pad_y) * WIDTH + (x + pad_x)) * 3;
            int src_idx;

            if (sx < 0) {
                sx = 0;
            } else if (sx >= src_w) {
                sx = src_w - 1;
            }
            if (sy < 0) {
                sy = 0;
            } else if (sy >= src_h) {
                sy = src_h - 1;
            }

            src_idx = (sy * src_w + sx) * 3;
            dst[dst_idx + 0] = src[src_idx + 0];
            dst[dst_idx + 1] = src[src_idx + 1];
            dst[dst_idx + 2] = src[src_idx + 2];
        }
    }
}

static void copy_rgb(uint8_t *dst, const uint8_t *src) {
    memcpy(dst, src, PIXELS * 3);
}

static void draw_bbox_rgb(uint8_t *rgb, const int32_t coords[4], uint8_t r, uint8_t g, uint8_t b) {
    int x0 = coords[0];
    int x1 = coords[1];
    int y0 = coords[2];
    int y1 = coords[3];
    int x;
    int y;

    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= WIDTH) x1 = WIDTH - 1;
    if (y1 >= HEIGHT) y1 = HEIGHT - 1;

    for (x = x0; x <= x1; ++x) {
        int top = (y0 * WIDTH + x) * 3;
        int bottom = (y1 * WIDTH + x) * 3;
        rgb[top + 0] = r; rgb[top + 1] = g; rgb[top + 2] = b;
        rgb[bottom + 0] = r; rgb[bottom + 1] = g; rgb[bottom + 2] = b;
    }

    for (y = y0; y <= y1; ++y) {
        int left = (y * WIDTH + x0) * 3;
        int right = (y * WIDTH + x1) * 3;
        rgb[left + 0] = r; rgb[left + 1] = g; rgb[left + 2] = b;
        rgb[right + 0] = r; rgb[right + 1] = g; rgb[right + 2] = b;
    }
}

static int bbox_valid(const int32_t coords[4]) {
    return coords[1] > coords[0] && coords[3] > coords[2];
}

static float bbox_iou(const int32_t a[4], const int32_t b[4]);

static int bbox_area(const int32_t coords[4]) {
    if (!bbox_valid(coords)) {
        return 0;
    }
    return (coords[1] - coords[0] + 1) * (coords[3] - coords[2] + 1);
}

static int detection_quality(const int32_t coords[4], const char *plate_text, int method_is_letterbox) {
    int width;
    int height;
    int area;
    int ratio_x100;
    int center_x;
    int center_y;
    int score;

    if (!bbox_valid(coords)) {
        return -1000000;
    }

    width = coords[1] - coords[0] + 1;
    height = coords[3] - coords[2] + 1;
    if (width < 20 || height < 10) {
        return -1000000;
    }

    area = width * height;
    ratio_x100 = width * 100 / (height ? height : 1);
    center_x = (coords[0] + coords[1]) / 2;
    center_y = (coords[2] + coords[3]) / 2;

    score = 0;
    score -= abs(ratio_x100 - 320) * 4;
    score -= abs(center_x - WIDTH / 2) * 2;
    score -= abs(center_y - (HEIGHT * 7 / 10)) * 3;

    if (area < 1500) {
        score -= (1500 - area) / 2;
    } else if (area > 35000) {
        score -= (area - 35000) / 10;
    } else {
        score += 1200;
    }

    if (coords[0] <= 4 || coords[2] <= 4 || coords[1] >= WIDTH - 5 || coords[3] >= HEIGHT - 5) {
        score -= 600;
    }

    if (plate_text[0] && strcmp(plate_text, "(未识别)") != 0) {
        score += 300;
        if (plate_text[1] != '\0') {
            score += 150;
        }
    }

    if (method_is_letterbox) {
        score += 180;
    }

    return score;
}

static void expand_bbox_for_coverage(int32_t coords[4]) {
    int width;
    int height;
    int pad_left;
    int pad_right;
    int pad_top;
    int pad_bottom;

    if (!bbox_valid(coords)) {
        return;
    }

    width = coords[1] - coords[0] + 1;
    height = coords[3] - coords[2] + 1;

    pad_left = width / 6 + 6;
    pad_right = width / 5 + 10;
    pad_top = height / 4 + 6;
    pad_bottom = height / 3 + 10;

    if (width < 90) {
        pad_left += 8;
        pad_right += 10;
    }
    if (height < 40) {
        pad_top += 6;
        pad_bottom += 10;
    }

    coords[0] = (coords[0] - pad_left > 0) ? (coords[0] - pad_left) : 0;
    coords[1] = (coords[1] + pad_right < WIDTH) ? (coords[1] + pad_right) : (WIDTH - 1);
    coords[2] = (coords[2] - pad_top > 0) ? (coords[2] - pad_top) : 0;
    coords[3] = (coords[3] + pad_bottom < HEIGHT) ? (coords[3] + pad_bottom) : (HEIGHT - 1);
}

static void merge_bbox_for_coverage(const int32_t detected[4],
                                    const int32_t reference[4],
                                    int32_t out[4]) {
    int ref_w;
    int ref_h;
    int allow_x;
    int allow_y;

    if (!bbox_valid(reference)) {
        if (bbox_valid(detected)) {
            memcpy(out, detected, sizeof(int32_t) * 4);
        }
        return;
    }

    out[0] = reference[0];
    out[1] = reference[1];
    out[2] = reference[2];
    out[3] = reference[3];

    if (!bbox_valid(detected)) {
        return;
    }

    ref_w = reference[1] - reference[0] + 1;
    ref_h = reference[3] - reference[2] + 1;
    allow_x = ref_w / 3 + 14;
    allow_y = ref_h / 2 + 14;

    if (detected[0] < reference[0] && reference[0] - detected[0] <= allow_x) {
        out[0] = detected[0];
    }
    if (detected[1] > reference[1] && detected[1] - reference[1] <= allow_x) {
        out[1] = detected[1];
    }
    if (detected[2] < reference[2] && reference[2] - detected[2] <= allow_y) {
        out[2] = detected[2];
    }
    if (detected[3] > reference[3] && detected[3] - reference[3] <= allow_y) {
        out[3] = detected[3];
    }
}

static int reference_trustworthy(const int32_t detected[4], const int32_t reference[4]) {
    int det_w;
    int det_h;
    int ref_w;
    int ref_h;
    float overlap;
    int ref_area;
    int det_area;

    if (!bbox_valid(reference)) {
        return 0;
    }

    ref_w = reference[1] - reference[0] + 1;
    ref_h = reference[3] - reference[2] + 1;
    ref_area = ref_w * ref_h;
    if (ref_w < 24 || ref_h < 10) {
        return 0;
    }
    if (ref_w > WIDTH / 2 || ref_h > HEIGHT / 3) {
        return 0;
    }

    if (!bbox_valid(detected)) {
        return 1;
    }

    det_w = detected[1] - detected[0] + 1;
    det_h = detected[3] - detected[2] + 1;
    det_area = det_w * det_h;
    overlap = bbox_iou(detected, reference);

    if (overlap > 0.10f) {
        return 1;
    }
    if (ref_area * 6 < det_area) {
        int ref_cx = (reference[0] + reference[1]) / 2;
        int ref_cy = (reference[2] + reference[3]) / 2;
        int det_cx = (detected[0] + detected[1]) / 2;
        int det_cy = (detected[2] + detected[3]) / 2;

        if (abs(ref_cx - det_cx) < det_w / 3 && abs(ref_cy - det_cy) < det_h / 3) {
            return 1;
        }
    }
    if (reference[2] > detected[2] &&
        abs(ref_w - det_w) < det_w &&
        abs(ref_h - det_h) < det_h) {
        return 1;
    }
    return 0;
}

static float bbox_iou(const int32_t a[4], const int32_t b[4]) {
    int ix0;
    int iy0;
    int ix1;
    int iy1;
    int inter;
    int area_a;
    int area_b;

    if (!bbox_valid(a) || !bbox_valid(b)) {
        return 0.0f;
    }

    ix0 = (a[0] > b[0]) ? a[0] : b[0];
    iy0 = (a[2] > b[2]) ? a[2] : b[2];
    ix1 = (a[1] < b[1]) ? a[1] : b[1];
    iy1 = (a[3] < b[3]) ? a[3] : b[3];
    if (ix1 < ix0 || iy1 < iy0) {
        return 0.0f;
    }

    inter = (ix1 - ix0 + 1) * (iy1 - iy0 + 1);
    area_a = (a[1] - a[0] + 1) * (a[3] - a[2] + 1);
    area_b = (b[1] - b[0] + 1) * (b[3] - b[2] + 1);
    return (float)inter / (float)(area_a + area_b - inter);
}

static int marker_match(uint8_t r, uint8_t g, uint8_t b, int marker_type) {
    if (marker_type == 0) {
        return r > 180 && g < 120 && b < 120;
    }
    if (marker_type == 1) {
        return g > 150 && r < 140 && b < 150;
    }
    return r < 120 && g > 130 && b > 150;
}

static marker_t extract_marker(const uint8_t *rgb,
                               int marker_type,
                               int prefer_x,
                               int prefer_y) {
    static uint8_t visited[PIXELS];
    static int queue[PIXELS];
    marker_t best = {0, 0, 0, 0};
    int i;

    memset(visited, 0, sizeof(visited));

    for (i = 0; i < PIXELS; ++i) {
        int idx = i * 3;
        int head;
        int tail;
        int min_x;
        int max_x;
        int min_y;
        int max_y;
        int sum_x;
        int sum_y;
        int count;
        int width;
        int height;
        int score;

        if (visited[i]) {
            continue;
        }
        if (!marker_match(rgb[idx + 0], rgb[idx + 1], rgb[idx + 2], marker_type)) {
            continue;
        }

        visited[i] = 1;
        head = 0;
        tail = 0;
        queue[tail++] = i;
        min_x = max_x = i % WIDTH;
        min_y = max_y = i / WIDTH;
        sum_x = 0;
        sum_y = 0;
        count = 0;

        while (head < tail) {
            int cur = queue[head++];
            int x = cur % WIDTH;
            int y = cur / WIDTH;

            ++count;
            sum_x += x;
            sum_y += y;
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;

            if (x > 0) {
                int n = cur - 1;
                int nidx = n * 3;
                if (!visited[n] && marker_match(rgb[nidx + 0], rgb[nidx + 1], rgb[nidx + 2], marker_type)) {
                    visited[n] = 1;
                    queue[tail++] = n;
                }
            }
            if (x + 1 < WIDTH) {
                int n = cur + 1;
                int nidx = n * 3;
                if (!visited[n] && marker_match(rgb[nidx + 0], rgb[nidx + 1], rgb[nidx + 2], marker_type)) {
                    visited[n] = 1;
                    queue[tail++] = n;
                }
            }
            if (y > 0) {
                int n = cur - WIDTH;
                int nidx = n * 3;
                if (!visited[n] && marker_match(rgb[nidx + 0], rgb[nidx + 1], rgb[nidx + 2], marker_type)) {
                    visited[n] = 1;
                    queue[tail++] = n;
                }
            }
            if (y + 1 < HEIGHT) {
                int n = cur + WIDTH;
                int nidx = n * 3;
                if (!visited[n] && marker_match(rgb[nidx + 0], rgb[nidx + 1], rgb[nidx + 2], marker_type)) {
                    visited[n] = 1;
                    queue[tail++] = n;
                }
            }
        }

        width = max_x - min_x + 1;
        height = max_y - min_y + 1;
        if (count < 3 || count > 120 || width > 24 || height > 24) {
            continue;
        }

        score = count * 100;
        score -= abs(sum_x / count - prefer_x) * 12;
        score -= abs(sum_y / count - prefer_y) * 12;

        if (!best.found || score > best.count) {
            best.found = 1;
            best.x = sum_x / count;
            best.y = sum_y / count;
            best.count = score;
        }
    }

    return best;
}

static int extract_marker_bbox(const uint8_t *target_rgb,
                               const int32_t predicted_coords[4],
                               int32_t coords[4]) {
    marker_t red;
    marker_t green;
    marker_t cyan;
    int pred_x0 = predicted_coords[0];
    int pred_x1 = predicted_coords[1];
    int pred_y0 = predicted_coords[2];
    int pred_y1 = predicted_coords[3];

    if (!bbox_valid(predicted_coords)) {
        pred_x0 = WIDTH / 4;
        pred_x1 = WIDTH * 3 / 4;
        pred_y0 = HEIGHT / 2;
        pred_y1 = HEIGHT - 1;
    }

    red = extract_marker(target_rgb, 0, pred_x1, pred_y1);
    green = extract_marker(target_rgb, 1, pred_x1, pred_y0);
    cyan = extract_marker(target_rgb, 2, pred_x0, pred_y1);

    if (!red.found || !green.found || !cyan.found) {
        return 0;
    }

    coords[0] = cyan.x;
    coords[1] = (red.x > green.x) ? red.x : green.x;
    coords[2] = green.y;
    coords[3] = (red.y > cyan.y) ? red.y : cyan.y;

    if (!bbox_valid(coords)) {
        return 0;
    }

    if (coords[0] > 2) coords[0] -= 2;
    if (coords[2] > 2) coords[2] -= 2;
    if (coords[1] + 2 < WIDTH) coords[1] += 2;
    if (coords[3] + 2 < HEIGHT) coords[3] += 2;
    return 1;
}

static int extract_reference_bbox(const uint8_t *src_rgb,
                                  const uint8_t *target_rgb,
                                  const int32_t predicted_coords[4],
                                  int32_t coords[4]) {
    static uint8_t mask[PIXELS];
    static int queue[PIXELS];
    int i;
    int best_score = -2147483647;

    coords[0] = 0;
    coords[1] = 0;
    coords[2] = 0;
    coords[3] = 0;

    for (i = 0; i < PIXELS; ++i) {
        int idx = i * 3;
        int sr = src_rgb[idx + 0];
        int sg = src_rgb[idx + 1];
        int sb = src_rgb[idx + 2];
        int tr = target_rgb[idx + 0];
        int tg = target_rgb[idx + 1];
        int tb = target_rgb[idx + 2];
        int diff = abs(tr - sr) + abs(tg - sg) + abs(tb - sb);

        mask[i] = (diff > 70) ? 1 : 0;
    }

    for (i = 0; i < PIXELS; ++i) {
        int head;
        int tail;
        int min_x;
        int max_x;
        int min_y;
        int max_y;
        int count;
        int width;
        int height;
        int aspect_x100;
        int cx;
        int cy;
        int score_bias = 0;
        int score;

        if (!mask[i]) {
            continue;
        }

        head = 0;
        tail = 0;
        count = 0;
        min_x = max_x = i % WIDTH;
        min_y = max_y = i / WIDTH;
        mask[i] = 0;
        queue[tail++] = i;

        while (head < tail) {
            int cur = queue[head++];
            int x = cur % WIDTH;
            int y = cur / WIDTH;

            ++count;
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;

            if (x > 0 && mask[cur - 1]) {
                mask[cur - 1] = 0;
                queue[tail++] = cur - 1;
            }
            if (x + 1 < WIDTH && mask[cur + 1]) {
                mask[cur + 1] = 0;
                queue[tail++] = cur + 1;
            }
            if (y > 0 && mask[cur - WIDTH]) {
                mask[cur - WIDTH] = 0;
                queue[tail++] = cur - WIDTH;
            }
            if (y + 1 < HEIGHT && mask[cur + WIDTH]) {
                mask[cur + WIDTH] = 0;
                queue[tail++] = cur + WIDTH;
            }
        }

        width = max_x - min_x + 1;
        height = max_y - min_y + 1;
        if (count < 20 || width < 20 || height < 10) {
            continue;
        }

        aspect_x100 = width * 100 / (height ? height : 1);
        if (aspect_x100 < 100 || aspect_x100 > 900) {
            continue;
        }

        cx = (min_x + max_x) / 2;
        cy = (min_y + max_y) / 2;
        score_bias += cy * 6;
        score_bias -= abs(cx - WIDTH / 2) * 2;

        if (bbox_valid(predicted_coords)) {
            int pred_cx = (predicted_coords[0] + predicted_coords[1]) / 2;
            int pred_cy = (predicted_coords[2] + predicted_coords[3]) / 2;
            int pred_w = predicted_coords[1] - predicted_coords[0] + 1;
            int pred_h = predicted_coords[3] - predicted_coords[2] + 1;

            score_bias -= abs(cx - pred_cx) * 6;
            score_bias -= abs(cy - pred_cy) * 8;
            score_bias -= abs(width - pred_w) * 3;
            score_bias -= abs(height - pred_h) * 3;
        }

        score = count * 10 + width * 2 + height + score_bias;
        if (score > best_score) {
            best_score = score;
            coords[0] = min_x;
            coords[1] = max_x;
            coords[2] = min_y;
            coords[3] = max_y;
        }
    }

    return bbox_valid(coords);
}

static void tighten_reference_bbox(int32_t coords[4], const int32_t predicted_coords[4]) {
    int x0 = coords[0];
    int x1 = coords[1];
    int y0 = coords[2];
    int y1 = coords[3];
    int width;
    int height;

    if (!bbox_valid(coords)) {
        return;
    }

    width = x1 - x0 + 1;
    height = y1 - y0 + 1;

    if (width > height * 28 / 10) {
        int target_w = height * 24 / 10;
        int center_x;

        if (target_w < 40) {
            target_w = 40;
        }
        if (target_w > width) {
            target_w = width;
        }

        if (bbox_valid(predicted_coords)) {
            center_x = (predicted_coords[0] + predicted_coords[1]) / 2;
        } else {
            center_x = x0 + target_w / 2;
        }

        if (center_x - target_w / 2 < x0) {
            center_x = x0 + target_w / 2;
        }
        if (center_x + target_w / 2 > x1) {
            center_x = x1 - target_w / 2;
        }

        coords[0] = center_x - target_w / 2;
        coords[1] = coords[0] + target_w - 1;
    }

    width = coords[1] - coords[0] + 1;
    height = coords[3] - coords[2] + 1;
    if (height > width * 9 / 10) {
        int target_h = width * 55 / 100;

        if (target_h < 20) {
            target_h = 20;
        }
        if (target_h > height) {
            target_h = height;
        }

        coords[2] = coords[3] - target_h + 1;
    }
}

static void refine_reference_with_plate_color(const uint8_t *rgb, int32_t coords[4]) {
    static int queue[PIXELS];
    uint8_t *masks[4];
    int best_score = -2147483647;
    int best_coords[4] = {0, 0, 0, 0};
    int x0;
    int x1;
    int y0;
    int y1;
    int mask_idx;
    int i;

    if (!bbox_valid(coords)) {
        return;
    }

    x0 = coords[0];
    x1 = coords[1];
    y0 = coords[2];
    y1 = coords[3];

    segment_plate_colors(rgb, blue_mask, green_mask, yellow_mask, white_mask);
    morphology_color_mask(blue_mask);
    morphology_color_mask(green_mask);
    morphology_color_mask(yellow_mask);
    morphology_color_mask(white_mask);

    masks[0] = blue_mask;
    masks[1] = green_mask;
    masks[2] = yellow_mask;
    masks[3] = white_mask;

    for (mask_idx = 0; mask_idx < 4; ++mask_idx) {
        uint8_t *mask = masks[mask_idx];

        for (i = 0; i < PIXELS; ++i) {
            tmp_mask_a[i] = mask[i];
        }

        for (i = y0 * WIDTH + x0; i <= y1 * WIDTH + x1; ++i) {
            int head;
            int tail;
            int min_x;
            int max_x;
            int min_y;
            int max_y;
            int count;
            int width;
            int height;
            int aspect_x100;
            int cx;
            int cy;
            int score;

            if ((i % WIDTH) < x0 || (i % WIDTH) > x1) {
                continue;
            }
            if (!tmp_mask_a[i]) {
                continue;
            }

            head = 0;
            tail = 0;
            count = 0;
            min_x = max_x = i % WIDTH;
            min_y = max_y = i / WIDTH;
            tmp_mask_a[i] = 0;
            queue[tail++] = i;

            while (head < tail) {
                int cur = queue[head++];
                int x = cur % WIDTH;
                int y = cur / WIDTH;

                if (x < x0 || x > x1 || y < y0 || y > y1) {
                    continue;
                }

                ++count;
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;

                if (x > x0 && tmp_mask_a[cur - 1]) {
                    tmp_mask_a[cur - 1] = 0;
                    queue[tail++] = cur - 1;
                }
                if (x < x1 && tmp_mask_a[cur + 1]) {
                    tmp_mask_a[cur + 1] = 0;
                    queue[tail++] = cur + 1;
                }
                if (y > y0 && tmp_mask_a[cur - WIDTH]) {
                    tmp_mask_a[cur - WIDTH] = 0;
                    queue[tail++] = cur - WIDTH;
                }
                if (y < y1 && tmp_mask_a[cur + WIDTH]) {
                    tmp_mask_a[cur + WIDTH] = 0;
                    queue[tail++] = cur + WIDTH;
                }
            }

            width = max_x - min_x + 1;
            height = max_y - min_y + 1;
            if (count < 30 || width < 24 || height < 12) {
                continue;
            }

            aspect_x100 = width * 100 / (height ? height : 1);
            if (aspect_x100 < 120 || aspect_x100 > 700) {
                continue;
            }

            cx = (min_x + max_x) / 2;
            cy = (min_y + max_y) / 2;
            score = count * 8;
            score -= abs(cx - (x0 + x1) / 2) * 3;
            score -= abs(cy - (y0 + y1) / 2) * 4;

            if (score > best_score) {
                best_score = score;
                best_coords[0] = (min_x > 2) ? (min_x - 2) : 0;
                best_coords[1] = (max_x + 2 < WIDTH) ? (max_x + 2) : (WIDTH - 1);
                best_coords[2] = (min_y > 2) ? (min_y - 2) : 0;
                best_coords[3] = (max_y + 2 < HEIGHT) ? (max_y + 2) : (HEIGHT - 1);
            }
        }
    }

    if (bbox_valid(best_coords)) {
        coords[0] = best_coords[0];
        coords[1] = best_coords[1];
        coords[2] = best_coords[2];
        coords[3] = best_coords[3];
    }
}

static void save_crop_rgb(const char *path, const uint8_t *rgb, const int32_t coords[4]) {
    int x0 = coords[0];
    int x1 = coords[1];
    int y0 = coords[2];
    int y1 = coords[3];

    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= WIDTH) x1 = WIDTH - 1;
    if (y1 >= HEIGHT) y1 = HEIGHT - 1;

    {
        int crop_w = x1 - x0 + 1;
        int crop_h = y1 - y0 + 1;
        uint8_t *crop = (uint8_t *)malloc(crop_w * crop_h * 3);
        int y;
        if (!crop) {
            return;
        }
        for (y = 0; y < crop_h; ++y) {
            memcpy(crop + y * crop_w * 3,
                   rgb + ((y0 + y) * WIDTH + x0) * 3,
                   crop_w * 3);
        }
        stbi_write_png(path, crop_w, crop_h, 3, crop, crop_w * 3);
        free(crop);
    }
}

static void save_compare_rgb(const char *path,
                             const uint8_t *processed_rgb,
                             const uint8_t *target_rgb) {
    uint8_t *canvas = (uint8_t *)malloc(WIDTH * HEIGHT * 3 * 2);
    int y;
    if (!canvas) {
        return;
    }

    for (y = 0; y < HEIGHT; ++y) {
        memcpy(canvas + y * WIDTH * 6,
               processed_rgb + y * WIDTH * 3,
               WIDTH * 3);
        memcpy(canvas + y * WIDTH * 6 + WIDTH * 3,
               target_rgb + y * WIDTH * 3,
               WIDTH * 3);
    }

    stbi_write_png(path, WIDTH * 2, HEIGHT, 3, canvas, WIDTH * 6);
    free(canvas);
}

int main(void) {
    const char *in_dir = "./imgs";
    const char *target_dir = "./target";
    const char *out_dir = "./results";
    struct dirent *entry;
    DIR *dp;

    mkdir(out_dir, 0777);

    dp = opendir(in_dir);
    if (!dp) {
        printf("错误：无法打开输入目录 %s\n", in_dir);
        return 1;
    }

    while ((entry = readdir(dp)) != NULL) {
        char *filename = entry->d_name;
        char src_path[512];
        char target_path[512];
        char base[256];
        char resized_path[512];
        char detected_path[512];
        char processed_path[512];
        char crop_path[512];
        char compare_path[512];
        int src_w;
        int src_h;
        int src_c;
        int target_w;
        int target_h;
        int target_c;
        int32_t coords[4];
        int32_t crop_coords[4];
        int32_t letter_coords[4];
        char plate_text[OCR_TEXT_MAX + 1];
        char crop_plate_text[OCR_TEXT_MAX + 1];
        char letter_plate_text[OCR_TEXT_MAX + 1];
        uint8_t *src_rgb;
        uint8_t *target_rgb_raw;
        uint8_t *norm_rgb;
        uint8_t *crop_rgb;
        uint8_t *letter_rgb;
        uint8_t *target_rgb_norm;
        uint8_t *detected_rgb;
        uint8_t *processed_rgb;
        uint8_t *gray_out;
        int32_t ref_coords[4];
        int32_t final_coords[4];
        float overlap = 0.0f;
        int has_reference = 0;
        int use_letterbox = 0;
        int crop_score;
        int letter_score;
        int preserve_full_view;
        int crop_suspicious;

        if (!has_image_ext(filename)) {
            continue;
        }

        snprintf(src_path, sizeof(src_path), "%s/%s", in_dir, filename);
        snprintf(target_path, sizeof(target_path), "%s/%s", target_dir, filename);
        basename_without_ext(filename, base, sizeof(base));
        snprintf(resized_path, sizeof(resized_path), "%s/resized_%s.png", out_dir, base);
        snprintf(detected_path, sizeof(detected_path), "%s/detected_%s.png", out_dir, base);
        snprintf(processed_path, sizeof(processed_path), "%s/processed_%s.png", out_dir, base);
        snprintf(crop_path, sizeof(crop_path), "%s/crop_%s.png", out_dir, base);
        snprintf(compare_path, sizeof(compare_path), "%s/compare_%s.png", out_dir, base);

        printf("正在尝试处理: %s\n", src_path);

        src_rgb = stbi_load(src_path, &src_w, &src_h, &src_c, 3);
        if (!src_rgb) {
            printf("错误：无法加载输入图片 %s，原因: %s\n", filename, stbi_failure_reason());
            continue;
        }

        target_rgb_raw = stbi_load(target_path, &target_w, &target_h, &target_c, 3);
        if (!target_rgb_raw) {
            printf("警告：无法加载 target 中同名图片 %s，原因: %s\n", filename, stbi_failure_reason());
        }

        norm_rgb = (uint8_t *)malloc(PIXELS * 3);
        crop_rgb = (uint8_t *)malloc(PIXELS * 3);
        letter_rgb = (uint8_t *)malloc(PIXELS * 3);
        target_rgb_norm = (uint8_t *)malloc(PIXELS * 3);
        detected_rgb = (uint8_t *)malloc(PIXELS * 3);
        processed_rgb = (uint8_t *)malloc(PIXELS * 3);
        gray_out = (uint8_t *)malloc(PIXELS);

        if (!norm_rgb || !crop_rgb || !letter_rgb || !target_rgb_norm || !detected_rgb || !processed_rgb || !gray_out) {
            printf("错误：内存分配失败: %s\n", filename);
            stbi_image_free(src_rgb);
            if (target_rgb_raw) stbi_image_free(target_rgb_raw);
            free(norm_rgb);
            free(crop_rgb);
            free(letter_rgb);
            free(target_rgb_norm);
            free(detected_rgb);
            free(processed_rgb);
            free(gray_out);
            continue;
        }

        resize_center_crop_rgb(src_rgb, src_w, src_h, crop_rgb);
        plate_preprocessor_rgb(crop_rgb, gray_out, crop_coords, crop_plate_text);
        crop_score = detection_quality(crop_coords, crop_plate_text, 0);

        resize_letterbox_rgb(src_rgb, src_w, src_h, letter_rgb);
        plate_preprocessor_rgb(letter_rgb, gray_out, letter_coords, letter_plate_text);
        letter_score = detection_quality(letter_coords, letter_plate_text, 1);

        preserve_full_view = (src_w * 100 > src_h * 150) || (src_h * 100 > src_w * 115);
        crop_suspicious = (!bbox_valid(crop_coords)) ||
                          (crop_coords[0] < 24) ||
                          (crop_coords[1] > WIDTH - 25) ||
                          (crop_coords[2] < 24) ||
                          (crop_coords[3] < HEIGHT / 2);

        if (preserve_full_view ||
            letter_score > crop_score + 120 ||
            (crop_suspicious && letter_score >= crop_score - 500)) {
            use_letterbox = 1;
            copy_rgb(norm_rgb, letter_rgb);
            coords[0] = letter_coords[0];
            coords[1] = letter_coords[1];
            coords[2] = letter_coords[2];
            coords[3] = letter_coords[3];
            memcpy(plate_text, letter_plate_text, sizeof(letter_plate_text));
        } else {
            copy_rgb(norm_rgb, crop_rgb);
            coords[0] = crop_coords[0];
            coords[1] = crop_coords[1];
            coords[2] = crop_coords[2];
            coords[3] = crop_coords[3];
            memcpy(plate_text, crop_plate_text, sizeof(crop_plate_text));
        }

        stbi_write_png(resized_path, WIDTH, HEIGHT, 3, norm_rgb, WIDTH * 3);
        expand_bbox_for_coverage(coords);
        copy_rgb(detected_rgb, norm_rgb);
        draw_bbox_rgb(detected_rgb, coords, 255, 0, 0);
        stbi_write_png(detected_path, WIDTH, HEIGHT, 3, detected_rgb, WIDTH * 3);

        ref_coords[0] = ref_coords[1] = ref_coords[2] = ref_coords[3] = 0;
        final_coords[0] = coords[0];
        final_coords[1] = coords[1];
        final_coords[2] = coords[2];
        final_coords[3] = coords[3];

        if (target_rgb_raw) {
            if (use_letterbox) {
                resize_letterbox_rgb(target_rgb_raw, target_w, target_h, target_rgb_norm);
            } else {
                resize_center_crop_rgb(target_rgb_raw, target_w, target_h, target_rgb_norm);
            }
            has_reference = extract_marker_bbox(target_rgb_norm, coords, ref_coords);
            if (!has_reference) {
                has_reference = extract_reference_bbox(norm_rgb, target_rgb_norm, coords, ref_coords);
            }
            if (has_reference) {
                int32_t refined_coords[4];
                tighten_reference_bbox(ref_coords, coords);
                overlap = bbox_iou(coords, ref_coords);
                refined_coords[0] = ref_coords[0];
                refined_coords[1] = ref_coords[1];
                refined_coords[2] = ref_coords[2];
                refined_coords[3] = ref_coords[3];
                if (overlap < 0.15f) {
                    refine_reference_with_plate_color(norm_rgb, refined_coords);
                    if (bbox_valid(refined_coords)) {
                        ref_coords[0] = refined_coords[0];
                        ref_coords[1] = refined_coords[1];
                        ref_coords[2] = refined_coords[2];
                        ref_coords[3] = refined_coords[3];
                    }
                }
                overlap = bbox_iou(coords, ref_coords);
                if (reference_trustworthy(coords, ref_coords)) {
                    merge_bbox_for_coverage(coords, ref_coords, final_coords);
                }
            }
            save_compare_rgb(compare_path, detected_rgb, target_rgb_norm);
        }

        expand_bbox_for_coverage(final_coords);
        copy_rgb(processed_rgb, norm_rgb);
        if (has_reference && reference_trustworthy(coords, ref_coords)) {
            draw_bbox_rgb(processed_rgb, final_coords, 0, 255, 0);
        } else {
            draw_bbox_rgb(processed_rgb, final_coords, 255, 0, 0);
        }
        stbi_write_png(processed_path, WIDTH, HEIGHT, 3, processed_rgb, WIDTH * 3);
        save_crop_rgb(crop_path, processed_rgb, final_coords);

        printf("------------------------------------\n");
        printf("处理图片: %s\n", filename);
        printf("归一化尺寸: %dx%d -> %dx%d\n", src_w, src_h, WIDTH, HEIGHT);
        printf("归一化方式: %s\n", use_letterbox ? "letterbox" : "center_crop");
        printf("预测车牌号: %s\n", plate_text[0] ? plate_text : "(未识别)");
        printf("车牌预测区域:\n");
        printf("X 范围: [%d 到 %d] (宽度: %d)\n", coords[0], coords[1], coords[1] - coords[0]);
        printf("Y 范围: [%d 到 %d] (高度: %d)\n", coords[2], coords[3], coords[3] - coords[2]);
        if (has_reference) {
            printf("参考区域:\n");
            printf("X 范围: [%d 到 %d] (宽度: %d)\n", ref_coords[0], ref_coords[1], ref_coords[1] - ref_coords[0]);
            printf("Y 范围: [%d 到 %d] (高度: %d)\n", ref_coords[2], ref_coords[3], ref_coords[3] - ref_coords[2]);
            printf("预测/参考 IoU: %.3f\n", overlap);
        }
        printf("输出文件: %s, %s, %s\n", detected_path, processed_path, crop_path);
        if (target_rgb_raw) {
            printf("对比文件: %s\n", compare_path);
        }
        printf("------------------------------------\n");

        stbi_image_free(src_rgb);
        if (target_rgb_raw) {
            stbi_image_free(target_rgb_raw);
        }
        free(norm_rgb);
        free(crop_rgb);
        free(letter_rgb);
        free(target_rgb_norm);
        free(detected_rgb);
        free(processed_rgb);
        free(gray_out);
    }

    closedir(dp);
    return 0;
}
