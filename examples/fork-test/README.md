The `srt_server` and `srt_client` apps should be compiled using
external installation of SRT (e.g. in the local directory), each
one as a single program. This is not compiled as a part of SRT.

If you want to use a local installation, simply set `PKG_CONFIG_PATH`
environment variable to point to the local installation directory with
"lib/pkgconfig" or "lib64/pkgconfig" suffix.
