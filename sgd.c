#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#endif

#include <cairo/cairo.h>
#include <png.h>
#include <zlib.h>

#include "sgd.h"

#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#define MAX(a, b)   ((a) > (b) ? (a) : (b))

#define MAX_WIDTH   2048
#define MAX_HEIGHT  2048

#define TILE_WIDTH  128
#define TILE_HEIGHT 128

#define MAX_TILES   ((MAX_WIDTH / TILE_WIDTH) * (MAX_HEIGHT / TILE_HEIGHT))

#define MAX_BASE    (MAX_WIDTH * MAX_HEIGHT)

static uint8_t base[MAX_BASE + 4096];
static uint32_t file_size;

#define base_off        (base + SGD_OFFSET)
#define file_size_off   (file_size - SGD_OFFSET)

static int sgd_width;
static int sgd_height;

static int h_tiles;
static int v_tiles;

static int comp_lvl = Z_DEFAULT_COMPRESSION;

/*
 * Made up palette. Replace this with actual SGD palette
 * to get original colors.
 */
static const png_color sgd_pal[8] = {
    { 0x15, 0x22, 0x25 },
    { 0x55, 0x6a, 0x48 },
    { 0x75, 0x92, 0x64 },
    { 0x90, 0xa9, 0x80 },
    { 0xaa, 0xbd, 0x9f },
    { 0xc5, 0xd2, 0xbd },
    { 0xdf, 0xe7, 0xdb },
    { 0xff, 0xff, 0xff },
};

static png_color png_pal[16];
static uint8_t tiles[MAX_TILES][TILE_WIDTH * TILE_HEIGHT];

static SGDDirectoryType0 *dir;

static const char *cur_fn;

__attribute__((__format__(printf, 1, 2)))
__attribute__((__noreturn__))
static void panic(const char *fmt, ...)
{
    va_list ap;

    if (cur_fn)
        fprintf(stderr, "%s: ", cur_fn);

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);
    exit(1);
}

__attribute__((__format__(printf, 3, 4)))
static int s_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);

    if (ret < 0 || ret >= size)
        panic("Buffer too small");

    return ret;
}

#define PAL_BLACK   0
#define PAL_WHITE   7

static uint8_t colormap[256];

static void remap_colors(const png_color *pal, int ncolors)
{
    for (int i = 0; i < ncolors; i++) {
        int r = pal[i].red;
        int g = pal[i].green;
        int b = pal[i].blue;
        int best = 0;
        int min_dist = INT_MAX;
        for (int j = 0; j < 8; j++) {
            int rd = png_pal[j].red   - r;
            int gd = png_pal[j].green - g;
            int bd = png_pal[j].blue  - b;
            int dist = abs(rd) + abs(gd) + abs(bd);
            if (dist < min_dist) {
                min_dist = dist;
                best = j;
            }
        }
        colormap[i] = best;
    }
}

static void parse_pal(SGDMrciPalette *e)
{
    if (e->type != SGD_BMPPALETTE)
        panic("Bad palette type");
    if (e->bytes_per_pixel != 1 && e->bytes_per_pixel != 3)
        panic("Bad palette bytes per pixel");
    if (e->bit_depth != 8 || e->num_colors - 1 > 255)
        panic("Bad palette bit depth or number of colors");

    png_color pal[256];
    uint8_t *src = e->data;

    for (int i = 0; i < e->num_colors; i++) {
        int r, g, b;

        if (e->bytes_per_pixel == 3) {
            r = src[0];
            g = src[1];
            b = src[2];
        } else {
            r = g = b = src[0];
        }

        pal[i].red   = r;
        pal[i].green = g;
        pal[i].blue  = b;

        src += e->bytes_per_pixel;
    }

    remap_colors(pal, e->num_colors);
}

static void parse_bmp(SGDMrciBitmap *b)
{
    if (b->type != SGD_BMPTILELIST)
        panic("Bad tile list type");

    for (int i = 0; i < h_tiles * v_tiles; i++) {
        if (b->addr[i] > file_size_off)
            panic("Bad tile address");
        SGDMrciTile *t = (SGDMrciTile *)(base_off + b->addr[i]);
        if (t->type != SGD_BMPTILE)
            panic("Bad tile type");
        if (t->encoding != 1)
            panic("Bad tile encoding");
        if (t->size - sizeof(uint32_t) > file_size_off - b->addr[i])
            panic("Bad tile size");

        uLongf outlen = TILE_WIDTH * TILE_HEIGHT;
        int res = uncompress(tiles[i], &outlen, t->data, t->size - sizeof(uint32_t));
        if (res)
            panic("uncompress() failed with %d", res);
    }
}

static void parse_mrci(SGDMrciHeader *m)
{
    if (m->hdr.type != SGD_MRCIHEADER)
        panic("Bad MRCI header type");
    if (m->width - 1 > MAX_WIDTH - 1 || m->height - 1 > MAX_HEIGHT - 1)
        panic("Bad MRCI image size");
    if (m->bytes_per_pixel != 1 || m->bit_depth != 8)
        panic("Bad MRCI bit depth or bytes per pixel");
    if (m->tile_width != TILE_WIDTH || m->tile_height != TILE_HEIGHT)
        panic("Bad MRCI tile size");
    if (m->palette_addr > file_size_off)
        panic("Bad MRCI palette address");
    if (m->bitmap_addr > file_size_off)
        panic("Bad MRCI bitmap address");

    sgd_width  = m->width;
    sgd_height = m->height;
    h_tiles = (m->width  + TILE_WIDTH  - 1) / TILE_WIDTH;
    v_tiles = (m->height + TILE_HEIGHT - 1) / TILE_HEIGHT;

    parse_pal((SGDMrciPalette *)(base_off + m->palette_addr));
    parse_bmp((SGDMrciBitmap  *)(base_off + m->bitmap_addr));
}

static SGDDirectoryType0 *find_directory(void)
{
    SGDDirectoryTable *t = (SGDDirectoryTable *)(base + 0x4c);
    if (t->num_entries > 8)
        panic("Bad number of directory table entries");
    for (int i = 0; i < t->num_entries; i++) {
        if (t->entry[i].type == 0) {
            if (t->entry[i].addr > file_size)
                panic("Bad directory address");
            SGDDirectory *d = (SGDDirectory *)(base + t->entry[i].addr);
            if (d->hdr.type != SGD_BULKDATA)
                panic("Bad directory type");
            if (d->type0.num_entries > (file_size - t->entry[i].addr) / sizeof(uint32_t))
                panic("Bad number of directory entries");
            return &d->type0;
        }
    }
    panic("Directory 0 not found");
}

static SGDEntry *find_entry(int index)
{
    for (int i = 0; i < dir->num_entries; i++) {
        SGDEntry *e = (SGDEntry *)(base_off + dir->addr[i]);
        if (e->hdr.index == index)
            return e;
    }
    panic("Entry %d not found", index);
}

static void validate_set_r(SGDEntry *set)
{
    if (set->set.num_entries == -1)
        panic("Cycle encountered");

    int num_entries = set->set.num_entries;
    set->set.num_entries = -1;
    for (int i = 0; i < num_entries; i++) {
        SGDEntry *e = find_entry(set->set.entries[i]);
        if (e->hdr.type == SGD_SET)
            validate_set_r(e);
    }
    set->set.num_entries = num_entries;
}

static void validate_directory(void)
{
    dir = find_directory();
    for (int i = 0; i < dir->num_entries; i++) {
        if (dir->addr[i] > file_size_off)
            panic("Bad entry address");
        SGDEntry *e = (SGDEntry *)(base_off + dir->addr[i]);
        switch (e->hdr.type) {
        case SGD_POLYLINE2D:
            if (e->polyline.num_points > (file_size_off - dir->addr[i]) / sizeof(SGDPoint))
                panic("Bad number of points");
            break;
        case SGD_LASSO2D:
            if (e->lasso.num_points > (file_size_off - dir->addr[i]) / sizeof(SGDPoint))
                panic("Bad number of points");
            break;
        case SGD_TEXTLINE2D:
            if (!memchr(e->textline.text, 0, file_size_off - dir->addr[i]))
                panic("Text too long");
            break;
        case SGD_SIMPLEAREA:
        case SGD_CONNECTEDAREA:
            if (e->simple_area.num_entries > (file_size_off - dir->addr[i]) / sizeof(uint32_t))
                panic("Bad number of entries");
            break;
        case SGD_SET:
            if (e->set.num_entries > (file_size_off - dir->addr[i]) / sizeof(uint32_t))
                panic("Bad number of entries");
            break;
        }
    }
    for (int i = 0; i < dir->num_entries; i++) {
        SGDEntry *e = (SGDEntry *)(base_off + dir->addr[i]);
        if (e->hdr.type == SGD_SET)
            validate_set_r(e);
    }
}

static void parse_header(void)
{
    SGDFileHeader *h = (SGDFileHeader *)base;
    if (h->magic1 != 0x0a0090 || h->magic2 != 0x55555555)
        panic("Bad SGD magic");
    if (h->ver_major != 0x07db || (h->ver_minor != 0x0407 && h->ver_minor != 0x0406))
        panic("Bad SGD version");
    if (h->flags != 0x01020015)
        panic("Bad SGD flags");

    validate_directory();
    parse_mrci((SGDMrciHeader *)(base_off + 8));
}

static void line_to(cairo_t *cr, SGDPoint p)
{
    cairo_line_to(cr, rintf(p.x), sgd_height - rintf(p.y));
}

static void draw_polyline(cairo_t *cr, SGDEntry *e, bool reverse)
{
    uint32_t start = e->polyline.point1;
    uint32_t end   = e->polyline.point2;

    if (reverse) {
        start = e->polyline.point2;
        end   = e->polyline.point1;
    }

    if (start)
        line_to(cr, find_entry(start)->point.point);

    if (reverse) {
        for (int i = e->polyline.num_points - 1; i >= 0; i--)
            line_to(cr, e->polyline.points[i]);
    } else {
        for (int i = 0; i < e->polyline.num_points; i++)
            line_to(cr, e->polyline.points[i]);
    }

    if (end)
        line_to(cr, find_entry(end)->point.point);
}

#define set_color(cr, a)    cairo_set_source_rgba(cr, 0, 0, 0, a)

static cairo_surface_t *render_labels(void)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_A8, sgd_width, sgd_height);
    cairo_t *cr = cairo_create(surface);

    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 18.0);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

    set_color(cr, 1);
    cairo_paint(cr);
    set_color(cr, 0);

    for (int i = 0; i < dir->num_entries; i++) {
        SGDEntry *e = (SGDEntry *)(base_off + dir->addr[i]);
        if (!e->hdr.unk3)
            continue;
        switch (e->hdr.type) {
        case SGD_POLYLINE2D:
            cairo_new_path(cr);
            draw_polyline(cr, e, false);
            cairo_stroke(cr);
            break;
        case SGD_TEXTLINE2D:
            cairo_move_to(cr, e->textline.pos.x, sgd_height - e->textline.pos.y);
            cairo_show_text(cr, e->textline.text);
            break;
        }
    }

    cairo_destroy(cr);
    cairo_surface_flush(surface);

    return surface;
}

typedef struct {
    int min_x, min_y;
    int max_x, max_y;
} bounds_t;

#define EMPTY_BOUNDS (bounds_t){9999, 9999, -9999, -9999}

static void add_point(bounds_t *b, int x, int y)
{
    b->min_x = MIN(b->min_x, x);
    b->min_y = MIN(b->min_y, y);
    b->max_x = MAX(b->max_x, x);
    b->max_y = MAX(b->max_y, y);
}

static bounds_t union_bounds(const bounds_t *b1, const bounds_t *b2)
{
    return (bounds_t) {
        .min_x = MIN(b1->min_x, b2->min_x),
        .min_y = MIN(b1->min_y, b2->min_y),
        .max_x = MAX(b1->max_x, b2->max_x),
        .max_y = MAX(b1->max_y, b2->max_y)
    };
}

static bool bounds_empty(const bounds_t *b)
{
    return b->min_x > b->max_x || b->min_y > b->max_y;
}

static int bounds_area(const bounds_t *b)
{
    if (bounds_empty(b))
        return 0;
    return (b->max_x - b->min_x + 1) * (b->max_y - b->min_y + 1);
}

static void expand_bounds(bounds_t *b)
{
    if (!bounds_empty(b)) {
        int mx = MIN(MIN(75, b->min_x), sgd_width - b->max_x - 1);
        int my = MIN(MIN(75, b->min_y), sgd_height - b->max_y - 1);
        b->min_x -= mx;
        b->min_y -= my;
        b->max_x += mx;
        b->max_y += my;
    }
}

static void calc_polyline_bounds(bounds_t *b, SGDEntry *e)
{
    if (e->polyline.point1) {
        SGDEntry *p1 = find_entry(e->polyline.point1);
        add_point(b, p1->point.point.x, sgd_height - p1->point.point.y);
    }

    for (int i = 0; i < e->polyline.num_points; i++)
        add_point(b, e->polyline.points[i].x, sgd_height - e->polyline.points[i].y);

    if (e->polyline.point2) {
        SGDEntry *p2 = find_entry(e->polyline.point2);
        add_point(b, p2->point.point.x, sgd_height - p2->point.point.y);
    }
}

static void calc_area_bounds(bounds_t *b, SGDEntry *e)
{
    for (int i = 0; i < e->simple_area.num_entries; i++) {
        SGDEntry *s = find_entry(abs(e->simple_area.entries[i]));
        switch (s->hdr.type) {
        case SGD_POLYLINE2D:
            calc_polyline_bounds(b, s);
            break;
        case SGD_ELLIPTICALARC2D:;
            float x = s->elliptical_arc.points[0].x;
            float y = sgd_height - s->elliptical_arc.points[0].y;
            float r = (s->elliptical_arc.points[1].x - x) / 2;
            x += r;
            add_point(b, x - r, y - r);
            add_point(b, x + r, y + r);
            break;
        }
    }
}

static void calc_entry_bounds(bounds_t *b, SGDEntry *e)
{
    switch (e->hdr.type) {
    case SGD_LASSO2D:
        for (int j = 0; j < e->lasso.num_points; j++)
            add_point(b, e->lasso.points[j].x, sgd_height - e->lasso.points[j].y);
        break;
    case SGD_CONNECTEDAREA:
        for (int j = 0; j < e->simple_area.num_entries; j++) {
            SGDEntry *s = find_entry(e->simple_area.entries[j]);
            if (s->hdr.type == SGD_SIMPLEAREA)
                calc_area_bounds(b, s);
        }
        break;
    case SGD_SIMPLEAREA:
        calc_area_bounds(b, e);
        break;
    }
}

static void fixup_set(SGDEntry *set)
{
    int num_entries = set->set.num_entries;
    for (int i = 0; i < num_entries-1; i++) {
        SGDEntry *e = find_entry(set->set.entries[i]);
        SGDEntry *n = find_entry(set->set.entries[i+1]);
        if (e->hdr.type == SGD_TEXTLINE2D && strchr(e->textline.text, '-') && n->hdr.type == SGD_SIMPLEAREA) {
            memmove(&set->set.entries[i], &set->set.entries[i+2], (num_entries-i-2)*sizeof(uint32_t));
            set->set.entries[num_entries-2] = e->hdr.index;
            set->set.entries[num_entries-1] = n->hdr.index;
            num_entries-=2;
            i--;
        }
    }
}

static int entry_has_shape(SGDEntry *e)
{
    switch (e->hdr.type) {
    case SGD_SET:
        return 1;
    case SGD_LASSO2D:
        return 2;
    case SGD_CONNECTEDAREA:
        return 3;
    case SGD_SIMPLEAREA:
        for (int i = 0; i < e->simple_area.num_entries; i++) {
            SGDEntry *s = find_entry(abs(e->simple_area.entries[i]));
            if (s->hdr.type == SGD_POLYLINE2D)
                return 4;
        }
        return 0;
    default:
        return 0;
    }
}

#define SET_DRAWN   0x80000000

static void calc_set_bounds_r(bounds_t *b, SGDEntry *set)
{
    bounds_t min_b = EMPTY_BOUNDS;
    bounds_t max_b = EMPTY_BOUNDS;
    int min_area = INT_MAX;

    if ((set->set.unk7 & ~SET_DRAWN) == 0x79) goto recurse;

    fixup_set(set);

    int last_shape = 0;
    bool textline = false;
    for (int i = 0; i < set->set.num_entries; i++) {
        bounds_t eb = EMPTY_BOUNDS;
        int shape = 0;
        int start = i;

        for (; i < set->set.num_entries; i++) {
            SGDEntry *e = find_entry(set->set.entries[i]);
            if (e->hdr.type == SGD_TEXTLINE2D) {
                if (textline)
                    break;
                textline = true;
                continue;
            }

            calc_entry_bounds(&eb, e);

            int class = entry_has_shape(e);
            if (class)
                shape += 1 << (8*(class-1));
        }

        if (!shape)
            continue;

        if (!last_shape)
            last_shape = shape;
        else if (last_shape != shape)
            last_shape = -1;

        for (int j = start; j < i; j++) {
            SGDEntry *e = find_entry(set->set.entries[j]);
            if (e->hdr.type == SGD_SET)
                calc_set_bounds_r(&eb, e);
        }

        bounds_t t = union_bounds(b, &eb);
        int area = bounds_area(&t);
        if (area < min_area) {
            min_b = t;
            min_area = area;
        }

        max_b = union_bounds(&max_b, &t);
    }

    if (last_shape == -1) {
        if (!bounds_empty(&max_b)) {
            *b = max_b;
            return;
        }
    } else {
        if (!bounds_empty(&min_b)) {
            *b = min_b;
            return;
        }
    }

recurse:
    for (int i = 0; i < set->set.num_entries; i++) {
        SGDEntry *e = find_entry(set->set.entries[i]);
        if (e->hdr.type == SGD_SET)
            calc_set_bounds_r(b, e);
    }
}

#define COLOR_HOLE      0.0
#define COLOR_SHAPE     0.5
#define COLOR_LABEL     1.0

static void render_area_mask(cairo_t *cr, SGDEntry *e)
{
    for (int i = 0; i < e->simple_area.num_entries; i++) {
        SGDEntry *s = find_entry(abs(e->simple_area.entries[i]));
        switch (s->hdr.type) {
        case SGD_POLYLINE2D:
            draw_polyline(cr, s, e->simple_area.entries[i] < 0);
            set_color(cr, COLOR_SHAPE);
            break;
        case SGD_ELLIPTICALARC2D:;
            float x = s->elliptical_arc.points[0].x;
            float y = sgd_height - s->elliptical_arc.points[0].y;
            float r = (s->elliptical_arc.points[1].x - x) / 2;
            x += r;
            cairo_arc(cr, x, y, r, 0, M_PI * 2);
            set_color(cr, COLOR_LABEL);
            break;
        }
    }
}

static void render_mask_r(cairo_t *cr, SGDEntry *set)
{
    for (int i = 0; i < set->set.num_entries; i++) {
        SGDEntry *e = find_entry(set->set.entries[i]);
        switch (e->hdr.type) {
        case SGD_LASSO2D:
            set_color(cr, COLOR_SHAPE);
            for (int j = 0; j < e->lasso.num_points; j++)
                line_to(cr, e->lasso.points[j]);
            cairo_fill(cr);
            break;
        case SGD_CONNECTEDAREA:
            for (int j = 0; j < e->simple_area.num_entries; j++) {
                SGDEntry *s = find_entry(e->simple_area.entries[j]);
                if (s->hdr.type == SGD_SIMPLEAREA) {
                    cairo_new_sub_path(cr);
                    render_area_mask(cr, s);
                    cairo_close_path(cr);
                }
            }
            cairo_fill(cr);
            break;
        case SGD_SIMPLEAREA:
            render_area_mask(cr, e);
            cairo_fill(cr);
            break;
        }
    }

    for (int i = 0; i < set->set.num_entries; i++) {
        SGDEntry *e = find_entry(set->set.entries[i]);
        if (e->hdr.type == SGD_SET)
            render_mask_r(cr, e);
    }
}

static void my_png_error_fn(png_structp png_ptr, png_const_charp error_msg)
{
    (void)png_ptr;
    panic("libpng error: %s", error_msg);
}

static void write_rows(const char *path, png_bytepp row_pointers, int width, int height, int ncolors)
{
    FILE *fp = fopen(path, "wb");
    if (!fp)
        panic("Couldn't open %s: %s", path, strerror(errno));

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, my_png_error_fn, NULL);
    if (!png_ptr)
        panic("png_create_write_struct() failed");
    png_init_io(png_ptr, fp);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
        panic("png_create_info_struct() failed");
    png_set_IHDR(png_ptr, info_ptr, width, height, 4, PNG_COLOR_TYPE_PALETTE,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_PLTE(png_ptr, info_ptr, png_pal, ncolors);
    png_set_rows(png_ptr, info_ptr, row_pointers);
    if (comp_lvl != Z_DEFAULT_COMPRESSION)
        png_set_compression_level(png_ptr, comp_lvl);
    png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_PACKING, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);

    if (ferror(fp) || fclose(fp))
        panic("Couldn't write %s", path);
}

static void render_tiles(uint8_t *sgd_data, cairo_surface_t *mask)
{
    uint8_t *cr_data = cairo_image_surface_get_data(mask);
    int cr_stride = cairo_format_stride_for_width(CAIRO_FORMAT_A8, sgd_width);
    for (int i = 0; i < sgd_height; i++) {
        int d = i / TILE_HEIGHT;
        int m = i % TILE_HEIGHT;
        uint8_t *dst = &sgd_data[i * sgd_width];
        uint8_t *msk = &cr_data[i * cr_stride];
        for (int j = 0; j < h_tiles; j++) {
            int w = MIN(TILE_WIDTH, sgd_width - j * TILE_WIDTH);
            uint8_t *src = &tiles[d * h_tiles + j][m * w];
            for (int k = 0; k < w; k++, msk++)
                *dst++ = *msk == 255 ? colormap[src[k]] : *msk >> 5;
        }
    }
}

static void apply_mask(uint8_t *sgd_data, cairo_surface_t *mask)
{
    uint8_t *cr_data = cairo_image_surface_get_data(mask);
    int cr_stride = cairo_format_stride_for_width(CAIRO_FORMAT_A8, sgd_width);
    for (int i = 0; i < sgd_height; i++) {
        uint8_t *dst = &sgd_data[i * sgd_width];
        uint8_t *msk = &cr_data[i * cr_stride];
        for (int j = 0; j < sgd_width; j++, dst++, msk++)
            if (*msk && (*dst != PAL_WHITE || *msk == 255))
                *dst |= 8;
    }
}

static void write_full(uint8_t *sgd_data, const char *path, int ncolors)
{
    png_bytep row_pointers[MAX_HEIGHT];

    for (int i = 0; i < sgd_height; i++)
        row_pointers[i] = &sgd_data[i * sgd_width];

    write_rows(path, row_pointers, sgd_width, sgd_height, ncolors);
}

static void write_crop(uint8_t *sgd_data, const char *path, const bounds_t *b)
{
    if (!bounds_empty(b)) {
        png_bytep row_pointers[MAX_HEIGHT];
        int w = b->max_x - b->min_x + 1;
        int h = b->max_y - b->min_y + 1;

        for (int i = 0; i < h; i++)
            row_pointers[i] = &sgd_data[(b->min_y + i) * sgd_width + b->min_x];

        write_rows(path, row_pointers, w, h, 16);
    }
}

static int do_full;
static int do_crop;

static char *fixsep(char *s)
{
#ifdef _WIN32
    char *p = s;

    while (*p) {
        if (*p == '\\')
            *p = '/';
        p++;
    }
#endif
    return s;
}

static void mkpath(char *s)
{
    char *p = s;

    while (*p == '/')
        p++;

    while ((p = strchr(p, '/'))) {
        *p = 0;
#ifdef _WIN32
        _mkdir(s);
#else
        mkdir(s, 0755);
#endif
        *p++ = '/';
    }
}

static bool set_has_entry(SGDEntry *set, int index)
{
    for (int i = 0; i < set->set.num_entries; i++)
        if (set->set.entries[i] == index)
            return true;
    return false;
}

static bool set_is_subset(SGDEntry *set)
{
    for (int i = 0; i < dir->num_entries; i++) {
        SGDEntry *e = (SGDEntry *)(base_off + dir->addr[i]);
        if (e == set || e->hdr.type != SGD_SET || e->set.num_entries <= set->set.num_entries)
            continue;
        int j;
        for (j = 0; j < set->set.num_entries; j++) {
            if (!set_has_entry(e, set->set.entries[j]))
                break;
        }
        if (j == set->set.num_entries)
            return true;
    }
    return false;
}

static void clearstr(char *out, size_t size, const char *in)
{
    while (*in) {
        int c = *in++;
        switch (c) {
        case 0 ... 32:
        case '(':
        case ')':
            continue;
        case 'A' ... 'Z':
        case '0' ... '9':
            break;
        case 'a' ... 'z':
            c -= 'a' - 'A';
            break;
        default:
            c = '_';
            break;
        }
        if (--size == 0)
            break;
        *out++ = c;
    }
    *out = 0;
}

static char *get_set_name_buf(char *buf, size_t size, SGDEntry *set)
{
    for (int i = 0; i < set->set.num_entries; i++) {
        SGDEntry *e = find_entry(set->set.entries[i]);
        if (e->hdr.type == SGD_TEXTLINE2D && !strchr(e->textline.text, '-')) {
            clearstr(buf, size, e->textline.text);
            if (*buf)
                return buf;
        }
    }
    return NULL;
}

#define get_set_name(set)   get_set_name_buf((char [16]){}, 16, set)

static void finalize_bounds(bounds_t *b, SGDEntry *set)
{
    if (bounds_empty(b)) {
        for (int i = 0; i < (int)set->set.num_entries-1; i++) {
            SGDEntry *e = find_entry(set->set.entries[i]);
            SGDEntry *n = find_entry(set->set.entries[i+1]);
            if (e->hdr.type == SGD_TEXTLINE2D && !strchr(e->textline.text, '-') && n->hdr.type == SGD_SIMPLEAREA) {
                calc_entry_bounds(b, n);
                break;
            }
        }
    }
    expand_bounds(b);
}

static void process_sets(const uint8_t *backgr, const char *path)
{
    char buf[1024];
    size_t size = sgd_width * sgd_height;
    uint8_t *data = malloc(size);

    char *p = strrchr(path, '/');
    if (!p)
        panic("Bad path");
    *p = 0;
    char *name = p + 1;

    if (do_full) {
        s_snprintf(buf, sizeof(buf), "%s/full/", path);
        mkpath(buf);
    }

    if (do_crop) {
        s_snprintf(buf, sizeof(buf), "%s/crop/", path);
        mkpath(buf);
    }

    cairo_surface_t *mask = cairo_image_surface_create(CAIRO_FORMAT_A8, sgd_width, sgd_height);

    cairo_t *mask_cr = cairo_create(mask);
    cairo_set_antialias(mask_cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_operator(mask_cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_fill_rule(mask_cr, CAIRO_FILL_RULE_EVEN_ODD);

    for (int i = 0; i < dir->num_entries; i++) {
        SGDEntry *e = (SGDEntry *)(base_off + dir->addr[i]);
        if (e->hdr.type != SGD_SET || e->set.unk7 & SET_DRAWN || set_is_subset(e))
            continue;

        char *text = get_set_name(e);
        if (!text)
            continue;

        bounds_t b = EMPTY_BOUNDS;

        set_color(mask_cr, COLOR_HOLE);
        cairo_paint(mask_cr);

        render_mask_r(mask_cr, e);

        if (do_crop)
            calc_set_bounds_r(&b, e);

        e->set.unk7 |= SET_DRAWN;

        for (int j = i + 1; j < dir->num_entries; j++) {
            SGDEntry *e2 = (SGDEntry *)(base_off + dir->addr[j]);
            if (e2->hdr.type != SGD_SET || e2->set.unk7 & SET_DRAWN || set_is_subset(e2))
                continue;

            char *text2 = get_set_name(e2);
            if (!text2 || strcmp(text2, text))
                continue;

            render_mask_r(mask_cr, e2);

            if (do_crop)
                calc_set_bounds_r(&b, e2);

            e2->set.unk7 |= SET_DRAWN;
        }

        cairo_surface_flush(mask);

        memcpy(data, backgr, size);
        apply_mask(data, mask);

        if (do_full) {
            s_snprintf(buf, sizeof(buf), "%s/full/%s_%s.png", path, name, text);
            write_full(data, buf, 16);
        }

        if (do_crop) {
            finalize_bounds(&b, e);
            s_snprintf(buf, sizeof(buf), "%s/crop/%s_%s.png", path, name, text);
            write_crop(data, buf, &b);
        }
    }

    cairo_destroy(mask_cr);
    cairo_surface_destroy(mask);

    free(data);
}

static void write_png(const char *path)
{
    size_t size = sgd_width * sgd_height;
    uint8_t *backgr = malloc(size);

    cairo_surface_t *mask = render_labels();
    render_tiles(backgr, mask);
    cairo_surface_destroy(mask);

    char *p = strrchr(path, '/');
    if (p && (p = strrchr(p + 1, '.')))
        *p = 0;

    char buf[1024];
    s_snprintf(buf, sizeof(buf), "%s.png", path);
    mkpath(buf);
    write_full(backgr, buf, 8);

    if (do_full || do_crop)
        process_sets(backgr, path);

    free(backgr);
    cur_fn = NULL;
}

static void uncompress_zgd(FILE *fp)
{
    z_stream z = {
        .next_out  = base,
        .avail_out = MAX_BASE
    };

    if (inflateInit2(&z, 32 + 15))
        panic("inflateInit() failed");

    while (1) {
        uint8_t buf[0x10000];
        z.next_in  = buf;
        z.avail_in = fread(buf, 1, sizeof(buf), fp);
        if (!z.avail_in)
            panic("Partial file");
        int res = inflate(&z, Z_NO_FLUSH);
        if (res == Z_STREAM_END)
            break;
        if (res != Z_OK)
            panic("inflate() failed with %d", res);
    }

    file_size = z.total_out;
    inflateEnd(&z);
}

static void load_sgd(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        panic("Couldn't open %s: %s", path, strerror(errno));

    cur_fn = path;

    uint32_t hdr;
    if (fread(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr) || fseek(fp, 0, SEEK_SET))
        panic("Couldn't read header");
    if ((hdr & 0xe0ffffff) == 0x00088b1f) {
        uncompress_zgd(fp);
    } else {
        file_size = fread(base, 1, MAX_BASE + 1, fp);
        if (file_size == MAX_BASE + 1)
            panic("SGD file too big");
    }

    fclose(fp);

    if (file_size < SGD_OFFSET)
        panic("SGD file too small");

    parse_header();
}

static char *dest_dir = ".";

static void process_files(int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        char *s = fixsep(argv[i]);
        load_sgd(s);

        char *p = strrchr(s, '/');
        if (p)
            s = p + 1;

        char buf[1024];
        s_snprintf(buf, sizeof(buf), "%s/%s", dest_dir, s);

        if (strlen(s) >= 3)
            for (p = buf; (p = strstr(p, "###")); p += 3)
                memcpy(p, s, 3);

        write_png(buf);
    }
}

static bool is_white(const char *s)
{
    while (*s) {
        if (*s > ' ')
            return false;
        s++;
    }
    return true;
}

static void parse_pal_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        panic("Couldn't open %s: %s", path, strerror(errno));

    int i = 0;
    int j = 0;
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp)) {
        j++;
        if (is_white(buf))
            continue;
        int r, g, b;
        if (sscanf(buf, "%x %x %x", &r, &g, &b) != 3)
            panic("Error at line %d in palette file", j);
        if (i == 16)
            panic("Too many colors in palette file");
        png_pal[i].red = r;
        png_pal[i].green = g;
        png_pal[i].blue = b;
        i++;
    }

    if (i == 8) {
        for (i = 0; i < 8; i++) {
            png_pal[i + 8] = png_pal[i];
            png_pal[i + 8].blue = 0;
        }
    } else if (i != 16) {
        panic("Palette file must contain 8 or 16 colors");
    }

    fclose(fp);
}

static void set_default_pal(void)
{
    for (int i = 0; i < 8; i++) {
        png_pal[i] = png_pal[i + 8] = sgd_pal[i];
        png_pal[i + 8].blue = 0;
    }
}

static void print_help(char **argv)
{
    fprintf(stderr, "Usage: %s [options] <SGD-file> [...]\n", argv[0]);
    fprintf(stderr, "Supported options:\n");
    fprintf(stderr, "-c         also output cropped pictures of each selection set\n");
    fprintf(stderr, "-f         also output full pictures of each selection set\n");
    fprintf(stderr, "-p <file>  load alternative 8 or 16 color palette from file\n");
    fprintf(stderr, "-z <0-9>   set PNG compression level\n");
    fprintf(stderr, "-o <path>  set destination directory\n");
    fprintf(stderr, "-h         show this help message\n");
    exit(0);
}

int main(int argc, char **argv)
{
    char *pal_file = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "cfp:z:o:h")) != -1) {
        switch (opt) {
        case 'c':
            do_crop = 1;
            break;
        case 'f':
            do_full = 1;
            break;
        case 'p':
            pal_file = optarg;
            break;
        case 'z':
            comp_lvl = atoi(optarg);
            break;
        case 'o':
            dest_dir = fixsep(optarg);
            break;
        default:
            print_help(argv);
            break;
        }
    }

    if (optind >= argc)
        print_help(argv);

    if (comp_lvl < Z_DEFAULT_COMPRESSION || comp_lvl > Z_BEST_COMPRESSION)
        panic("Bad PNG compression level");

    if (pal_file)
        parse_pal_file(pal_file);
    else
        set_default_pal();

    process_files(argc - optind, argv + optind);

    return 0;
}
