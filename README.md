<h1 align="center">Tide Island</h1>

<p align="center">
  <b>A smooth, lightweight, and flexible interactive Dynamic Island for Wayland.</b>
</p>

<p align="center">
  <a href="https://github.com/enhaoswen/Tide-island/stargazers"><img alt="GitHub stars" src="https://img.shields.io/github/stars/enhaoswen/Tide-island?style=flat-square&color=8aadf4"></a>
  <a href="https://github.com/enhaoswen/Tide-island/issues"><img alt="GitHub issues" src="https://img.shields.io/github/issues/enhaoswen/Tide-island?style=flat-square&color=8aadf4"></a>
  <a href="https://aur.archlinux.org/packages/tide-island"><img alt="AUR package" src="https://img.shields.io/aur/version/tide-island?style=flat-square&label=AUR&color=8aadf4"></a>
  <img alt="Wayland" src="https://img.shields.io/badge/Wayland-111111?style=flat-square&color=7dc4e4">
  <img alt="OpenGL" src="https://img.shields.io/badge/OpenGL-111111?style=flat-square&color=ed8796">
</p>

## About this project

This is a new version of [Tide Island](https://github.com/enhaoswen/Tide-island), made using OpenGL (still under development).

### Why making a new version?

Because we pursue better performance, less resource usage, and a higher ceiling.

Compared with Quickshell, Tide Island uses a pure OpenGL backend, removing the heavyweight QML component system, JavaScript runtime, and Qt dependency stack. This version is perfect for geeks who are concerned about resource usage.

## Live RAM monitor

See the [RAM monitor documentation](tools/README.md) for usage, controls, and memory analysis details.

NVIDIA compiler file-page reclamation is disabled during normal operation. Its [manual API](tools/README.md#nvidia-compiler-file-pages) remains available for explicit use.

## Contributing

Issues, bug reports, design suggestions, and pull requests are all welcome.

- Only 1 topic per issue / PR.
- Tell your ideas first before making a PR.

AI is a tool, not a replacement for understanding. Using AI for small, well-defined modules is perfectly fine, but code should never be blindly generated and accepted without being understood and reviewed.

## Acknowledgements

Special thanks to the developers and contributors of these projects:

- [sokol](https://github.com/floooh/sokol) — for providing a lightweight graphics and cross-platform development library.
- [Wayland](https://wayland.freedesktop.org/) — for the display and windowing protocol.
- [Glaze](https://github.com/stephenberry/glaze) — for providing fast JSON serialization and deserialization.

<p align="center">
  <sub>
    Made for Wayland users who like quiet and practical desktops.
  </sub>
</p>
