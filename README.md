# sgd2png

Convert SGD images to 8 or 16 color PNG with custom palette. Optionally
highlight selection sets with different color and write cropped image of each
selection set.

## Synopsis

`sgd2png [options] <SGD-file> [...]`

## Description

| Option      | Description |
| ----------- | ----------- |
| `-c`        | Also output cropped pictures of each selection set
| `-f`        | Also output full pictures of each selection set
| `-p <file>` | Load alternative 8 or 16 color palette from file
| `-z <0-9>`  | Set PNG compression level
| `-o <path>` | Set output directory
| `-h`        | Show help message

By default, PNG images are written to current directory. This can be overridden
with `-o <path>`. Each instance of `###` substring in `path` is replaced with
first 3 characters of source filename.

## Example

Convert all SGD files under `src` in 8 threads and store them in directories
under `dst` designated by filename prefix, using palette `example.pal` and
writing full and cropped image of each selection set:

`find /path/to/src -type f -name *.zgd | xargs -P 8 -n 100 ./sgd2png -cf -p example.pal -o /path/to/dst/###`

## Palette file

Palette file must contain 8 or 16 colors in hexadecimal `RR GG BB` format, one
color per line. First 8 colors are regular colors, last 8 are selection colors.
If palette file has 8 colors, selection colors are obtained from the first 8 by
zeroing the blue component.

By default, images are output with custom palette fixed in source code. To get
original colors, replace it with actual SGD palette (not included here).
