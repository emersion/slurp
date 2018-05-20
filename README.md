# slurg

Select a region in a Wayland compositor and print it to the standard output.

It currently works on Sway 1.0 alpha.

## Building

Install dependencies:
* meson
* wayland
* cairo

Then run:

```shell
meson build
ninja -C build
build/slurg
```

## License

MIT
