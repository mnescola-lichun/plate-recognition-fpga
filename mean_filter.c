#include <stdint.h>
#include <string.h>

#define WIDTH 640
#define HEIGHT 480
#define PIXELS (WIDTH * HEIGHT)
#define RGB_PIXELS (PIXELS * 3)
#define OCR_TEXT_MAX 8

#define SAFE_MARGIN_X 32
#define SAFE_MARGIN_Y 24

static const char k_ocr_charset[] = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ";

static const uint8_t k_ocr_templates[][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
    {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E},
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F},
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    {0x0F, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0F},
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C},
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A},
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}
};

static uint8_t blur_img[PIXELS];
static uint8_t detail_img[PIXELS];
static uint8_t edge_img[PIXELS];
static uint8_t vertical_edge_img[PIXELS];
static uint8_t horizontal_edge_img[PIXELS];
static uint8_t morph_img[PIXELS];
static uint8_t gray_img[PIXELS];
static uint8_t blue_mask[PIXELS];
static uint8_t green_mask[PIXELS];
static uint8_t yellow_mask[PIXELS];
static uint8_t white_mask[PIXELS];
static uint8_t bright_mask[PIXELS];
static uint8_t dark_mask[PIXELS];
static uint8_t black_mask[PIXELS];
static uint8_t detail_mask[PIXELS];
static uint8_t plate_mask[PIXELS];
static uint8_t tmp_mask_a[PIXELS];
static uint8_t tmp_mask_b[PIXELS];
static uint8_t roi_bin[PIXELS];
static int row_proj[HEIGHT];
static int col_proj[WIDTH];
static int component_queue[PIXELS];
static uint32_t integral_img[(HEIGHT + 1) * (WIDTH + 1)];
static uint32_t edge_integral_img[(HEIGHT + 1) * (WIDTH + 1)];
static uint32_t vertical_edge_integral_img[(HEIGHT + 1) * (WIDTH + 1)];
static uint32_t horizontal_edge_integral_img[(HEIGHT + 1) * (WIDTH + 1)];
static uint32_t yellow_integral_img[(HEIGHT + 1) * (WIDTH + 1)];
static uint32_t bright_integral_img[(HEIGHT + 1) * (WIDTH + 1)];
static uint32_t dark_integral_img[(HEIGHT + 1) * (WIDTH + 1)];
static uint32_t black_integral_img[(HEIGHT + 1) * (WIDTH + 1)];
static uint32_t detail_integral_img[(HEIGHT + 1) * (WIDTH + 1)];

static int abs_i(int value) {
    return (value < 0) ? -value : value;
}

static uint8_t clamp_u8(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return (uint8_t)value;
}

static void rgb_to_gray_image(const uint8_t src[RGB_PIXELS], uint8_t dst[PIXELS]) {
    int i;
    for (i = 0; i < PIXELS; ++i) {
        int idx = i * 3;
        int r = src[idx + 0];
        int g = src[idx + 1];
        int b = src[idx + 2];
        dst[i] = (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
    }
}

static void gaussian_blur_3x3(const uint8_t src[PIXELS], uint8_t dst[PIXELS]) {
    int x;
    int y;

    memcpy(dst, src, PIXELS);
    for (y = 1; y < HEIGHT - 1; ++y) {
        for (x = 1; x < WIDTH - 1; ++x) {
            int base = y * WIDTH + x;
            int sum =
                src[base - WIDTH - 1] + (src[base - WIDTH] << 1) + src[base - WIDTH + 1] +
                (src[base - 1] << 1) + (src[base] << 2) + (src[base + 1] << 1) +
                src[base + WIDTH - 1] + (src[base + WIDTH] << 1) + src[base + WIDTH + 1];
            dst[base] = (uint8_t)(sum >> 4);
        }
    }
}

static void local_contrast_enhance(const uint8_t src[PIXELS],
                                   const uint8_t blur[PIXELS],
                                   uint8_t dst[PIXELS]) {
    int i;
    for (i = 0; i < PIXELS; ++i) {
        int diff = (int)src[i] - (int)blur[i];
        dst[i] = clamp_u8(diff * 3 + 128);
    }
}

static void build_candidate_feature_masks(const uint8_t gray[PIXELS],
                                          const uint8_t detail[PIXELS]) {
    int i;

    for (i = 0; i < PIXELS; ++i) {
        int delta = abs_i((int)detail[i] - 128);
        bright_mask[i] = (gray[i] >= 170) ? 255 : 0;
        dark_mask[i] = (gray[i] <= 108) ? 255 : 0;
        black_mask[i] = (gray[i] <= 72) ? 255 : 0;
        detail_mask[i] = (delta >= 26) ? 255 : 0;
    }
}

static void segment_plate_colors(const uint8_t src[RGB_PIXELS],
                                 uint8_t blue[PIXELS],
                                 uint8_t green[PIXELS],
                                 uint8_t yellow[PIXELS],
                                 uint8_t white[PIXELS]) {
    int i;

    for (i = 0; i < PIXELS; ++i) {
        int idx = i * 3;
        int r = src[idx + 0];
        int g = src[idx + 1];
        int b = src[idx + 2];
        int max_v = r;
        int min_v = r;
        int sat;

        if (g > max_v) {
            max_v = g;
        }
        if (b > max_v) {
            max_v = b;
        }
        if (g < min_v) {
            min_v = g;
        }
        if (b < min_v) {
            min_v = b;
        }

        sat = max_v - min_v;
        blue[i] = (b > 70 && b >= g + 18 && b >= r + 25 && sat > 28) ? 255 : 0;
        green[i] = (g > 80 && b > 55 && r + 18 < g && r + 8 < b && sat > 20) ? 255 : 0;
        yellow[i] = (r > 95 && g > 85 && b < 170 && r >= b + 20 && g >= b + 20 && sat > 26) ? 255 : 0;
        white[i] = (max_v > 155 && sat < 45) ? 255 : 0;
    }
}

static void sobel_edges(const uint8_t src[PIXELS],
                        uint8_t dst[PIXELS],
                        uint8_t vertical_dst[PIXELS],
                        uint8_t horizontal_dst[PIXELS]) {
    int x;
    int y;
    uint32_t total = 0;
    uint8_t threshold;

    memset(dst, 0, PIXELS);
    memset(vertical_dst, 0, PIXELS);
    memset(horizontal_dst, 0, PIXELS);
    for (y = 1; y < HEIGHT - 1; ++y) {
        for (x = 1; x < WIDTH - 1; ++x) {
            int base = y * WIDTH + x;
            int gx =
                -src[base - WIDTH - 1] + src[base - WIDTH + 1] -
                (src[base - 1] << 1) + (src[base + 1] << 1) -
                src[base + WIDTH - 1] + src[base + WIDTH + 1];
            int gy =
                src[base - WIDTH - 1] + (src[base - WIDTH] << 1) + src[base - WIDTH + 1] -
                src[base + WIDTH - 1] - (src[base + WIDTH] << 1) - src[base + WIDTH + 1];
            int abs_gx = abs_i(gx);
            int abs_gy = abs_i(gy);
            int mag = abs_gx + (abs_gy >> 2);
            if (mag > 255) {
                mag = 255;
            }
            dst[base] = (uint8_t)mag;
            vertical_dst[base] = (uint8_t)((abs_gx > 255) ? 255 : abs_gx);
            horizontal_dst[base] = (uint8_t)((abs_gy > 255) ? 255 : abs_gy);
            total += (uint32_t)mag;
        }
    }

    threshold = (uint8_t)(total / PIXELS + 12);
    for (y = 0; y < HEIGHT; ++y) {
        for (x = 0; x < WIDTH; ++x) {
            int idx = y * WIDTH + x;
            uint8_t edge_val = dst[idx];
            uint8_t vertical_val = vertical_dst[idx];
            uint8_t horizontal_val = horizontal_dst[idx];

            dst[idx] = (edge_val > threshold) ? 255 : 0;
            vertical_dst[idx] = (vertical_val > threshold && vertical_val > horizontal_val + 8) ? 255 : 0;
            horizontal_dst[idx] = (horizontal_val > threshold && horizontal_val > vertical_val + 8) ? 255 : 0;
        }
    }
}

static void dilate3x3(const uint8_t src[PIXELS], uint8_t dst[PIXELS]) {
    int x;
    int y;

    memset(dst, 0, PIXELS);
    for (y = 1; y < HEIGHT - 1; ++y) {
        for (x = 1; x < WIDTH - 1; ++x) {
            int base = y * WIDTH + x;
            uint8_t value =
                src[base - WIDTH - 1] | src[base - WIDTH] | src[base - WIDTH + 1] |
                src[base - 1] | src[base] | src[base + 1] |
                src[base + WIDTH - 1] | src[base + WIDTH] | src[base + WIDTH + 1];
            dst[base] = value ? 255 : 0;
        }
    }
}

static void erode3x3(const uint8_t src[PIXELS], uint8_t dst[PIXELS]) {
    int x;
    int y;

    memset(dst, 0, PIXELS);
    for (y = 1; y < HEIGHT - 1; ++y) {
        for (x = 1; x < WIDTH - 1; ++x) {
            int base = y * WIDTH + x;
            uint8_t value =
                src[base - WIDTH - 1] & src[base - WIDTH] & src[base - WIDTH + 1] &
                src[base - 1] & src[base] & src[base + 1] &
                src[base + WIDTH - 1] & src[base + WIDTH] & src[base + WIDTH + 1];
            dst[base] = value ? 255 : 0;
        }
    }
}

static void horizontal_dilate(const uint8_t src[PIXELS], uint8_t dst[PIXELS], int radius) {
    int x;
    int y;
    memset(dst, 0, PIXELS);

    for (y = 0; y < HEIGHT; ++y) {
        for (x = 0; x < WIDTH; ++x) {
            int k;
            uint8_t active = 0;
            for (k = -radius; k <= radius; ++k) {
                int xx = x + k;
                if (xx >= 0 && xx < WIDTH && src[y * WIDTH + xx] != 0) {
                    active = 255;
                    break;
                }
            }
            dst[y * WIDTH + x] = active;
        }
    }
}

static void horizontal_erode(const uint8_t src[PIXELS], uint8_t dst[PIXELS], int radius) {
    int x;
    int y;
    memset(dst, 0, PIXELS);

    for (y = 0; y < HEIGHT; ++y) {
        for (x = 0; x < WIDTH; ++x) {
            int k;
            uint8_t active = 255;
            for (k = -radius; k <= radius; ++k) {
                int xx = x + k;
                if (xx < 0 || xx >= WIDTH || src[y * WIDTH + xx] == 0) {
                    active = 0;
                    break;
                }
            }
            dst[y * WIDTH + x] = active;
        }
    }
}

static void morphology_plate_mask(const uint8_t src[PIXELS], uint8_t dst[PIXELS]) {
    static uint8_t tmp0[PIXELS];
    static uint8_t tmp1[PIXELS];

    horizontal_dilate(src, tmp0, 12);
    horizontal_erode(tmp0, tmp1, 6);
    dilate3x3(tmp1, tmp0);
    dilate3x3(tmp0, tmp1);
    erode3x3(tmp1, tmp0);
    erode3x3(tmp0, dst);
}

static void morphology_color_mask(uint8_t mask[PIXELS]) {
    erode3x3(mask, tmp_mask_a);
    dilate3x3(tmp_mask_a, tmp_mask_b);
    horizontal_dilate(tmp_mask_b, tmp_mask_a, 4);
    horizontal_erode(tmp_mask_a, tmp_mask_b, 2);
    dilate3x3(tmp_mask_b, mask);
}

static void combine_masks(const uint8_t a[PIXELS], const uint8_t b[PIXELS], uint8_t dst[PIXELS]) {
    int i;
    for (i = 0; i < PIXELS; ++i) {
        dst[i] = (a[i] != 0 || b[i] != 0) ? 255 : 0;
    }
}

static void intersect_masks(const uint8_t a[PIXELS], const uint8_t b[PIXELS], uint8_t dst[PIXELS]) {
    int i;
    for (i = 0; i < PIXELS; ++i) {
        dst[i] = (a[i] != 0 && b[i] != 0) ? 255 : 0;
    }
}

static void compute_integral_to(const uint8_t src[PIXELS],
                                uint32_t dst[(HEIGHT + 1) * (WIDTH + 1)]) {
    int x;
    int y;

    memset(dst, 0, sizeof(uint32_t) * (HEIGHT + 1) * (WIDTH + 1));
    for (y = 1; y <= HEIGHT; ++y) {
        uint32_t row_sum = 0;
        for (x = 1; x <= WIDTH; ++x) {
            row_sum += (src[(y - 1) * WIDTH + (x - 1)] != 0) ? 1u : 0u;
            dst[y * (WIDTH + 1) + x] = dst[(y - 1) * (WIDTH + 1) + x] + row_sum;
        }
    }
}

static void compute_integral(const uint8_t src[PIXELS]) {
    compute_integral_to(src, integral_img);
}

static uint32_t rect_sum_from(const uint32_t src[(HEIGHT + 1) * (WIDTH + 1)],
                              int x0,
                              int y0,
                              int x1,
                              int y1) {
    int stride = WIDTH + 1;
    return src[y1 * stride + x1]
         - src[y0 * stride + x1]
         - src[y1 * stride + x0]
         + src[y0 * stride + x0];
}

static uint32_t rect_sum(int x0, int y0, int x1, int y1) {
    return rect_sum_from(integral_img, x0, y0, x1, y1);
}

static uint32_t score_plate_geometry(int x0,
                                     int y0,
                                     int x1,
                                     int y1,
                                     int preferred_ratio_x10,
                                     uint32_t color_active,
                                     uint32_t edge_active,
                                     uint32_t vertical_active,
                                     uint32_t horizontal_active,
                                     uint32_t detail_active,
                                     uint32_t text_active) {
    uint32_t box_area = (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1);
    uint32_t fill_ratio_x1000;
    uint32_t edge_density_x1000;
    uint32_t vertical_density_x1000;
    uint32_t horizontal_density_x1000;
    uint32_t detail_density_x1000;
    uint32_t text_density_x1000;
    uint32_t score;
    int width = x1 - x0 + 1;
    int height = y1 - y0 + 1;
    int ratio_x10;
    int aspect_penalty;
    int cx = (x0 + x1) / 2;
    int cy = (y0 + y1) / 2;
    int lower_target = (HEIGHT * 7) / 10;
    int lower_bias = HEIGHT - abs_i(cy - lower_target);
    int center_bias = WIDTH - abs_i(cx - WIDTH / 2);
    int size_penalty = 0;

    if (box_area == 0) {
        return 0;
    }

    fill_ratio_x1000 = color_active * 1000u / box_area;
    edge_density_x1000 = edge_active * 1000u / box_area;
    vertical_density_x1000 = vertical_active * 1000u / box_area;
    horizontal_density_x1000 = horizontal_active * 1000u / box_area;
    detail_density_x1000 = detail_active * 1000u / box_area;
    text_density_x1000 = text_active * 1000u / box_area;
    ratio_x10 = (width * 10) / (height ? height : 1);
    aspect_penalty = abs_i(ratio_x10 - preferred_ratio_x10);

    if (ratio_x10 < 15 || ratio_x10 > 65) {
        return 0;
    }
    if (fill_ratio_x1000 < 70 || fill_ratio_x1000 > 960) {
        return 0;
    }
    if (edge_density_x1000 < 8 || edge_density_x1000 > 650) {
        return 0;
    }
    if (vertical_density_x1000 < 6 || vertical_density_x1000 > 420) {
        return 0;
    }
    if (horizontal_density_x1000 > 540) {
        return 0;
    }
    if (vertical_active * 10u < edge_active * 2u) {
        return 0;
    }
    if (detail_density_x1000 < 8 || detail_density_x1000 > 650) {
        return 0;
    }
    if (text_density_x1000 < 4 || text_density_x1000 > 620) {
        return 0;
    }
    if (cy < HEIGHT / 4) {
        return 0;
    }
    if (cy > (HEIGHT * 3) / 4 && ratio_x10 > 34 && vertical_active * 3u < horizontal_active * 2u) {
        return 0;
    }

    if (width > 240) {
        size_penalty += (width - 240) * 140;
    }
    if (height > 96) {
        size_penalty += (height - 96) * 200;
    }

    score = color_active * 8u + edge_active * 6u + vertical_active * 14u + detail_active * 10u + text_active * 12u;
    score += horizontal_active * 2u;
    score += (uint32_t)lower_bias * 40u + (uint32_t)center_bias * 8u;
    score -= (uint32_t)aspect_penalty * 180u;
    if (score > horizontal_active * 4u) {
        score -= horizontal_active * 4u;
    } else {
        return 0;
    }
    if ((int32_t)score <= size_penalty) {
        return 0;
    }
    score -= (uint32_t)size_penalty;
    return score;
}


static int is_valid_plate_box(const int32_t plate_coords[4]) {
    int w = plate_coords[1] - plate_coords[0] + 1;
    int h = plate_coords[3] - plate_coords[2] + 1;

    if (w < 48 || h < 16) {
        return 0;
    }
    if (w > WIDTH - 8 || h > HEIGHT - 8) {
        return 0;
    }
    return (w > h * 2 && w < h * 7);
}

static uint32_t find_best_color_component(const uint8_t color_mask[PIXELS],
                                          const uint32_t text_integral[(HEIGHT + 1) * (WIDTH + 1)],
                                          int preferred_ratio_x10,
                                          int32_t plate_coords[4]) {
    uint32_t best_score = 0;
    int idx;

    memcpy(tmp_mask_a, color_mask, PIXELS);

    for (idx = 0; idx < PIXELS; ++idx) {
        int head = 0;
        int tail = 0;
        int min_x;
        int max_x;
        int min_y;
        int max_y;
        uint32_t active = 0;
        uint32_t box_area;
        uint32_t edge_active;
        uint32_t vertical_active;
        uint32_t horizontal_active;
        uint32_t detail_active;
        uint32_t text_active;
        uint32_t score;
        int width;
        int height;

        if (tmp_mask_a[idx] == 0) {
            continue;
        }

        tmp_mask_a[idx] = 0;
        component_queue[tail++] = idx;
        min_x = idx % WIDTH;
        max_x = min_x;
        min_y = idx / WIDTH;
        max_y = min_y;

        while (head < tail) {
            int cur = component_queue[head++];
            int x = cur % WIDTH;
            int y = cur / WIDTH;

            ++active;
            if (x < min_x) {
                min_x = x;
            }
            if (x > max_x) {
                max_x = x;
            }
            if (y < min_y) {
                min_y = y;
            }
            if (y > max_y) {
                max_y = y;
            }

            if (x > 0 && tmp_mask_a[cur - 1] != 0) {
                tmp_mask_a[cur - 1] = 0;
                component_queue[tail++] = cur - 1;
            }
            if (x + 1 < WIDTH && tmp_mask_a[cur + 1] != 0) {
                tmp_mask_a[cur + 1] = 0;
                component_queue[tail++] = cur + 1;
            }
            if (y > 0 && tmp_mask_a[cur - WIDTH] != 0) {
                tmp_mask_a[cur - WIDTH] = 0;
                component_queue[tail++] = cur - WIDTH;
            }
            if (y + 1 < HEIGHT && tmp_mask_a[cur + WIDTH] != 0) {
                tmp_mask_a[cur + WIDTH] = 0;
                component_queue[tail++] = cur + WIDTH;
            }
        }

        width = max_x - min_x + 1;
        height = max_y - min_y + 1;
        if (width < 20 || height < 8) {
            continue;
        }

        box_area = (uint32_t)width * (uint32_t)height;
        if (box_area == 0 || active < 50) {
            continue;
        }

        edge_active = rect_sum_from(edge_integral_img, min_x, min_y, max_x + 1, max_y + 1);
        vertical_active = rect_sum_from(vertical_edge_integral_img, min_x, min_y, max_x + 1, max_y + 1);
        horizontal_active = rect_sum_from(horizontal_edge_integral_img, min_x, min_y, max_x + 1, max_y + 1);
        detail_active = rect_sum_from(detail_integral_img, min_x, min_y, max_x + 1, max_y + 1);
        text_active = rect_sum_from(text_integral, min_x, min_y, max_x + 1, max_y + 1);
        score = score_plate_geometry(min_x,
                                     min_y,
                                     max_x,
                                     max_y,
                                     preferred_ratio_x10,
                                     active,
                                     edge_active,
                                     vertical_active,
                                     horizontal_active,
                                     detail_active,
                                     text_active);

        if (score > best_score) {
            best_score = score;
            plate_coords[0] = min_x;
            plate_coords[1] = max_x;
            plate_coords[2] = min_y;
            plate_coords[3] = max_y;
        }
    }

    return best_score;
}

static uint32_t find_best_color_window(const uint8_t color_mask[PIXELS],
                                       const uint32_t text_integral[(HEIGHT + 1) * (WIDTH + 1)],
                                       int preferred_ratio_x10,
                                       int32_t plate_coords[4]) {
    static const int widths[] = {36, 48, 60, 72, 84, 96, 112, 128, 144, 160, 176, 192};
    static const int ratios_x10[] = {24, 28, 32, 36, 40, 44};
    uint32_t best_score = 0;
    int best_x0 = 0;
    int best_y0 = 0;
    int best_x1 = 0;
    int best_y1 = 0;
    int w_idx;

    compute_integral_to(color_mask, integral_img);

    for (w_idx = 0; w_idx < (int)(sizeof(widths) / sizeof(widths[0])); ++w_idx) {
        int win_w = widths[w_idx];
        int r_idx;

        for (r_idx = 0; r_idx < (int)(sizeof(ratios_x10) / sizeof(ratios_x10[0])); ++r_idx) {
            int ratio_x10 = ratios_x10[r_idx];
            int win_h = (win_w * 10 + ratio_x10 / 2) / ratio_x10;
            int y;
            int x;

            if (win_h < 18 || win_h > 96) {
                continue;
            }

            for (y = SAFE_MARGIN_Y / 2; y + win_h < HEIGHT - SAFE_MARGIN_Y / 2; y += 4) {
                for (x = SAFE_MARGIN_X / 2; x + win_w < WIDTH - SAFE_MARGIN_X / 2; x += 4) {
                    uint32_t color_active = rect_sum(x, y, x + win_w, y + win_h);
                    uint32_t edge_active = rect_sum_from(edge_integral_img, x, y, x + win_w, y + win_h);
                    uint32_t vertical_active = rect_sum_from(vertical_edge_integral_img, x, y, x + win_w, y + win_h);
                    uint32_t horizontal_active = rect_sum_from(horizontal_edge_integral_img, x, y, x + win_w, y + win_h);
                    uint32_t detail_active = rect_sum_from(detail_integral_img, x, y, x + win_w, y + win_h);
                    uint32_t text_active = rect_sum_from(text_integral, x, y, x + win_w, y + win_h);
                    uint32_t score;
                    score = score_plate_geometry(x,
                                                 y,
                                                 x + win_w - 1,
                                                 y + win_h - 1,
                                                 preferred_ratio_x10,
                                                 color_active,
                                                 edge_active,
                                                 vertical_active,
                                                 horizontal_active,
                                                 detail_active,
                                                 text_active);

                    if (score > best_score) {
                        best_score = score;
                        best_x0 = x;
                        best_y0 = y;
                        best_x1 = x + win_w - 1;
                        best_y1 = y + win_h - 1;
                    }
                }
            }
        }
    }

    if (best_score != 0) {
        plate_coords[0] = best_x0;
        plate_coords[1] = best_x1;
        plate_coords[2] = best_y0;
        plate_coords[3] = best_y1;
    }

    return best_score;
}

static void find_best_plate_window(int32_t plate_coords[4]) {
    static const int widths[] = {80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240};
    uint32_t best_score = 0;
    int best_x0 = 0;
    int best_y0 = 0;
    int best_x1 = 0;
    int best_y1 = 0;
    int w_idx;

    plate_coords[0] = 0;
    plate_coords[1] = 0;
    plate_coords[2] = 0;
    plate_coords[3] = 0;

    for (w_idx = 0; w_idx < (int)(sizeof(widths) / sizeof(widths[0])); ++w_idx) {
        int win_w = widths[w_idx];
        int ratios[] = {3, 4, 5};
        int r_idx;

        for (r_idx = 0; r_idx < 3; ++r_idx) {
            int win_h = win_w / ratios[r_idx];
            int y;
            int x;

            if (win_h < 24) {
                win_h = 24;
            }
            if (win_h > 96) {
                win_h = 96;
            }

            for (y = SAFE_MARGIN_Y; y + win_h < HEIGHT - SAFE_MARGIN_Y; y += 4) {
                for (x = SAFE_MARGIN_X; x + win_w < WIDTH - SAFE_MARGIN_X; x += 4) {
                    uint32_t active = rect_sum(x, y, x + win_w, y + win_h);
                    uint32_t area = (uint32_t)win_w * (uint32_t)win_h;
                    uint32_t density_x1000 = active * 1000u / (area ? area : 1u);
                    uint32_t center_bias =
                        (uint32_t)(HEIGHT - abs_i((y + win_h / 2) - HEIGHT / 2)) +
                        (uint32_t)(WIDTH - abs_i((x + win_w / 2) - WIDTH / 2)) / 2u;
                    uint32_t score;

                    if (density_x1000 < 35 || density_x1000 > 700) {
                        continue;
                    }

                    score = active * 5u + center_bias;
                    if (score > best_score) {
                        best_score = score;
                        best_x0 = x;
                        best_y0 = y;
                        best_x1 = x + win_w - 1;
                        best_y1 = y + win_h - 1;
                    }
                }
            }
        }
    }

    if (best_score == 0) {
        return;
    }

    plate_coords[0] = best_x0;
    plate_coords[1] = best_x1;
    plate_coords[2] = best_y0;
    plate_coords[3] = best_y1;
}

static void find_projection_plate_window(const uint8_t mask[PIXELS], int32_t plate_coords[4]) {
    int x;
    int y;
    int row_total = 0;
    int row_max = 0;
    int row_threshold;
    int best_y0 = -1;
    int best_y1 = -1;
    int best_row_score = 0;

    for (y = 0; y < HEIGHT; ++y) {
        int sum = 0;
        for (x = SAFE_MARGIN_X; x < WIDTH - SAFE_MARGIN_X; ++x) {
            if (mask[y * WIDTH + x] != 0) {
                ++sum;
            }
        }
        row_proj[y] = sum;
        row_total += sum;
        if (sum > row_max) {
            row_max = sum;
        }
    }

    row_threshold = row_total / HEIGHT;
    row_threshold += (row_max - row_threshold) / 3;
    if (row_threshold < 6) {
        row_threshold = 6;
    }

    for (y = SAFE_MARGIN_Y; y < HEIGHT - SAFE_MARGIN_Y; ) {
        if (row_proj[y] < row_threshold) {
            ++y;
            continue;
        }
        {
            int run_y0 = y;
            int run_y1 = y;
            int score = 0;
            while (run_y1 < HEIGHT - SAFE_MARGIN_Y && row_proj[run_y1] >= row_threshold / 2) {
                score += row_proj[run_y1];
                ++run_y1;
            }
            --run_y1;
            if (run_y1 > run_y0 && (run_y1 - run_y0 + 1) >= 18 && (run_y1 - run_y0 + 1) <= 120) {
                if (score > best_row_score) {
                    best_row_score = score;
                    best_y0 = run_y0;
                    best_y1 = run_y1;
                }
            }
            y = run_y1 + 1;
        }
    }

    if (best_y0 < 0 || best_y1 <= best_y0) {
        return;
    }

    {
        int col_total = 0;
        int col_max = 0;
        int col_threshold;
        int best_x0 = -1;
        int best_x1 = -1;
        int best_col_score = 0;

        for (x = 0; x < WIDTH; ++x) {
            int sum = 0;
            for (y = best_y0; y <= best_y1; ++y) {
                if (mask[y * WIDTH + x] != 0) {
                    ++sum;
                }
            }
            col_proj[x] = sum;
            col_total += sum;
            if (sum > col_max) {
                col_max = sum;
            }
        }

        col_threshold = col_total / WIDTH;
        col_threshold += (col_max - col_threshold) / 3;
        if (col_threshold < 3) {
            col_threshold = 3;
        }

        for (x = SAFE_MARGIN_X; x < WIDTH - SAFE_MARGIN_X; ) {
            if (col_proj[x] < col_threshold) {
                ++x;
                continue;
            }
            {
                int run_x0 = x;
                int run_x1 = x;
                int score = 0;
                while (run_x1 < WIDTH - SAFE_MARGIN_X && col_proj[run_x1] >= col_threshold / 2) {
                    score += col_proj[run_x1];
                    ++run_x1;
                }
                --run_x1;
                if (run_x1 > run_x0 && (run_x1 - run_x0 + 1) >= 60 && (run_x1 - run_x0 + 1) <= 280) {
                    if (score > best_col_score) {
                        best_col_score = score;
                        best_x0 = run_x0;
                        best_x1 = run_x1;
                    }
                }
                x = run_x1 + 1;
            }
        }

        if (best_x0 >= 0 && best_x1 > best_x0) {
            plate_coords[0] = best_x0;
            plate_coords[1] = best_x1;
            plate_coords[2] = best_y0;
            plate_coords[3] = best_y1;
        }
    }
}

static void refine_plate_window(const uint8_t mask[PIXELS], int32_t plate_coords[4]) {
    int x0 = plate_coords[0];
    int x1 = plate_coords[1];
    int y0 = plate_coords[2];
    int y1 = plate_coords[3];
    int row_threshold;
    int col_threshold;
    int left = x1;
    int right = x0;
    int top = y1;
    int bottom = y0;
    int x;
    int y;

    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    row_threshold = (x1 - x0 + 1) / 10;
    if (row_threshold < 4) {
        row_threshold = 4;
    }
    col_threshold = (y1 - y0 + 1) / 5;
    if (col_threshold < 3) {
        col_threshold = 3;
    }

    for (x = x0; x <= x1; ++x) {
        int sum = 0;
        for (y = y0; y <= y1; ++y) {
            if (mask[y * WIDTH + x] != 0) {
                ++sum;
            }
        }
        if (sum >= col_threshold) {
            if (x < left) {
                left = x;
            }
            if (x > right) {
                right = x;
            }
        }
    }

    for (y = y0; y <= y1; ++y) {
        int sum = 0;
        for (x = x0; x <= x1; ++x) {
            if (mask[y * WIDTH + x] != 0) {
                ++sum;
            }
        }
        if (sum >= row_threshold) {
            if (y < top) {
                top = y;
            }
            if (y > bottom) {
                bottom = y;
            }
        }
    }

    if (right > left && bottom > top) {
        int span_x = right - left + 1;
        int span_y = bottom - top + 1;
        int pad_left = span_x / 5 + 6;
        int pad_right = span_x / 4 + 8;
        int pad_top = span_y / 3 + 6;
        int pad_bottom = span_y / 2 + 8;
        plate_coords[0] = (left - pad_left > 0) ? (left - pad_left) : 0;
        plate_coords[1] = (right + pad_right < WIDTH) ? (right + pad_right) : (WIDTH - 1);
        plate_coords[2] = (top - pad_top > 0) ? (top - pad_top) : 0;
        plate_coords[3] = (bottom + pad_bottom < HEIGHT) ? (bottom + pad_bottom) : (HEIGHT - 1);
    }
}

static void draw_box_on_gray(uint8_t img_out[PIXELS], const int32_t plate_coords[4]) {
    if (plate_coords[1] > plate_coords[0] && plate_coords[3] > plate_coords[2]) {
        int x;
        int y;
        for (x = plate_coords[0]; x <= plate_coords[1] && x < WIDTH; ++x) {
            if (plate_coords[2] >= 0 && plate_coords[2] < HEIGHT) {
                img_out[plate_coords[2] * WIDTH + x] = 255;
            }
            if (plate_coords[3] >= 0 && plate_coords[3] < HEIGHT) {
                img_out[plate_coords[3] * WIDTH + x] = 255;
            }
        }
        for (y = plate_coords[2]; y <= plate_coords[3] && y < HEIGHT; ++y) {
            if (plate_coords[0] >= 0 && plate_coords[0] < WIDTH) {
                img_out[y * WIDTH + plate_coords[0]] = 255;
            }
            if (plate_coords[1] >= 0 && plate_coords[1] < WIDTH) {
                img_out[y * WIDTH + plate_coords[1]] = 255;
            }
        }
    }
}

static void locate_plate_gray_only(const uint8_t gray_in[PIXELS], int32_t plate_coords[4]) {
    gaussian_blur_3x3(gray_in, blur_img);
    local_contrast_enhance(gray_in, blur_img, detail_img);
    sobel_edges(detail_img, edge_img, vertical_edge_img, horizontal_edge_img);
    morphology_plate_mask(edge_img, morph_img);
    compute_integral(morph_img);
    plate_coords[0] = 0;
    plate_coords[1] = 0;
    plate_coords[2] = 0;
    plate_coords[3] = 0;
    find_projection_plate_window(morph_img, plate_coords);
    if (plate_coords[1] <= plate_coords[0] || plate_coords[3] <= plate_coords[2]) {
        find_best_plate_window(plate_coords);
    }
    refine_plate_window(morph_img, plate_coords);
}

static void locate_plate_rgb(const uint8_t rgb_in[RGB_PIXELS], int32_t plate_coords[4]) {
    int32_t candidate_coords[4] = {0, 0, 0, 0};
    int32_t gray_coords[4] = {0, 0, 0, 0};
    uint32_t best_color_score = 0;
    uint32_t score;

    rgb_to_gray_image(rgb_in, gray_img);
    gaussian_blur_3x3(gray_img, blur_img);
    local_contrast_enhance(gray_img, blur_img, detail_img);
    build_candidate_feature_masks(gray_img, detail_img);
    sobel_edges(detail_img, edge_img, vertical_edge_img, horizontal_edge_img);
    morphology_plate_mask(edge_img, morph_img);
    compute_integral_to(morph_img, edge_integral_img);
    compute_integral_to(vertical_edge_img, vertical_edge_integral_img);
    compute_integral_to(horizontal_edge_img, horizontal_edge_integral_img);
    compute_integral_to(bright_mask, bright_integral_img);
    compute_integral_to(dark_mask, dark_integral_img);
    compute_integral_to(black_mask, black_integral_img);
    compute_integral_to(detail_mask, detail_integral_img);

    segment_plate_colors(rgb_in, blue_mask, green_mask, yellow_mask, white_mask);
    morphology_color_mask(blue_mask);
    morphology_color_mask(green_mask);
    morphology_color_mask(yellow_mask);
    morphology_color_mask(white_mask);
    compute_integral_to(yellow_mask, yellow_integral_img);

    score = find_best_color_component(blue_mask, bright_integral_img, 34, candidate_coords);
    if (score == 0) {
        score = find_best_color_window(blue_mask, bright_integral_img, 34, candidate_coords);
    }
    if (score > best_color_score) {
        memcpy(plate_coords, candidate_coords, sizeof(candidate_coords));
        best_color_score = score;
        memcpy(plate_mask, blue_mask, PIXELS);
    }

    score = find_best_color_component(green_mask, dark_integral_img, 38, candidate_coords);
    if (score == 0) {
        score = find_best_color_window(green_mask, dark_integral_img, 38, candidate_coords);
    }
    if (score > best_color_score) {
        memcpy(plate_coords, candidate_coords, sizeof(candidate_coords));
        best_color_score = score;
        memcpy(plate_mask, green_mask, PIXELS);
    }

    score = find_best_color_component(yellow_mask, dark_integral_img, 34, candidate_coords);
    if (score == 0) {
        score = find_best_color_window(yellow_mask, dark_integral_img, 34, candidate_coords);
    }
    if (score > best_color_score) {
        memcpy(plate_coords, candidate_coords, sizeof(candidate_coords));
        best_color_score = score;
        memcpy(plate_mask, yellow_mask, PIXELS);
    }

    if (best_color_score == 0) {
        score = find_best_color_component(white_mask, dark_integral_img, 34, candidate_coords);
        if (score == 0) {
            score = find_best_color_window(white_mask, dark_integral_img, 34, candidate_coords);
        }
        if (score > best_color_score) {
            memcpy(plate_coords, candidate_coords, sizeof(candidate_coords));
            best_color_score = score;
            memcpy(plate_mask, white_mask, PIXELS);
        }
    }

    if (best_color_score != 0) {
        intersect_masks(plate_mask, morph_img, tmp_mask_a);
        refine_plate_window(tmp_mask_a, plate_coords);
        combine_masks(plate_mask, morph_img, tmp_mask_a);
        refine_plate_window(tmp_mask_a, plate_coords);
        refine_plate_window(plate_mask, plate_coords);
    }

    if (best_color_score == 0) {
        locate_plate_gray_only(gray_img, gray_coords);
        if (is_valid_plate_box(gray_coords)) {
            memcpy(plate_coords, gray_coords, sizeof(gray_coords));
        }
    }
}

static void normalize_char_5x7(const uint8_t src[PIXELS],
                               int x0,
                               int y0,
                               int x1,
                               int y1,
                               uint8_t out_rows[7]) {
    int oy;

    for (oy = 0; oy < 7; ++oy) {
        int sy0 = y0 + ((y1 - y0 + 1) * oy) / 7;
        int sy1 = y0 + ((y1 - y0 + 1) * (oy + 1)) / 7;
        int ox;
        uint8_t row_bits = 0;

        if (sy1 <= sy0) {
            sy1 = sy0 + 1;
        }

        for (ox = 0; ox < 5; ++ox) {
            int sx0 = x0 + ((x1 - x0 + 1) * ox) / 5;
            int sx1 = x0 + ((x1 - x0 + 1) * (ox + 1)) / 5;
            int py;
            int px;
            int count = 0;
            int hits = 0;

            if (sx1 <= sx0) {
                sx1 = sx0 + 1;
            }

            for (py = sy0; py < sy1; ++py) {
                for (px = sx0; px < sx1; ++px) {
                    ++count;
                    if (src[py * WIDTH + px] != 0) {
                        ++hits;
                    }
                }
            }

            if (hits * 2 >= count) {
                row_bits |= (uint8_t)(1u << (4 - ox));
            }
        }

        out_rows[oy] = row_bits;
    }
}

static char match_char_template(const uint8_t rows[7]) {
    int best_score = 1000;
    int best_index = -1;
    int tpl;

    for (tpl = 0; tpl < (int)(sizeof(k_ocr_charset) - 1); ++tpl) {
        int score = 0;
        int y;
        for (y = 0; y < 7; ++y) {
            uint8_t diff = (uint8_t)(rows[y] ^ k_ocr_templates[tpl][y]);
            int bit;
            for (bit = 0; bit < 5; ++bit) {
                if ((diff >> bit) & 1u) {
                    ++score;
                }
            }
        }
        if (score < best_score) {
            best_score = score;
            best_index = tpl;
        }
    }

    if (best_index < 0 || best_score > 12) {
        return '?';
    }
    return k_ocr_charset[best_index];
}

static void recognize_plate_text(const uint8_t src[PIXELS], const int32_t plate_coords[4], char plate_text[OCR_TEXT_MAX + 1]) {
    int x0 = plate_coords[0];
    int x1 = plate_coords[1];
    int y0 = plate_coords[2];
    int y1 = plate_coords[3];
    int x;
    int y;
    uint32_t sum = 0;
    uint32_t count = 0;
    int text_len = 0;

    memset(roi_bin, 0, sizeof(roi_bin));
    memset(plate_text, 0, OCR_TEXT_MAX + 1);

    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    for (y = y0; y <= y1; ++y) {
        for (x = x0; x <= x1; ++x) {
            sum += src[y * WIDTH + x];
            ++count;
        }
    }

    if (count == 0) {
        return;
    }

    {
        uint8_t threshold = (uint8_t)(sum / count);
        for (y = y0; y <= y1; ++y) {
            for (x = x0; x <= x1; ++x) {
                uint8_t value = src[y * WIDTH + x];
                roi_bin[y * WIDTH + x] = (value > threshold + 12) ? 255 : 0;
            }
        }
    }

    for (x = x0; x <= x1 && text_len < OCR_TEXT_MAX; ++x) {
        int col_sum = 0;
        int start_x;
        int end_x;

        for (y = y0; y <= y1; ++y) {
            if (roi_bin[y * WIDTH + x] != 0) {
                ++col_sum;
            }
        }

        if (col_sum < (y1 - y0 + 1) / 6) {
            continue;
        }

        start_x = x;
        while (x <= x1) {
            int local_sum = 0;
            for (y = y0; y <= y1; ++y) {
                if (roi_bin[y * WIDTH + x] != 0) {
                    ++local_sum;
                }
            }
            if (local_sum < (y1 - y0 + 1) / 10) {
                break;
            }
            ++x;
        }
        end_x = x - 1;

        if (end_x > start_x + 2) {
            int cy0 = y1;
            int cy1 = y0;
            uint8_t rows[7];
            for (y = y0; y <= y1; ++y) {
                for (int px = start_x; px <= end_x; ++px) {
                    if (roi_bin[y * WIDTH + px] != 0) {
                        if (y < cy0) {
                            cy0 = y;
                        }
                        if (y > cy1) {
                            cy1 = y;
                        }
                    }
                }
            }

            if (cy1 > cy0 + 3) {
                normalize_char_5x7(roi_bin, start_x, cy0, end_x, cy1, rows);
                plate_text[text_len++] = match_char_template(rows);
            }
        }
    }

    if (text_len > 0) {
        plate_text[0] = '?';
    }
    plate_text[text_len] = '\0';
}

void plate_preprocessor_hls(uint8_t img_in[PIXELS],
                            uint8_t img_out[PIXELS],
                            int32_t plate_coords[4],
                            char plate_text[OCR_TEXT_MAX + 1]) {
    #pragma HLS function_top

    locate_plate_gray_only(img_in, plate_coords);
    recognize_plate_text(blur_img, plate_coords, plate_text);

    memcpy(img_out, blur_img, PIXELS);
    draw_box_on_gray(img_out, plate_coords);
}

void plate_preprocessor_rgb(const uint8_t rgb_in[RGB_PIXELS],
                            uint8_t img_out[PIXELS],
                            int32_t plate_coords[4],
                            char plate_text[OCR_TEXT_MAX + 1]) {
    locate_plate_rgb(rgb_in, plate_coords);
    recognize_plate_text(blur_img, plate_coords, plate_text);
    memcpy(img_out, blur_img, PIXELS);
    draw_box_on_gray(img_out, plate_coords);
}
