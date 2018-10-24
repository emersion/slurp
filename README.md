# slurp

Select a region in a Wayland compositor and print it to the standard output.
Works well with [grim](https://github.com/emersion/grim).

It currently works on Sway 1.0.

## Building

Install dependencies:
* meson
* wayland
* cairo

Then run:

```shell
meson build
ninja -C build
build/slurp
```

## Contributing

Either [send GitHub pull requests][1] or [send patches on the mailing list][2].

## License

MIT

[1]: https://github.com/emersion/slurp
[2]: https://lists.sr.ht/%7Eemersion/public-inbox
