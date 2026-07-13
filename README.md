# bdump

A modern-ish remake of `od`, `hexdump`, `xxd`, and similar byte-dumping utilities. The look and feel is largely inspired by 
[bat](https://github.com/sharkdp/bat).

![A screenshot of bdump](./img/screenshot1.png "Screenshot 1")

![A screenshot of bdump](./img/screenshot2.png "Screenshot 2")

![A screenshot of bdump](./img/screenshot3.png "Screenshot 3")


## Synopsis

```text
bdump [-xodSHWBLbnsrlhV] [-s N] [-r N] [-l N] [FILE]
```

If `FILE` is omitted, input is read from standard input.

## Description

`bdump` prints unambiguous representations of individual bytes, or 2 and 4 byte groupings, along with a textual 
representation of bytes that fall within the printable ASCII range.

Input may be provided either as a filename argument or via standard input.

Output is divided into four logical sections using Unicode box-drawing characters:

### Banner

The banner currently displays the name of the file being dumped.

### Offset Well

A fixed-width column displaying the starting offset of each output line.

Offsets are displayed using the currently selected output format. By default, this is hexadecimal.

### Data Dump

The main output area containing the byte values.

The default format is unsigned hexadecimal, but output may also be displayed as:

* Unsigned octal
* Unsigned decimal
* Signed decimal
* Unsigned binary

Changing the output radix also changes the offset format, except when binary output is selected. In binary mode, offsets continue to be displayed in hexadecimal.

### ASCII Dump

Displays printable ASCII characters directly.

Bytes outside the printable ASCII range are shown as the Unicode middle dot character (`·`, U+00B7).

### Line Width

The number of bytes displayed per row can be controlled with the `--line-width` option.

The default width is 16 bytes.

Regardless of line width, each output row contains:

1. The offset of the first byte on the line
2. The byte values in the selected format
3. The printable ASCII representation of those same bytes

## Options

### `-h`, `--help`

Display usage information.

### `-V`, `--version`

Display version information.

### `-x`, `--hex`

Display output in hexadecimal format.

This is the default.

### `-o`, `--oct`

Display output in octal format.

### `-d`, `--unsigned`

Display output in unsigned decimal format.

### `-S`, `--signed`

Display output in signed decimal format.

### `-b`, `--bin`

Display output as binary bit strings. Offsets are displayed in hexadecimal, and output can only be 
displayed as single bytes (ie: -H and -W will be silently ignored).

### `-L`, `--little-endian`

Display 2 and 4 byte groupings in little-endian byte order.

### `-B`, `--big-endian`

Display 2 and 4 byte groupings in big-endian byte order.

### `-n`, `---no-elide`

By default, 2+ lines of just zero bytes (NULLs) are elided, and replaced with a message which states how many lines
were elided. With this option, all the zero-byte lines are printed.

### `-l N`, `--line-width=N`

Display `N` bytes per output row.

The value is capped at 255. In practice, values larger than roughly 48 may become difficult to read depending on terminal width.

### `-s N`, `--start-offset=N`

Begin dumping at byte offset `N`.

In other words, skip the first `N` bytes of input.

### `-r N`, `--read-size=N`

Stop after reading `N` bytes.

This option may be combined with `--start-offset` to dump arbitrary slices of a file.

## Bugs

The width of the offset well is fixed at 8 zero-padded chars no matter which radix is used to
display it. This is not really a practical problem, but keep in mind that the file-size limits 
for each of hex, dec, and oct is ~4GB, ~95MB, and ~16MB respectively. When dumping files larger 
than this the columns will become unaligned to accommodate the wider offset display.

I probably can't be arsed to calculate the necessary offset well width based on file size 
dynamically, but I could add a char or two to the fixed-width if enough people complain about it.

## Author

Darren Kirby [darren@dragonbyte.ca](mailto:darren@dragonbyte.ca)

## See Also

* `od(1)`
* `hexdump(1)`
* `xxd(1)`

# Building

A simple `make` in the project root will generally do the trick. To install try `make install`. 
You can also pass variables to affect compilation/installation, eg: `make CC=/my/custom/gcc` or
`make install PREFIX=/my/cestom/install/path`.

Note that `bdump` uses contemporary C23 coding standards, and as such, older compilers may have issues. 
GCC 14+ and LLVM/Clang 19+ should be fine.

If you are stuck with an older compiler and the build fails, try changing the `constexpr`s on lines 54 and 55 of
the source to jut `const` and it should build.
