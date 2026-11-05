Tools
=====

This directory contains applications used for testing and development only.

They may contain experimental versions or not fully functioning features.

Every application has its own individual Manifest file (`*.maf`), which defines
of which source files particular application comprises. They may be contained
either in the same directory, or in any other subproject.


Tools applications:
===================


* srt-test-file: File transmission testing tool. Corresponds to
[../docs/apps/srt-file-transmit.md](`srt-file-transmit`) in the apps.

* srt-test-live: Live transmission testing tool. Corresponds to
[../docs/apps/srt-live-transmit.md](`srt-live-transmit`) in the apps.

* srt-test-mpbond: Testing tool for live transmission with redundancy.
Allows to set up a service on multiple listeners in order to test
the functionality of multiple listeners accepting connections for
the same group

* srt-test-multiplex: The application that allows to send multiple
streams at once over one SRT connection (see
[../docs/apps/srt-test-multiplex.md](Documentation)).

* srt-test-relay: The bidirectional sending testing application.
For the specified SRT connection there is used both input and
output (see [../docs/apps/srt-test-relay.md](Documentation)).

* srt-test-stow: The BSTOW reader testing application. See the
[bstow.md](BSTOW documentation) for more details.
