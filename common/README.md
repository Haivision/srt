Common files
============

This directory is meant to hold source files that may be reusable components.

Initially they were thought of as common for multiple applications.

The UriParser class is universal enough to parse and interpret URI.
It's important for SRT to specify not only the medium address, but
also some extra parameters.

The SocketOption class is originally part of stransmit, but it was extracted
so that you can do the same in your application. It's useful for having
a text-specified option with value that you'd set to a socket, which is
the form that has been extracted from the URI by UriParser. It's also
responsible for a standard way of extracting the caller/listener/rendezvous
mode.

These things are not meant for the public API yet, but they are extracted
the way that may allow you to use it in your application.


Compat
======

This part contains some portability problem resolutions, including:
 - `clock_gettime`, a function that is unavailable on Mac
 - `strerror` in a version that is both portable and thread safe

