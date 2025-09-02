# slurp

Select a region in a Wayland compositor and print it to the standard output.
Works well with [grim].

Join the IRC channel: [#emersion on Libera Chat][IRC].

## Building

Install dependencies:

* meson
* wayland
* cairo
* libxkbcommon
* scdoc (optional: man pages)

Then run:

```sh
git clone https://github.com/emersion/slurp
cd slurp
meson setup build
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

Select a window without border under Sway, using `swaymsg` and `jq`:

```sh
swaymsg -t get_tree | jq -r '.. | select(.pid? and .visible?) | "\(.rect.x+.window_rect.x),\(.rect.y+.window_rect.y) \(.window_rect.width)x\(.window_rect.height)"' | slurp
```
## Contributing

Either [send GitHub pull requests][GitHub] or [send patches on the mailing list][ML].

## License

MIT

[grim]: https://gitlab.freedesktop.org/emersion/grim
[IRC]: https://web.libera.chat/gamja/#emersion
[GitHub]: https://github.com/emersion/slurp
[ML]: https://lists.sr.ht/%7Eemersion/public-inbox
