# Throned UI preview

This is a dependency-free Qt Widgets preview for the post-1.1 redesign. It deliberately does not open the database, start a core, alter proxy settings, or create a TUN interface.

Build it against Qt 6.5 or newer:

```sh
cmake -S tools/ui-demo -B build-ui
cmake --build build-ui
```

Useful arguments:

```text
--screen main|routes
--mode simple|advanced
--locale en|ru
--theme midnight|graphite|ocean|violet|ember
--scale 1.0|1.25|1.5|2.0
--output screenshot.png
--width 1000 --height 700
```

When `--output` is supplied, the preview renders once, saves the client area as a PNG, and exits. The GitHub UI preview workflow uses this mode to publish deterministic review screenshots and a runnable Windows bundle.
