# Documentation Overview

TODO:

- Table of contents
- Same columns width for the table
- gstreamer.md ?
- images
- links

## SRT API Documents

| Folder Name | File Name                                          | Description                                          | Refer as               |
| :---------- | -------------------------------------------------- | ---------------------------------------------------- | ---------------------- |
| [API](API/) | [API.md](API/API.md)                               | Detailed description of the SRT C API.               | SRT API                |
| [API](API/) | [API-functions.md](API/API-functions.md)           | Reference document for SRT API functions.            | SRT API Functions      |
| [API](API/) | [API-socket-options.md](API/API-socket-options.md) | Instructions and list of socket options for SRT API. | SRT API Socket Options |
| [API](API/) | [statistics.md](API/statistics.md)                 | How to use SRT socket and socket group statistics.   | SRT Statistics         |

## Build Instructions

| Folder Name     | File Name                                  | Description                                             | Refer as                 |
| :-------------- | ------------------------------------------ | ------------------------------------------------------- | ------------------------ |
| [build](build/) | [build-android.md](build/build-android.md) | SRT build instructions for Android.                     | Building SRT for Android |
| [build](build/) | [build-iOS.md](build/build-iOS.md)         | SRT build instructions for iOS.                         | Building SRT for iOS     |
| [build](build/) | [build-options.md](build/build-options.md) | Description of CMake build system and configure script. | SRT Build Options        |
| [build](build/) | [build-win.md](build/build-win.md)         | SRT build instructions for Windows.                     | Building SRT for Windows |
|                 |                                            |                                                         |                          |

## Development Documents

| Folder Name | File Name                                        | Description                                                  | Refer as              |
| :---------- | ------------------------------------------------ | ------------------------------------------------------------ | --------------------- |
| [dev](dev/) | [developers-guide.md](dev/developers-guide.md)   | Development setup, project structure, coding rules, submitting issues & PRs, etc. | SRT Developer's Guide |
| [dev](dev/) | [low-level-info.md](dev/low-level-info.md)       | Low level information for the SRT project (only mutex locking). | Low Level Info        |
| [dev](dev/) | [making-srt-better.md](dev/making-srt-better.md) | Guidelines for problem reporting, collecting debug logs and pcaps. | Making SRT Better     |
|             |                                                  |                                                              |                       |

## Features

| Folder Name           | File Name                   | Description                                                  | Refer as                                  |
| :-------------------- | --------------------------- | ------------------------------------------------------------ | ----------------------------------------- |
| [features](features/) | access-control.md           | Access Control (Stream ID) guidelines.                       | SRT Access Control (Stream ID) Guidelines |
| [features](features/) | bonding-intro.md            | Introduction to Connection Bonding. Description of group (bonded) connections. | SRT Connection Bonding                    |
| [features](features/) | encryption.md               | Description of SRT encryption mechanism. This document might be outdated, please consult [Section 6. Encryption](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00#section-6) of the [SRT RFC](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00) additionally. | SRT Encryption                            |
| [features](features/) | handshake.md                | Description of SRT handshake mechanism. This document might be outdated, please consult [Section 3.2.1 Handshake](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00#section-3.2.1) and [Section 4.3 Handshake Messages](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00#section-4.3) of the [SRT RFC](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00) additionally. | SRT Handshake                             |
| [features](features/) | live-streaming.md           | Guidelines for live streaming with SRT. See also best practices and configuration tips in [Section 7.1 Live Streaming](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00#section-7.1) of the [SRT RFC](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00). | Live Streaming with SRT - Guidelines      |
| [features](features/) | packet-filtering-and-fec.md | Description of SRT packet filtering mechanism, including FEC. | SRT Packet Filtering & FEC                |
| [features](features/) | socket-groups.md            | Description of socket groups in SRT (Connection Bonding).    | SRT Socket Groups                         |

## Sample Applications

| Folder Name   | File Name            | Description                                                 | Refer as            |
| :------------ | -------------------- | ----------------------------------------------------------- | ------------------- |
| [apps](apps/) | srt-live-transmit.md | How to use the universal data transport tool.               | `srt-live-transmit` |
| [apps](apps/) | srt-multiplex.md     | Description of sample program for sending multiple streams. | `srt-multiplex`     |
| [apps](apps/) | srt-tunnel.md        | How to use the tunnelling application.                      | `srt-tunnel`        |

# Miscellaneous

| Folder Name   | File Name                                             | Description                                                  | Refer as            |
| :------------ | ----------------------------------------------------- | ------------------------------------------------------------ | ------------------- |
| [misc](misc/) | [why-srt-was-created.md](misc/why-srt-was-created.md) | Background and history of SRT. See also [Section 1. Introduction](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00#section-1) of the [SRT RFC](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00). | Why SRT Was Created |
|               |                                                       |                                                              |                     |

TODO:

- [ ] There are three documents with contribution guidelines for the project: CONTRIBUTING.md, developers-guide.md and reporting.md -> Consider reducing the scope to Developer's Guide and CONTRIBUTING.md only.