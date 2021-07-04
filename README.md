# slurp

Select a region in a Wayland compositor and print it to the standard output.
Works well with [grim](https://github.com/emersion/grim).

Join the IRC channel: #emersion on Libera Chat.

## Building

Install dependencies:

* meson
* wayland
* cairo
* libxkbcommon
* scdoc (optional: man pages)

Then run:

```sh
meson build
ninja -C build
build/slurp
```

## Example usage

Select a region and print it to stdout:

```sh
slurp
```

Select a single point instead of a region:

```sh
slurp -p
```

Select an output and print its name:

```sh
slurp -o -f "%o"
```

Select a window under Sway, using `swaymsg` and `jq`:

```sh
swaymsg -t get_tree | jq -r '.. | select(.pid? and .visible?) | .rect | "\(.x),\(.y) \(.width)x\(.height)"' | slurp
```

## Contributing

Either [send GitHub pull requests][1] or [send patches on the mailing list][2].

## License

MIT

[1]: https://github.com/emersion/slurp
[2]: https://lists.sr.ht/%7Eemersion/public-inbox
