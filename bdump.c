/*
 * 'bdump.c'
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
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <locale.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#define APPNAME "bdump"
#define APPVERSION "1.0.0"

/* Constants for box-drawing, and others. */
#define WELL_WIDTH 12
#define VERT_BAR 0x2502
#define HORT_BAR 0x2500
#define DOWN_T   0x252C
#define UP_T     0x2534
#define CROSS    0x253C
#define MID_DOT  0x00B7

/* Determine machine endianess for default output. */
#ifndef __BYTE_ORDER__
bool little_endian = true;
#else
bool little_endian = (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);
#endif

/* Static lookup tables for formatting hex and oct strings. */
static constexpr char hex_chars[] = "0123456789abcdef";
static constexpr char oct_chars[] = "01234567";

/* This is an arbitrary constant that sets the upper
 * read limit for piped input. */
#define MAX_READ_BYTES SIZE_MAX

/* A buffer large enough for the widest format:
 * (BIN: 9 chars * 255 bytes max = 2295 + 32B padding). */
#define MAX_LINE_BUF_LEN ((255 * 9) + 32)

/* 255 * 2 = 510 bytes + 5 more:
 * (space + 3-byte UTF-8 vbar + newline. */
#define ASCII_BUF_SIZE 515

/* L1/L2 cache friendly read-buffer size. */
#define CHUNK_SIZE 8192

typedef enum : int8_t {
    F_HEX,
    F_OCT,
    F_UNSIGNED,
    F_SIGNED,
    F_BIN
} format_t;

typedef enum : int8_t {
    O_BYTE,
    O_HALF_WORD,
    O_WORD
} output_t;

/* Default format: hex */
format_t format = F_HEX;
/* Default output: single bytes. */
output_t output = O_BYTE;
/* Default line_width: 16 */
uint8_t line_width = 16;
/* Default is to read all bytes. This value will be filled by
 * call to stat() if --read-size is not used. */
size_t read_size = 0;
/* This does not change per run, so cache it. */
int32_t bin_width;


void show_help(void)
{
    printf("Usage: %s [OPTION(s)] [FILE]\n\n\
Options:\n\
    Output radix options:\n\
        -x, --hex\t\t hexadecimal\n\
        -o, --oct\t\t octal\n\
        -b, --bin\t\t binary\n\
        -S, --signed\t\t signed decimal\n\
        -d, --unsigned\t\t unsigned decimal\n\n\
    Output byte grouping options:\n\
        -H, --half-word\t\t output 2 byte groupings\n\
        -W, --word\t\t output 4 byte groupings\n\n\
    Output byte-order options:\n\
        -L, --little-endian\t output in little-endian\n\
        -B, --big-endian\t output in big-endian\n\n\
    General options:\n\
        -n, --no-elide\t\t don't elide lines of NULL bytes\n\
        -l, --line-width=n\t print n bytes per line\n\
        -s, --start-offset=n\t start output at offset n\n\
        -r, --read-size=n\t read only n bytes\n\
        -h, --help\t\t display this help\n\
        -V, --version\t\t display version information\n\n\
Report bugs to <darren@dragonbyte.ca>\n", APPNAME);
}


size_t get_file_size(const int fd)
{
    struct stat buf;
    if (fstat(fd, &buf) == -1) {
        fprintf(stderr, "stat failed: %s\n",
            strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (!S_ISREG(buf.st_mode)) {
        /* Input was either piped or no input was supplied.
         * just return an arbitrary large number. */
        return MAX_READ_BYTES;
    }
    return buf.st_size;
}

/* The value returned by this function only really
 * affects the length of the horizontal lines. */
int32_t get_term_width(void)
{
    /* Check if stdout is redirected to a file or a pipe. */
    if (!isatty(STDOUT_FILENO)) {
        /* This is width of default line-width with hex output. */
        return 82;
    }

    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0) {
        fprintf(stderr, "ioctl failed: %s\n",
            strerror(errno));
        exit(EXIT_FAILURE);
    }
    return w.ws_col;
}


/* Calculate the width of the binary section based on output
 * format and line_width. */
int32_t get_bin_width(void)
{
    int n_groups;
    switch (output) {
    /* Half word. */
    case O_HALF_WORD:
        n_groups = line_width / 2;
        switch (format) {
        case F_UNSIGNED:
            /* DEC: 5 chars + 1 space for each group + 1 space left side. */
            return n_groups * 6 + 1;
        case F_OCT:
        case F_SIGNED:
            /* Signed DEC and OCT: 6 chars + 1 space for each group + 1 space on left side. */
            return n_groups * 7 + 1;
        default:
            /* HEX: 4 chars + 1 space for each group + 1 space on left side. */
            return n_groups * 5 + 1;
        }
    /* Full word. */
    case O_WORD:
        n_groups = line_width / 4;
        switch (format) {
        case F_UNSIGNED:
            /* DEC: 10 chars + 1 space for each group + 1 space on left side. */
            return n_groups * 11 + 1;
        case F_SIGNED:
        case F_OCT:
            /* Signed DEC and OCT: 11 chars + 1 space for each group + 1 space on left side. */
            return n_groups * 12 + 1;
        default:
            /* HEX: 8 chars + 1 space for each + 1 space on left side. */
            return n_groups * 9 + 1;
    }
    /* Single byte. */
    default:
        switch (format) {
        case F_UNSIGNED:
        case F_OCT:
            /* OCT and DEC: 3 chars + 1 space for each + 1 space on left side. */
            return line_width * 4 + 1;
        case F_SIGNED:
            /* Signed DEC: 4 chars + 1 space for each, plus 1 space on left side. */
            return line_width * 5 + 1;
        case F_BIN:
            /* BIN: 8 chars + 1 space for each + 1 space on left side. */
            return line_width * 9 + 1;
        default:
            /* HEX: 2 chars + 1 space for each + 1 space on left side. */
            return line_width * 3 + 1;
        }
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


/* Write the offset well section of output. Returns a boolean
 * indicating if we have printed the last data line. */
bool write_well(const uint64_t offset, const size_t bytes_read)
{
    switch (format) {
    case F_OCT:
        if (bytes_read == 0) {
            printf(" 0o%08" PRIo64 " %lc", offset, VERT_BAR);
            return true;
        }
        printf(" 0o%08" PRIo64 " %lc ", offset, VERT_BAR);
        return false;
    case F_UNSIGNED:
    case F_SIGNED:
        if (bytes_read == 0) {
            printf(" 0d%08" PRIu64 " %lc", offset, VERT_BAR);
            return true;
        }
        printf(" 0d%08" PRIu64 " %lc ", offset, VERT_BAR);
        return false;
    default:
        if (bytes_read == 0) {
            printf(" 0x%08" PRIx64 " %lc", offset, VERT_BAR);
            return true;
        }
        printf(" 0x%08" PRIx64 " %lc ", offset, VERT_BAR);
        return false;
    }
}


/* Build a half-word from 2 bytes. */
static uint16_t parse_half_word(const uint8_t a, const uint8_t b)
{
    uint16_t half_word;
    if (little_endian) {
        half_word = (uint16_t)b << 8 | (uint16_t)a;
    } else {
        half_word = (uint16_t)a << 8 | (uint16_t)b;
    }
    return half_word;
}


/* Zero-pad incomplete half-word groupings. */
static uint16_t load_half_word(const uint8_t *buf, const size_t remaining)
{
    const uint8_t a = remaining > 0 ? buf[0] : 0;
    const uint8_t b = remaining > 1 ? buf[1] : 0;

    return parse_half_word(a, b);
}


/* Build a word from 4 bytes. */
static uint32_t parse_word(const uint8_t a, const uint8_t b, const uint8_t c, const uint8_t d)
{
    uint32_t word;
    if (little_endian) {
        word = (uint32_t)d << 24 | (uint32_t)c << 16 | (uint32_t)b << 8 | (uint32_t)a;
    } else {
        word = (uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)c << 8 | (uint32_t)d;
    }
    return word;
}


/* Zero-pad incomplete word groupings. */
static uint32_t load_word(const uint8_t *buf, size_t remaining)
{
    uint8_t a = remaining > 0 ? buf[0] : 0;
    uint8_t b = remaining > 1 ? buf[1] : 0;
    uint8_t c = remaining > 2 ? buf[2] : 0;
    uint8_t d = remaining > 3 ? buf[3] : 0;

    return parse_word(a, b, c, d);
}


size_t write_hex_dump(char *line_buf, const uint8_t *buffer, const size_t bytes_read)
{
    size_t pos = 0;

    if (output == O_BYTE) {
        for (size_t i = 0; i < bytes_read; i++) {
            line_buf[pos++] = hex_chars[(buffer[i] >> 4) & 0x0F];
            line_buf[pos++] = hex_chars[buffer[i] & 0x0F];
            line_buf[pos++] = ' ';
        }
        return pos;
    }

    if (output == O_HALF_WORD) {
        for (size_t i = 0; i < bytes_read; i+=2) {
            const uint16_t half_word = load_half_word(&buffer[i], bytes_read - i);

            line_buf[pos++] = hex_chars[(half_word >> 12) & 0x0F];
            line_buf[pos++] = hex_chars[(half_word >> 8) & 0x0F];
            line_buf[pos++] = hex_chars[(half_word >> 4) & 0x0F];
            line_buf[pos++] = hex_chars[half_word & 0x0F];
            line_buf[pos++] = ' ';
        }
        return pos;
    }


    for (size_t i = 0; i < bytes_read; i+=4) {
        const uint32_t word = load_word(&buffer[i], bytes_read - i);

        line_buf[pos++] = hex_chars[(word >> 28) & 0x0F];
        line_buf[pos++] = hex_chars[(word >> 24) & 0x0F];
        line_buf[pos++] = hex_chars[(word >> 20) & 0x0F];
        line_buf[pos++] = hex_chars[(word >> 16) & 0x0F];
        line_buf[pos++] = hex_chars[(word >> 12) & 0x0F];
        line_buf[pos++] = hex_chars[(word >> 8) & 0x0F];
        line_buf[pos++] = hex_chars[(word >> 4) & 0x0F];
        line_buf[pos++] = hex_chars[word & 0x0F];
        line_buf[pos++] = ' ';
    }
    return pos;
}


size_t write_oct_dump(char *line_buf, const uint8_t *buffer, const size_t bytes_read) {
    size_t pos = 0;

    if (output == O_BYTE) {
        for (size_t i = 0; i < bytes_read; i++) {
            line_buf[pos++] = oct_chars[(buffer[i] >> 6) & 0x07];
            line_buf[pos++] = oct_chars[(buffer[i] >> 3) & 0x07];
            line_buf[pos++] = oct_chars[buffer[i] & 0x07];

            line_buf[pos++] = ' ';
        }
        return pos;
    }

    if (output == O_HALF_WORD) {
        for (size_t i = 0; i < bytes_read; i+=2) {
            const uint16_t half_word = load_half_word(&buffer[i], bytes_read - i);

            line_buf[pos++] = oct_chars[(half_word >> 15) & 0x07];
            line_buf[pos++] = oct_chars[(half_word >> 12) & 0x07];
            line_buf[pos++] = oct_chars[(half_word >> 9) & 0x07];
            line_buf[pos++] = oct_chars[(half_word >> 6) & 0x07];
            line_buf[pos++] = oct_chars[(half_word >> 3) & 0x07];
            line_buf[pos++] = oct_chars[half_word & 0x07];

            line_buf[pos++] = ' ';
        }
        return pos;
    }

    for (size_t i = 0; i < bytes_read; i+=4) {
        const uint32_t word = load_word(&buffer[i], bytes_read - i);

        line_buf[pos++] = oct_chars[(word >> 30) & 0x07];
        line_buf[pos++] = oct_chars[(word >> 27) & 0x07];
        line_buf[pos++] = oct_chars[(word >> 24) & 0x07];
        line_buf[pos++] = oct_chars[(word >> 21) & 0x07];
        line_buf[pos++] = oct_chars[(word >> 18) & 0x07];
        line_buf[pos++] = oct_chars[(word >> 15) & 0x07];
        line_buf[pos++] = oct_chars[(word >> 12) & 0x07];
        line_buf[pos++] = oct_chars[(word >> 9) & 0x07];
        line_buf[pos++] = oct_chars[(word >> 6) & 0x07];
        line_buf[pos++] = oct_chars[(word >> 3) & 0x07];
        line_buf[pos++] = oct_chars[word & 0x07];

        line_buf[pos++] = ' ';
    }
    return pos;
}


size_t write_signed_dump(char *line_buf, const uint8_t *buffer, const size_t bytes_read) {
    size_t pos = 0;

    if (output == O_BYTE) {
        for (size_t i = 0; i < bytes_read; i++) {
            pos += snprintf(&line_buf[pos], MAX_LINE_BUF_LEN - pos, "%4d ", (int8_t)buffer[i]);
        }
        return pos;
    }

    if (output == O_HALF_WORD) {
        for (size_t i = 0; i < bytes_read; i+=2) {
            const uint16_t half_word = load_half_word(&buffer[i], bytes_read - i);
            pos += snprintf(&line_buf[pos], MAX_LINE_BUF_LEN - pos, "%6d ", (int16_t)half_word);
        }
        return pos;
    }

    for (size_t i = 0; i < bytes_read; i+=4) {
        const uint32_t word = load_word(&buffer[i], bytes_read - i);
        pos += snprintf(&line_buf[pos], MAX_LINE_BUF_LEN - pos, "%11d ", (int32_t)word);
    }
    return pos;
}


size_t write_unsigned_dump(char *line_buf, const uint8_t *buffer, const size_t bytes_read)
{
    size_t pos = 0;

    if (output == O_BYTE) {
        for (size_t i = 0; i < bytes_read; i++) {
            pos += snprintf(&line_buf[pos], MAX_LINE_BUF_LEN - pos, "%3u ", buffer[i]);
        }
        return pos;
    }

    if (output == O_HALF_WORD) {
        for (size_t i = 0; i < bytes_read; i+=2) {
            const uint16_t half_word = load_half_word(&buffer[i], bytes_read - i);
            pos += snprintf(&line_buf[pos], MAX_LINE_BUF_LEN - pos, "%5u ", half_word);
        }
        return pos;
    }

    for (size_t i = 0; i < bytes_read; i+=4) {
        const uint32_t word = load_word(&buffer[i], bytes_read - i);
        pos += snprintf(&line_buf[pos], MAX_LINE_BUF_LEN - pos, "%10u ", word);
    }
    return pos;
}


void calculate_gap_and_padding(size_t *gap, size_t *pad_chars)
{
    switch (output) {
    case O_BYTE: {
        switch (format) {
        case F_HEX: *pad_chars = 3; break;
        case F_BIN: *pad_chars = 9; break;
        case F_SIGNED: *pad_chars = 5; break;
        default: *pad_chars = 4; break;
        }
        break;
    }
    case O_HALF_WORD: {
        *gap = *gap / 2;
        switch (format) {
        case F_HEX:
            *pad_chars = 5;
            break;
        case F_UNSIGNED:
            *pad_chars = 6;
            break;
        default:
            *pad_chars = 7;
            break;
        }
        break;
    }
    case O_WORD: {
        *gap = *gap / 4;
        switch (format) {
        case F_HEX:
            *pad_chars = 9;
            break;
        case F_UNSIGNED:
            *pad_chars = 11;
            break;
        default:
            *pad_chars = 12;
            break;
            }
        }
    }
}


/* Write the binary dump section of output. */
void write_binary_dump(const uint8_t *buffer, const size_t bytes_read)
{
    char line_buf[MAX_LINE_BUF_LEN];
    size_t pos = 0;

    switch (format) {
    case F_HEX:
        pos = write_hex_dump(line_buf, buffer, bytes_read);
        break;
    case F_OCT:
        pos = write_oct_dump(line_buf, buffer, bytes_read);
        break;
    case F_UNSIGNED:
        pos = write_unsigned_dump(line_buf, buffer, bytes_read);
        break;
    case F_SIGNED:
        pos = write_signed_dump(line_buf, buffer, bytes_read);
        break;
    case F_BIN: {
        for (size_t i = 0; i < bytes_read; i++) {
            char bitstring[9];
            byte_to_binary_string(buffer[i], bitstring);
            pos += snprintf(&line_buf[pos], sizeof(line_buf) - pos, "%s ", bitstring);
        }
        break;
    }
    default:
        break;
    }

    /* Handle the padding for partial lines. */
    if (bytes_read < line_width) {
        size_t gap = line_width - bytes_read;
        size_t pad_chars = 0;

        calculate_gap_and_padding(&gap, &pad_chars);

        memset(&line_buf[pos], ' ', gap * pad_chars);
        pos += gap * pad_chars;
    }

    fwrite(line_buf, 1, pos, stdout);
}


void write_ascii(const uint8_t *buffer, const size_t bytes_read)
{
    for (size_t i = 0; i < bytes_read; i++) {
        if (buffer[i] >= 0x20 && buffer[i] < 0x7F) {
            putchar(buffer[i]);
        } else {
            printf("%lc", MID_DOT);
        }
    }

    /* Handle the padding gap. */
    if (bytes_read < line_width) {
        const size_t gap = line_width - bytes_read;
        for (size_t i = 0; i < gap; i++) {
            putchar(' ');
        }
    }

    /* Add the final VERT_BAR. */
    printf(" %lc\n", VERT_BAR);
}


/* Write the output. */
void write_output(const uint8_t *buffer, const uint64_t offset, const size_t bytes_read)
{
    if (write_well(offset, bytes_read)) {
        /* Write the vertical bars for the last line. */
        for (int i = 0; i < bin_width; i++) {
            printf(" ");
        }
        printf("%lc", VERT_BAR);
        for (size_t i = 0; i < line_width; i++) {
            printf(" ");
        }
        printf("  ");
        printf("%lc\n", VERT_BAR);
        return;
    }
    write_binary_dump(buffer, bytes_read);
    printf("%lc ", VERT_BAR);
    write_ascii(buffer, bytes_read);
}


void print_elide_line(const uint32_t n_lines)
{
    /* We need the length of msg to calculate padding,
     * so format the message into a temporary buffer. */
    char msg[128];
    int msg_len = snprintf(msg, sizeof(msg), "   *** %u line%s of zero-bytes elided ***",
        n_lines, n_lines == 1 ? "" : "s");

    /* Print the left well (12 spaces) and the first vertical bar. */
    for (int i = 0; i < WELL_WIDTH; i++) {
        printf(" ");
    }
    printf("%lc", VERT_BAR);

    /* Print the elision message. */
    printf("%s", msg);

    /* Calculate and print the remaining gap to the next border. */
    if (msg_len < bin_width) {
        int gap = bin_width - msg_len;
        for (int i = 0; i < gap; i++) {
            printf(" ");
        }
    }

    printf("%lc", VERT_BAR);

    /* Pad the ASCII section. */
    for (int i = 0; i < line_width + 2; i++) {
        printf(" ");
    }

    /* Print the final vertical bar and newline. */
    printf("%lc\n", VERT_BAR);
}


/* Print the Unicode box-drawing chars to the screen. */
void print_banner(const char* filename)
{
    /* Get the terminal width. */
    const int32_t term_width = get_term_width();
    /* Get the ascii string section width. */
    const int32_t ascii_width = line_width + 2;
    /* Calculate how many more columns left in the row. */
    const int32_t cols_left = term_width - ascii_width - bin_width - WELL_WIDTH - 3;

    /* First line...
     * Print the horizontal line over the well. */
    for (size_t i = 0; i < WELL_WIDTH; i++) {
        printf("%lc", HORT_BAR);
    }
    /* Print the '┬' */
    printf("%lc", DOWN_T);
    /* Complete the horizontal line to end of terminal. */
    const int rest = term_width - WELL_WIDTH - 1;
    for (int i = 0; i < rest; i++) {
        printf("%lc", HORT_BAR);
    }
    printf("\n");

    /* Second line....
     * print spaces over the well. */
    for (size_t i = 0; i < WELL_WIDTH; i++) {
        printf("%s", " ");
    }
    /* Print the v-bar */
    printf("%lc", VERT_BAR);
    /* Print the filename. */
    printf(" File: %s\n", filename);

    /* Third line...
    *Print the horizontal line over the well. */
    for (size_t i = 0; i < WELL_WIDTH; i++) {
        printf("%lc", HORT_BAR);
    }
    /* Print the '┼' */
    printf("%lc", CROSS);
    /* Print the horizontal bar over the binary content. */
    for (int i = 0; i < bin_width; i++) {
        printf("%lc", HORT_BAR);
    }
    /* Print the first '┬' */
    printf("%lc", DOWN_T);
    /* Print the horizontal bar over the ascii content. */
    for (int i = 0; i < ascii_width; i++) {
        printf("%lc", HORT_BAR);
    }
    /* Print the final '┬' */
    printf("%lc", DOWN_T);
    /* Complete the horizontal line to end of terminal. */
    for (int i = 0; i < cols_left; i++) {
        printf("%lc", HORT_BAR);
    }

    printf("\n");
}


void print_footer(void)
{
    for (int i = 0; i < WELL_WIDTH; i++) {
        printf("%lc", HORT_BAR);
    }
    printf("%lc", UP_T);
    for (int i = 0; i < bin_width; i++) {
        printf("%lc", HORT_BAR);
    }
    printf("%lc", UP_T);
    const int32_t ascii_width = line_width + 2;
    for (int i = 0; i < ascii_width; i++) {
        printf("%lc", HORT_BAR);
    }
    printf("%lc", UP_T);
    const int32_t rest = get_term_width() - WELL_WIDTH - bin_width - ascii_width - 3;
    for (int i = 0; i < rest; i++) {
        printf("%lc", HORT_BAR);
    }
}


int64_t validate_numeric_arg(const char* arg, const int32_t max_val, const char* flag)
{
    /* 'Special value' 0 for base is interpreted as decimal,
     * or hex/oct if 0x or 0 prefix is present. */
    char* p;
    errno = 0;
    const long int value = strtol(arg, &p, 0);

    if (errno == ERANGE) {
        if (value == LONG_MAX) {
            fprintf(stderr, "Error: Overflow occurred.\n");
            exit(EXIT_FAILURE);
        }
        if (value == LONG_MIN) {
            fprintf(stderr, "Error: Underflow occurred.\n");
            exit(EXIT_FAILURE);
        }
    }

    if (p == arg) {
        fprintf(stderr, "Error: No digits were found.\n");
        exit(EXIT_FAILURE);
    }

    if (*p != '\0') {
        fprintf(stderr, "Partial conversion: Number is %ld, but trailing junk found: '%s'\n", value, p);
        exit(EXIT_FAILURE);
    }

    if (value < 0 ) {
        fprintf(stderr, "Negative values not valid for %s\n", flag);
        exit(EXIT_FAILURE);
    }

    if (max_val != 0 && value > max_val) {
        fprintf(stderr, "Argument too large for %s\n", flag);
        exit(EXIT_FAILURE);
    }
    return value;
}


int main(const int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int opt;
    uint64_t offset = 0;

    /* Zeroed-out memory to compare for lines of just NUL bytes. */
    static const uint8_t zero_block[256] = {0};
    /* Counter of elided lines. */
    uint32_t n_elided = 0;
    /* Flag for whether to elide or not. */
    bool elide = true;

    const struct option longopts[] = {
        {"hex",           no_argument,       nullptr, 'x'},
        {"oct",           no_argument,       nullptr, 'o'},
        {"unsigned",      no_argument,       nullptr, 'd'},
        {"signed",        no_argument,       nullptr, 'S'},
        {"half-word",     no_argument,       nullptr, 'H'},
        {"word",          no_argument,       nullptr, 'W'},
        {"big-endian",    no_argument,       nullptr, 'B'},
        {"little-endian", no_argument,       nullptr, 'L'},
        {"bin",           no_argument,       nullptr, 'b'},
        {"no-elide",      no_argument,       nullptr, 'n'},
        {"start-offset",  required_argument, nullptr, 's'},
        {"read-size",     required_argument, nullptr, 'r'},
        {"line-width",    required_argument, nullptr, 'l'},
        {"help",          no_argument,       nullptr, 'h'},
        {"version",       no_argument,       nullptr, 'V'},
        {nullptr,0,nullptr,0}
    };


    while ((opt = getopt_long(argc, argv, "xodSHWBLbns:r:l:hV", longopts, nullptr)) != -1) {
        switch(opt) {
        case 'x':
            format = F_HEX;
            break;
        case 'd':
            format = F_UNSIGNED;
            break;
        case 'S':
            format = F_SIGNED;
            break;
        case 'o':
            format = F_OCT;
            break;
        case 'b':
            format = F_BIN;
            /* For binary output, set the default line width to 8. */
            line_width = 8;
            break;
        case 'H':
            output = O_HALF_WORD;
            break;
        case 'W':
            output = O_WORD;
            break;
        case 'L':
            little_endian = true;
            break;
        case 'B':
            little_endian = false;
            break;
        case 'n':
            elide = false;
            break;
        case 's':
            offset = (int32_t)validate_numeric_arg(optarg, 0, "--start-offset");
            break;
        case 'r':
            read_size = (size_t)validate_numeric_arg(optarg, 0, "--read-size");
            break;
        case 'l':
            line_width = (uint8_t)validate_numeric_arg(optarg, 255, "--line-width");
            break;
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
        default:
            /* getopt_long prints own error message. */
            show_help();
            exit(EXIT_FAILURE);
        }
    }

    /* Normalize options. */

    /* Binary format requires single byte output. */
    if (format == F_BIN) {
        output = O_BYTE;
    }
    /* Half-word output requires line_width be divisible by 2. */
    if (output == O_HALF_WORD) {
        line_width = line_width & ~1;
    }
    /* Full-word output requires line_width be divisible by 4. */
    if (output == O_WORD) {
        line_width = (line_width + 2) & ~3;
    }

    /* Calculate and cache bin width. */
    bin_width = get_bin_width();

    /* Open arg/stdin for reading. */
    FILE* input;
    const char* filename;
    /* Ensure we only call fopen() on passed args,
     * and not on shell I/O redirects. */
    if (optind < argc) {
        input = fopen(argv[optind], "rb");
        if (!input) {
            fprintf(stderr, "failed to open %s: %s\n",
                argv[optind], strerror(errno));
            exit(EXIT_FAILURE);
        }
        filename = argv[optind];
    } else {
        input = stdin;
        filename = "STDIN";
    }

    /* Get file size if read_size not set. */
    if (read_size == 0) {
        read_size = get_file_size(fileno(input));
    }

    print_banner(filename);

    /* Call fseek() if --start-offset is used. */
    if (offset != 0) {
        if (fseeko(input, (off_t)offset, SEEK_SET) < 0) {
            /* fseek() fails on pipes. We must manually consume and discard 
             * 'offset' bytes to reach the correct starting position in the stream. */
            size_t bytes_to_discard = offset;
            uint8_t discard_buf[CHUNK_SIZE];
            
            while (bytes_to_discard > 0) {
                const size_t grab = (bytes_to_discard < sizeof(discard_buf)) ? bytes_to_discard : sizeof(discard_buf);
                const size_t read_in = fread(discard_buf, 1, grab, input);
                
                if (read_in == 0) {
                    break; /* EOF reached before we even hit the offset. */
                }
                bytes_to_discard -= read_in;
            }
        }
    }


    /* This forces printf to buffer CHUNK_SIZE before calling write(). */
    char stdout_buffer[CHUNK_SIZE];
    setvbuf(stdout, stdout_buffer, _IOFBF, sizeof(stdout_buffer));

    uint8_t file_buf[CHUNK_SIZE];

    while (read_size > 0) {
        /* Determine how much to read into the big block. */
        const size_t to_read = (read_size < CHUNK_SIZE) ? read_size : CHUNK_SIZE;
        const size_t bytes_read = fread(file_buf, 1, to_read, input);

        if (bytes_read == 0) break;

        size_t i = 0;
        /* Slice the big block into line_width chunks. */
        while (i < bytes_read) {
            const size_t chunk_len = (bytes_read - i < line_width) ? bytes_read - i : line_width;

            if (elide) {
                const bool is_zero = memcmp(&file_buf[i], zero_block, chunk_len) == 0;
                if (is_zero) {
                    n_elided++;
                    if (n_elided == 1) {
                        /* It's the first row of zeros. Print it normally. */
                        write_output(&file_buf[i], offset, chunk_len);
                    }
                } else {
                    if (n_elided > 1) {
                        /* Already printed the first one, so we actually skipped (n_elided - 1). */
                        print_elide_line(n_elided - 1);
                    }

                    n_elided = 0;

                    /* Print the current non-zero row. */
                    write_output(&file_buf[i], offset, chunk_len);
                }
            } else {
                write_output(&file_buf[i], offset, chunk_len);
            }

            offset += chunk_len;
            i += chunk_len;
        }
        read_size -= bytes_read;
    }

    if (n_elided > 1) {
        print_elide_line(n_elided - 1);
    }

    print_footer();
    fclose(input);
    return EXIT_SUCCESS;
}
