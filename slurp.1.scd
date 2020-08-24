slurp(1)

# NAME

slurp - select a region in a Wayland compositor

# DESCRIPTION

*slurp* [options...]

# SYNOPSIS

slurp is a command-line utility to select a region from Wayland compositors
which support the layer-shell protocol. It lets the user hold the pointer to
select, or click to cancel the selection.

If the standard input is not a TTY, slurp will read a list of predefined
rectangles for quick selection. Each line must be in the form
"<x>,<y> <width>x<height> [label]". The label is optional and can be any string
that doesn't contain newlines. It can be accessed using the "%l" sequence in a
format string.

If the _Esc_ key is pressed, selection is cancelled. If the _Space_ key is
held, the selection is moved instead of being resized.

# OPTIONS

*-h*
	Show help message and quit.

*-d*
	Display dimensions of selection.

*-b* _color_
	Set background color. See *COLORS* for more detail.

*-c* _color_
	Set border color. See *COLORS* for more detail.

*-s* _color_
	Set selection color. See *COLORS* for more detail.

*-w* _weight_
	Set border weight.

*-f* _format_
	Set format. See *FORMAT* for more detail.

*-p*
	Select a single pixel instead of a rectangle. This mode ignores any
	predefined rectangles read from the standard input.

*-o*
	Add predefined rectangles for all outputs, as if provided on standard input.
	The label will be the name of the output.

# COLORS

Colors may be specified in #RRGGBB or #RRGGBBAA format. The # is optional.

# FORMAT

Interpreted sequences are:

%x	The x-coordinate of the selection

%y	The y-coordinate of the selection

%w	The width of the selection

%h	The height of the selection

%l	Label included with region from stdin

%o	The name of the output containing the top left corner, or "<unknown>" if
	not known

The default format is "%x,%y %wx%h\\n".

# AUTHORS

Maintained by Simon Ser <contact@emersion.fr>, who is assisted by other
open-source contributors. For more information about slurp development, see
https://github.com/emersion/slurp.
