slurp(1)

# NAME

slurp - select a region in a Wayland compositor

# DESCRIPTION

*slurp* [options...]

# SYNOPSIS

slurp is a command-line utility to select a region from Wayland compositors
which support the layer-shell protocol. It lets the user hold the pointer to
select, or click to cancel the selection.

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

# COLORS

Colors may be specified in #RRGGBB or #RRGGBBAA format. The # is optional.

# AUTHORS

Maintained by Simon Ser <contact@emersion.fr>, who is assisted by other
open-source contributors. For more information about slurp development, see
https://github.com/emersion/slurp.
