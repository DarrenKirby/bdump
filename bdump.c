/*
 * '/bdump.c'
 * This file is part of bdump - https://github.com/DarrenKirby/bdump
 * Copyright © 2026 Darren Kirby <darren@dragonbyte.ca>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <locale.h>
#include <sys/ioctl.h>

#define APPNAME "bdump"
#define APPVERSION "0.10"

/* Constants for box-drawing, and others. */
#define WELL_WIDTH 12
#define VERT_BAR 0x2502
#define HORT_BAR 0x2500
#define DOWN_T   0x252C
#define UP_T     0x2534
#define CROSS    0x253C
#define MID_DOT  0x00B7

typedef enum int8_t {
    F_HEX,
    F_OCT,
    F_DEC,
    F_BIN
} format_t;

/* Default format: hex */
format_t format = F_HEX;
/* Default line_width: 16 */
uint8_t line_width = 16;


void show_help(void)
{
    printf("Usage: %s [OPTION] FILE\n\n\
Options:\n\
  Output format options:\n\
    -x, --hex\t\t hexidecimal format\n\
    -o, --oct\t\t octal format\n\
    -d, --dec\t\t decimal format\n\
    -b, --bin\t\t binary format\n\
  General options:\n\
    -l, --line-width=n\t print n bytes per line\n\
    -s, --start-offset=n start output at offset n\n\
    -n, --read-bytes=n\t read only n bytes and exit\n\
    -h, --help\t\t display this help\n\
    -V, --version\t display version information\n\n\
Report bugs to <darren@dragonbyte.ca>\n", APPNAME);
}


int32_t get_term_width(void)
{
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0) {
        perror("failed to get terminal width");
        exit(EXIT_FAILURE);
    }
    return w.ws_col;
}

/* Calculate the width of the binary section based on output
 * format and line_width. */
int32_t get_bin_width(void)
{
    switch (format) {
        case F_DEC:
        case F_OCT:
            /* OCT and DEC: 3 chars + 1 space for each + 2 spaces on each end. */
            return line_width * 4 + 2;
        case F_BIN:
            /* BIN: 8 chars + 1 space for each + 2 spaces on each end. */
            return line_width * 9 + 2;
        default:
            /* HEX: 2 chars + 1 space for each + 2 spaces on each end. */
            return line_width * 3 + 2;
    }
}


void byte_to_binary_string(const uint8_t byte, char *str)
{
    /* 0x80 is 10000000 in binary (the Most Significant Bit). */
    for (int i = 0; i < 8; i++) {
        str[i] = (byte & (0x80 >> i)) ? '1' : '0';
    }
    str[8] = '\0';
}


/* Read <line_width> bytes from the file into the buffer. */
size_t read_input(FILE* input, uint8_t *buffer)
{
    const size_t bytes_read = fread(buffer, 1, line_width, input);
    return bytes_read;
}


/* Write the offset-well section of output. */
int write_well(const int32_t offset, const size_t bytes_read)
{
    switch (format) {
        case F_OCT: {
            if (bytes_read == 0) {
                printf(" 0o%08o %lc", offset, VERT_BAR);
                return 1;
            }
            printf(" 0o%08o %lc ", offset, VERT_BAR);
            return 0;
        }
        case F_DEC: {
            if (bytes_read == 0) {
                printf(" 0d%08d %lc", offset, VERT_BAR);
                return 1;
            }
            printf(" 0d%08d %lc ", offset, VERT_BAR);
            return 0;
        }
        default: {
            if (bytes_read == 0) {
                printf(" 0x%08X %lc", offset, VERT_BAR);
                return 1;
            }
            printf(" 0x%08X %lc ", offset, VERT_BAR);
            return 0;
        }
    }
}


/* Write the binary dump section of output. */
void write_binary_dump(const uint8_t *buffer, const size_t bytes_read)
{
    switch (format) {
        case F_OCT: {
            for (size_t i = 0; i < bytes_read; i++) {
                printf("%03o ", buffer[i]);
            }
            break;
        }
        case F_DEC: {
            for (size_t i = 0; i < bytes_read; i++) {
                printf("%03d ", buffer[i]);
            }
            break;
        }
        case F_BIN: {
            for (size_t i = 0; i < bytes_read; i++) {
                char bitstring[9];
                byte_to_binary_string(buffer[i], bitstring);
                printf("%s ", bitstring);
            }
            break;
        }
        default: {
            for (size_t i = 0; i < bytes_read; i++) {
                printf("%02x ", buffer[i]);
            }
        }
    }
    if (bytes_read < line_width) {
        const size_t gap = line_width - bytes_read;
        for (size_t i = 0; i < gap; i++) {
            if (format == F_OCT || format == F_DEC) {
                printf("    ");
            } else if (format == F_BIN) {
                printf("         ");
            } else {
                printf("   ");
            }
        }
    }
}


/* Write the ascii string section of output. */
void write_ascii(const uint8_t *buffer, const size_t bytes_read)
{

    for (size_t i = 0; i < bytes_read; i++) {
        if (buffer[i] >= 0x20 && buffer[i] < 0x7F) {
            printf("%c", buffer[i]);
        } else {
            printf("%lc", MID_DOT);
        }
    }

    if (bytes_read < line_width) {
        const size_t gap = line_width - bytes_read;
        for (size_t i = 0; i < gap; i++) {
            printf(" ");
        }
    }
    printf(" %lc ", VERT_BAR);
    printf("\n");
}


/* Write the output. */
void write_output(const uint8_t *buffer, const int32_t offset, const size_t bytes_read)
{
    if (write_well(offset, bytes_read) == 1) {
        /* Write the vertical bars for the last line. */
        const int32_t bw = get_bin_width();
        for (int32_t i = 0; i < bw; i++) {
            printf(" ");
        }
        printf("%lc", VERT_BAR);
        for (int32_t i = 0; i < line_width; i++) {
            printf(" ");
        }
        printf("  ");
        printf("%lc\n", VERT_BAR);
        return;
    }
    write_binary_dump(buffer, bytes_read);
    printf(" %lc ", VERT_BAR);
    write_ascii(buffer, bytes_read);
}


/* Print the Unicode box-drawing chars to the screen. */
void print_banner(char* filename)
{
    /* Get the terminal width. */
    const int32_t term_width = get_term_width();
    /* Get the binary section width. */
    const int32_t bin_width = get_bin_width();
    /* Get the ascii string section width. */
    const int32_t ascii_width = line_width + 2;
    /* Calculate how many more columns left in the row. */
    const int32_t cols_left = term_width - ascii_width - bin_width - WELL_WIDTH - 3;

    /* First line...
     * Print the horizontal line over the well. */
    for (int32_t i = 0; i < WELL_WIDTH; i++) {
        printf("%lc", HORT_BAR);
    }
    /* Print the '┬' */
    printf("%lc", DOWN_T);
    /* Complete the horizontal line to end of terminal. */
    const int rest = term_width - WELL_WIDTH - 1;
    for (int32_t i = 0; i < rest; i++) {
        printf("%lc", HORT_BAR);
    }
    printf("\n");

    /* Second line....
     * print spaces over the well. */
    for (int32_t i = 0; i < WELL_WIDTH; i++) {
        printf("%s", " ");
    }
    /* Print the v-bar */
    printf("%lc", VERT_BAR);
    /* Print the filename. */
    printf(" File: %s\n", filename);

    /* Third line...
    *Print the horizontal line over the well. */
    for (int32_t i = 0; i < WELL_WIDTH; i++) {
        printf("%lc", HORT_BAR);
    }
    /* Print the '┼' */
    printf("%lc", CROSS);
    /* Print the horizontal bar over the binary content. */
    for (int32_t i = 0; i < bin_width; i++) {
        printf("%lc", HORT_BAR);
    }
    /* Print the first '┬' */
    printf("%lc", DOWN_T);
    /* Print the horizontal bar over the ascii content. */
    for (int32_t i = 0; i < ascii_width; i++) {
        printf("%lc", HORT_BAR);
    }
    /* Print the final '┬' */
    printf("%lc", DOWN_T);
    /* Complete the horizontal line to end of terminal. */
    for (int32_t i = 0; i < cols_left; i++) {
        printf("%lc", HORT_BAR);
    }

    printf("\n");
}


void print_footer()
{
    for (int i = 0; i < WELL_WIDTH; i++) {
        printf("%lc", HORT_BAR);
    }
    printf("%lc", UP_T);
    const int32_t bw = get_bin_width();
    for (int i = 0; i < bw; i++) {
        printf("%lc", HORT_BAR);
    }
    printf("%lc", UP_T);
    const int32_t ascii_width = line_width + 2;
    for (int32_t i = 0; i < ascii_width; i++) {
        printf("%lc", HORT_BAR);
    }
    printf("%lc", UP_T);
    const int32_t rest = get_term_width() - WELL_WIDTH - bw - ascii_width - 3;
    for (int i = 0; i < rest; i++) {
        printf("%lc", HORT_BAR);
    }
}


int main(const int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int opt;

    const struct option longopts[] = {
        {"hex",          no_argument,       nullptr, 'x'},
        {"oct",          no_argument,       nullptr, 'o'},
        {"dec",          no_argument,       nullptr, 'd'},
        {"bin",          no_argument,       nullptr, 'b'},
        {"start-offset", required_argument, nullptr, 's'},
        {"read-bytes",   required_argument, nullptr, 'n'},
        {"line-width",   required_argument, nullptr, 'l'},
        {"help",         no_argument,       nullptr, 'h'},
        {"version",      no_argument,       nullptr, 'V'},
        {nullptr,0,nullptr,0}
    };

    int32_t offset = 0;

    while ((opt = getopt_long(argc, argv, "Vhxodbs:n:l:", longopts, nullptr)) != -1) {
        switch(opt) {
            case 'x':
                format = F_HEX;
                break;
            case 'd':
                format = F_DEC;
                break;
            case 'o':
                format = F_OCT;
                break;
            case 'b':
                format = F_BIN;
                /* For binary output, set the default line width to 8. */
                line_width = 8;
                break;
            case 's': {
                const long int start_offset = strtol(optarg, nullptr, 10);
                offset = (int32_t)start_offset;
                break;
            }
            case 'l': {
                const long int width = strtol(optarg, nullptr, 10);
                if (width < 0 || width > 255) {
                    printf("invalid width: %ld\n", width);
                    exit(EXIT_FAILURE);
                }
                line_width = (uint8_t)width;
                break;
            }
            case 'V':
                printf("%s version %s\n", APPNAME, APPVERSION);
                printf("%s compiled on %s at %s\n",
                       strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__,
                       __DATE__, __TIME__);
                exit(EXIT_SUCCESS);
            case 'h':
                show_help();
                exit(EXIT_SUCCESS);
            case ':':
            case '?':
                /* getopt_long prints own error message */
                exit(EXIT_FAILURE);
            default:
                show_help();
                exit(EXIT_FAILURE);
        }
    }

    /* Open arg/stdin for reading. */
    FILE* input;
    char* filename;
    if (argc > 1) {
        input = fopen(argv[optind], "r");
        filename = argv[optind];
    } else {
        input = stdin;
        filename = "STDIN";
    }

    print_banner(filename);

    /* Allocate the byte buffer based on line_width. */
    uint8_t *buffer = malloc(sizeof(int8_t) * line_width);
    if (!buffer) {
        printf("failed to allocate buffer\n");
        exit(EXIT_FAILURE);
    }

    /* Call fseek() if --start-offset is used. */
    if (offset != 0) {
        fseek(input, offset, SEEK_SET);
    }

    while (true) {
        const size_t bytes_read = read_input(input, buffer);
        write_output(buffer, offset, bytes_read);
        if (bytes_read == 0) {
            break;
        }
        offset += (int32_t)bytes_read;
    }

    print_footer();

    free(buffer);
    return EXIT_SUCCESS;
}
