#include <stdint.h>
#include <stdio.h>

/* Deterministic 256x256 diagnostic source for the direct 16-bit .dt gate.
 *
 * The image is an 8x8 grid of 32x32 blocks. Whole-block colors keep the
 * expected value unambiguous under nearest sampling at any half-integer
 * destination scale, so a capture can be compared block by block.
 *
 *   row 0  opaque reference colors: white red green blue black cyan magenta yellow
 *   row 1  per-column channel ramp (c*32, 128, 255-c*32): quantization and
 *          channel-order check
 *   rows 2-5  reference colors with alpha 0, 85, 170, 255: alpha levels
 *   row 6  white with per-column alpha ramp 0..224: multi-level alpha
 *   row 7  reverse channel ramp (255-c*32, c*32, 128): row/edge integrity
 *   corner blocks are fixed to white/red/green/blue for orientation
 *
 * Alpha 85 is below the ARGB1555 one-bit threshold and 170 is above it, so the
 * ARGB1555 panel must show exactly two alpha levels while ARGB4444 shows four.
 */

#define WIDTH 256
#define HEIGHT 256
#define BLOCK 32

static const int base[8][3] = {
    { 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 },
    { 0, 0, 0 }, { 0, 255, 255 }, { 255, 0, 255 }, { 255, 255, 0 }
};

static void block_color(int col, int row, int *r, int *g, int *b, int *a)
{
    *r = base[col][0];
    *g = base[col][1];
    *b = base[col][2];
    *a = 255;

    switch (row) {
    case 1:
        *r = col * 32;
        *g = 128;
        *b = 255 - col * 32;
        break;
    case 2:
    case 3:
    case 4:
    case 5:
        *a = (row - 2) * 85;
        break;
    case 6:
        *a = col * 32;
        break;
    case 7:
        *r = 255 - col * 32;
        *g = col * 32;
        *b = 128;
        break;
    default:
        break;
    }
}

/* Orientation markers: the four corner blocks. */
static void corner_override(int col, int row, int *r, int *g, int *b, int *a)
{
    if (row == 0 && col == 0) {
        *r = 255; *g = 255; *b = 255; *a = 255;
    } else if (row == 0 && col == 7) {
        *r = 255; *g = 0; *b = 0; *a = 255;
    } else if (row == 7 && col == 0) {
        *r = 0; *g = 255; *b = 0; *a = 255;
    } else if (row == 7 && col == 7) {
        *r = 0; *g = 0; *b = 255; *a = 255;
    }
}

int main(int argc, char **argv)
{
    unsigned char header[18] = { 0 };
    FILE *out;

    if (argc != 2) {
        fprintf(stderr, "usage: %s output.tga\n", argv[0]);
        return 1;
    }
    out = fopen(argv[1], "wb");
    if (!out) {
        perror(argv[1]);
        return 1;
    }

    header[2] = 2;              /* uncompressed true-color */
    header[12] = WIDTH & 0xff;
    header[13] = WIDTH >> 8;
    header[14] = HEIGHT & 0xff;
    header[15] = HEIGHT >> 8;
    header[16] = 32;
    header[17] = 0x28;          /* no attribute bits */
    if (fwrite(header, sizeof(header), 1, out) != 1) {
        fclose(out);
        return 1;
    }

    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            unsigned char pixel[4];
            int r, g, b, a;

            block_color(x / BLOCK, y / BLOCK, &r, &g, &b, &a);
            corner_override(x / BLOCK, y / BLOCK, &r, &g, &b, &a);

            pixel[2] = (unsigned char)r;
            pixel[1] = (unsigned char)g;
            pixel[0] = (unsigned char)b;
            pixel[3] = (unsigned char)a;
            if (fwrite(pixel, sizeof(pixel), 1, out) != 1) {
                fclose(out);
                return 1;
            }
        }
    }

    return fclose(out) == 0 ? 0 : 1;
}
