# Documentation Overview

## SRT API Documents

| Folder Name | File Name                                          | Description                                          | Refer as               |
| :---------: | -------------------------------------------------- | ---------------------------------------------------- | ---------------------- |
|     API     | [API.md](API/API.md)                               | Detailed description of the SRT C API.               | SRT API                |
|     API     | [API-functions.md](API/API-functions.md)           | Reference document for SRT API functions.            | SRT API Functions      |
|     API     | [API-socket-options.md](API/API-socket-options.md) | Instructions and list of socket options for SRT API. | SRT API Socket Options |
|     API     | [statistics.md](API/statistics.md)                 | How to use SRT socket and socket group statistics.   | SRT Statistics         |

## Build Instructions

| Folder Name                   | File Name                                                    | Description                                            | Refer as               |
| :---------------------------- | ------------------------------------------------------------ | ------------------------------------------------------ | ---------------------- |
| build-instructions or build ? | build-options.md                                             | Description of CMake build system and configure script | SRT Build Options      |
|                               | build-win.md or build-instructions-win.md or building-srt-for-win? | SRT build instructions for Windows                     |                        |
|                               | build_iOS.md -> new name                                     | SRT build instructions for iOS                         | Building SRT for iOS ? |
|                               | [Android](https://github.com/Haivision/srt/tree/master/docs/Android)/**Compiling.md** ???<br />The `docs/Android` folder contains not only documentation but also build scripts. They need to be moved to the `scripts` folder. |                                                        |                        |

## Development Documents

| Folder Name | File Name                           | Description                                                  | Refer as              |
| :---------- | ----------------------------------- | ------------------------------------------------------------ | --------------------- |
| DEV ??      | developers-guide.md                 | TODO: Project setup, rules, submitting issues & PRs (build options?)<br />!!! The main document we should refer to from readme - contributing (add a link in readme) | SRT Developer's Guide |
|             | low-level-info.md                   | Low level information for the SRT project (only mutex locking).<br />?? Move to devbook |                       |
|             | reporting.md -> making-set-better ? | Guidelines for problem reporting, collecting debug logs and pcaps |                       |
|             |                                     |                                                              |                       |

## Features

| Folder Name           | File Name                   | Description                                                  | Refer as                                  |
| :-------------------- | --------------------------- | ------------------------------------------------------------ | ----------------------------------------- |
| [features](features/) | access-control.md           | Access Control (Stream ID) guidelines .                      | SRT Access Control (Stream ID) Guidelines |
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

## Other Documents

| Folder Name | File Name              | Description                                                  | Refer as |
| :---------- | ---------------------- | ------------------------------------------------------------ | -------- |
| other       | why-srt-was-created.md | Background and history of SRT. See also [Section 1. Introduction](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00#section-1) of the [SRT RFC](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00). |          |
|             |                        |                                                              |          |

TODO:

- [ ] There are three documents with contribution guidelines for the project: CONTRIBUTING.md, developers-guide.md and reporting.md -> Consider reducing the scope to Developer's Guide and CONTRIBUTING.md only.