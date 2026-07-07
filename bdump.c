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
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <locale.h>
#include <errno.h>
#include <sys/stat.h>
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

/* Static lookup tables for formatting hex and oct strings. */
static constexpr char hex_chars[] = "0123456789abcdef";
static constexpr char oct_chars[] = "01234567";

/* This is an arbitrary constant that sets the upper
 * limit for typed input in lieu of file arguments. */
#define MAX_READ_BYTES 5096

/* L1/L2 cache friendly buffer size. */
#define CHUNK_SIZE 8192

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
/* Default is to read all bytes. This value will be filled by
 * call to stat() if --read-size is not used. */
size_t read_size = 0;


void show_help(void)
{
    printf("Usage: %s [OPTION] FILE\n\n\
Options:\n\
  Output format options:\n\
    -x, --hex\t\t hexadecimal format\n\
    -o, --oct\t\t octal format\n\
    -d, --dec\t\t decimal format\n\
    -b, --bin\t\t binary format\n\
  General options:\n\
    -n, --no-elide\t don't elide lines of NULL bytes\n\
    -l, --line-width=n\t print n bytes per line\n\
    -s, --start-offset=n start output at offset n\n\
    -r, --read-size=n\t read only n bytes\n\
    -h, --help\t\t display this help\n\
    -V, --version\t display version information\n\n\
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


int32_t get_term_width(void)
{
    /* Check if stdout is redirected to a file or a pipe. */
    if (!isatty(STDOUT_FILENO)) {
        /* This is width of default line-width and hex output. */
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
    switch (format) {
        case F_DEC:
        case F_OCT:
            /* OCT and DEC: 3 chars + 1 space for each + 1 space left side. */
            return line_width * 4 + 1;
        case F_BIN:
            /* BIN: 8 chars + 1 space for each + 1 space on left side. */
            return line_width * 9 + 1;
        default:
            /* HEX: 2 chars + 1 space for each + 1 space on left side. */
            return line_width * 3 + 1;
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


/* Write the offset well section of output. */
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
    /* A stack buffer large enough for the widest format (BIN: 9 chars * 255 bytes max = 2295). */
    char line_buf[4096]; 
    size_t pos = 0;

    switch (format) {
        case F_HEX: {
            for (size_t i = 0; i < bytes_read; i++) {
                line_buf[pos++] = hex_chars[(buffer[i] >> 4) & 0x0F];
                line_buf[pos++] = hex_chars[buffer[i] & 0x0F];
                line_buf[pos++] = ' ';
            }
            break;
        }
        case F_OCT: {
            for (size_t i = 0; i < bytes_read; i++) {
                line_buf[pos++] = oct_chars[(buffer[i] >> 6) & 0x07];
                line_buf[pos++] = oct_chars[(buffer[i] >> 3) & 0x07];
                line_buf[pos++] = oct_chars[buffer[i] & 0x07];
    
                line_buf[pos++] = ' ';
            }
            break;
        }
        case F_DEC: {
            for (size_t i = 0; i < bytes_read; i++) {
                pos += snprintf(&line_buf[pos], sizeof(line_buf) - pos, "%3d ", buffer[i]);
            }
            break;
        }
        case F_BIN: {
            for (size_t i = 0; i < bytes_read; i++) {
                char bitstring[9];
                byte_to_binary_string(buffer[i], bitstring);
                pos += snprintf(&line_buf[pos], sizeof(line_buf) - pos, "%s ", bitstring);
            }
            break;
        }
        default:
            return;
    }

    /* Handle the padding gap exactly. */ 
    if (bytes_read < line_width) {
        const size_t gap = line_width - bytes_read;
        const int pad_chars = (format == F_OCT || format == F_DEC) ? 4 : (format == F_BIN ? 9 : 3);
        
        memset(&line_buf[pos], ' ', gap * pad_chars);
        pos += gap * pad_chars;
    }

    fwrite(line_buf, 1, pos, stdout);
}


void write_ascii(const uint8_t *buffer, const size_t bytes_read)
{
    char ascii_buf[512]; 
    size_t pos = 0;

    for (size_t i = 0; i < bytes_read; i++) {
        if (buffer[i] >= 0x20 && buffer[i] < 0x7F) {
            ascii_buf[pos++] = (char)buffer[i];
        } else {
            /* Add the UTF-8 bytes for MID_DOT. */
            ascii_buf[pos++] = (char)0xC2;
            ascii_buf[pos++] = (char)0xB7;
        }
    }

    /* Handle the padding gap. */
    if (bytes_read < line_width) {
        const size_t gap = line_width - bytes_read;
        memset(&ascii_buf[pos], ' ', gap);
        pos += gap;
    }

    /* Add the final VERT_BAR. */
    ascii_buf[pos++] = ' ';
    ascii_buf[pos++] = (char)0xE2;
    ascii_buf[pos++] = (char)0x94;
    ascii_buf[pos++] = (char)0x82;
    ascii_buf[pos++] = '\n';

    fwrite(ascii_buf, 1, pos, stdout);
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
    printf("%lc ", VERT_BAR);
    write_ascii(buffer, bytes_read);
}


void print_elide_line(const uint32_t n_lines)
{
    int32_t bin_width = get_bin_width();

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


int64_t validate_numeric_arg(char* arg, const int32_t max_val, char* flag) {
    /* 'Special value' 0 for base is interpreted as decimal,
     * or hex/oct if 0x or 0 prefix is present. */
    const long int value = strtol(arg, nullptr, 0);
    if (value == 0) {
        fprintf(stderr, "Invalid number: %s\n", arg);
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
    int32_t offset = 0;

    /* Zeroed-out memory to compare for lines of just NUL bytes. */
    static const uint8_t zero_block[256] = {0};
    /* Counter of elided lines. */
    uint32_t n_elided = 0;
    /* Flag for whether to elide or not. */
    bool elide = 1;

    const struct option longopts[] = {
        {"hex",          no_argument,       nullptr, 'x'},
        {"oct",          no_argument,       nullptr, 'o'},
        {"dec",          no_argument,       nullptr, 'd'},
        {"bin",          no_argument,       nullptr, 'b'},
        {"no-elide",     no_argument,       nullptr, 'n'},
        {"start-offset", required_argument, nullptr, 's'},
        {"read-size",    required_argument, nullptr, 'r'},
        {"line-width",   required_argument, nullptr, 'l'},
        {"help",         no_argument,       nullptr, 'h'},
        {"version",      no_argument,       nullptr, 'V'},
        {nullptr,0,nullptr,0}
    };


    while ((opt = getopt_long(argc, argv, "xodbns:r:l:hV", longopts, nullptr)) != -1) {
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
            case 'n':
                elide = 0;
                break;
            case 's': {
                offset = (int32_t)validate_numeric_arg(optarg, 0, "--start-offset");
                break;
            }
            case 'r': {
                read_size = (size_t)validate_numeric_arg(optarg, 0, "--read-size");
                break;
            }
            case 'l': {
                line_width = (uint8_t)validate_numeric_arg(optarg, 255, "--line-width");
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
    /* Ensure we only call fopen() on passed args,
     * and not on shell I/O redirects. */
    if (optind < argc) {
        input = fopen(argv[optind], "r");
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
        if (fseek(input, offset, SEEK_SET) < 0) {
            /* fseek() fails on pipes. We must manually consume and discard 
             * 'offset' bytes to reach the correct starting position in the stream. */
            size_t bytes_to_discard = (size_t)offset;
            uint8_t discard_buf[4096];
            
            while (bytes_to_discard > 0) {
                size_t grab = (bytes_to_discard < sizeof(discard_buf)) ? bytes_to_discard : sizeof(discard_buf);
                size_t read_in = fread(discard_buf, 1, grab, input);
                
                if (read_in == 0) {
                    break; /* EOF reached before we even hit the offset */
                }
                bytes_to_discard -= read_in;
            }
        }
    }


    /* This forces printf to buffer 64KB before calling write(). */
    char stdout_buffer[CHUNK_SIZE];
    setvbuf(stdout, stdout_buffer, _IOFBF, sizeof(stdout_buffer));


    uint8_t *file_buf = malloc(CHUNK_SIZE);
    if (!file_buf) {
        fprintf(stderr, "failed to allocate buffer\n");
        exit(EXIT_FAILURE);
    }

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
                const bool is_zero = (memcmp(&file_buf[i], zero_block, chunk_len) == 0);
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

            offset += (int32_t)chunk_len;
            i += chunk_len;
        }
        read_size -= bytes_read;
    }

    if (n_elided > 1) {
        print_elide_line(n_elided - 1);
    }

    print_footer();
    free(file_buf);
    return EXIT_SUCCESS;
}
