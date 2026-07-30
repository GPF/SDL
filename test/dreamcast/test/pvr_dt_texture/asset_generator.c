#include <stdint.h>
#include <stdio.h>

#define WIDTH 64
#define HEIGHT 64

int main(int argc, char **argv)
{
    unsigned char header[18] = { 0 };
    FILE *out;
    int x;
    int y;

    if (argc != 2) {
        fprintf(stderr, "usage: %s output.tga\n", argv[0]);
        return 1;
    }
    out = fopen(argv[1], "wb");
    if (!out) {
        perror(argv[1]);
        return 1;
    }

    header[2] = 2; /* Uncompressed true-color image. */
    header[12] = WIDTH & 0xff;
    header[13] = WIDTH >> 8;
    header[14] = HEIGHT & 0xff;
    header[15] = HEIGHT >> 8;
    header[16] = 32;
    header[17] = 0x28; /* Top-left origin, eight alpha bits. */
    if (fwrite(header, sizeof(header), 1, out) != 1) {
        fclose(out);
        return 1;
    }

    for (y = 0; y < HEIGHT; ++y) {
        for (x = 0; x < WIDTH; ++x) {
            unsigned char pixel[4];
            const int checker = ((x / 8) ^ (y / 8)) & 1;

            pixel[2] = (unsigned char)(x * 4);             /* R */
            pixel[1] = (unsigned char)(y * 4);             /* G */
            pixel[0] = (unsigned char)(checker ? 240 : 32);/* B */
            pixel[3] = (unsigned char)(((x + y) & 16) ? 255 : 96);
            if (x < 4 || y < 4) {
                pixel[2] = 255;
                pixel[1] = 255;
                pixel[0] = 255;
                pixel[3] = 255;
            } else if (x >= WIDTH - 4 || y >= HEIGHT - 4) {
                pixel[2] = 255;
                pixel[1] = 220;
                pixel[0] = 0;
                pixel[3] = 255;
            }
            if (fwrite(pixel, sizeof(pixel), 1, out) != 1) {
                fclose(out);
                return 1;
            }
        }
    }

    return fclose(out) == 0 ? 0 : 1;
}
