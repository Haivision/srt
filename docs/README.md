# Documentation Overview

<!-- - [SRT API Documents](#srt-api-documents)
- [Build Instructions](#build-instructions)
- [Development Documents](#development-documents)
- [Features](#features)
- [Sample Applications](#sample-applications)
- [Miscellaneous](#miscellaneous) -->

## SRT API Documents

|            Folder             | File Name                                          | Description                                          | Refer as                      |
| :---------------------------: | -------------------------------------------------- | ---------------------------------------------------- | ----------------------------- |
|          [API](API/)          | [API.md](API/API.md)                               | Detailed description of the SRT C API.               | SRT API                       |
|          [API](API/)          | [API-functions.md](API/API-functions.md)           | Reference document for SRT API functions.            | SRT API Functions             |
|          [API](API/)          | [API-socket-options.md](API/API-socket-options.md) | Instructions and list of socket options for SRT API. | SRT API Socket Options        |
|          [API](API/)          | [statistics.md](API/statistics.md)                 | How to use SRT socket and socket group statistics.   | SRT Statistics                |
| <img width=100px height=1px/> | <img width=200px height=1px/>                      | <img width=500px height=1px/>                        | <img width=200px height=1px/> |

## Build Instructions

|            Folder             | File Name                                  | Description                                             | Refer as                      |
| :---------------------------: | ------------------------------------------ | ------------------------------------------------------- | ----------------------------- |
|        [build](build/)        | [build-android.md](build/build-android.md) | SRT build instructions for Android.                     | Building SRT for Android      |
|        [build](build/)        | [build-iOS.md](build/build-iOS.md)         | SRT build instructions for iOS.                         | Building SRT for iOS          |
|        [build](build/)        | [build-options.md](build/build-options.md) | Description of CMake build system and configure script. | SRT Build Options             |
|        [build](build/)        | [build-win.md](build/build-win.md)         | SRT build instructions for Windows.                     | Building SRT for Windows      |
| <img width=100px height=1px/> | <img width=200px height=1px/>              | <img width=500px height=1px/>                           | <img width=200px height=1px/> |

## Development Documents

|            Folder             | File Name                                        | Description                                                  | Refer as                      |
| :---------------------------: | ------------------------------------------------ | ------------------------------------------------------------ | ----------------------------- |
|          [dev](dev/)          | [developers-guide.md](dev/developers-guide.md)   | Development setup, project structure, coding rules,<br />submitting issues & PRs, etc. | SRT Developer's Guide         |
|          [dev](dev/)          | [low-level-info.md](dev/low-level-info.md)       | Low level information for the SRT project (only<br />mutex locking). | Low Level Info                |
|          [dev](dev/)          | [making-srt-better.md](dev/making-srt-better.md) | Guidelines for problem reporting, collecting debug logs<br />and pcaps. | Making SRT Better             |
| <img width=100px height=1px/> | <img width=200px height=1px/>                    | <img width=500px height=1px/>                                | <img width=200px height=1px/> |

## Features

|            Folder             | File Name                                                    | Description                                                  | Refer as                                       |
| :---------------------------: | ------------------------------------------------------------ | ------------------------------------------------------------ | ---------------------------------------------- |
|     [features](features/)     | [access-control.md](features/access-control.md)              | Access Control (Stream ID) guidelines.                       | SRT Access Control<br />(Stream ID) Guidelines |
|     [features](features/)     | [bonding-intro.md](features/bonding-intro.md)                | Introduction to Connection Bonding. Description<br />of group (bonded) connections. | SRT Connection Bonding                         |
|     [features](features/)     | [encryption.md](features/encryption.md)                      | Description of SRT encryption mechanism. This<br />document might be outdated, please consult<br />[Section 6. Encryption](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00#section-6) of the [SRT RFC](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00) additionally. | SRT Encryption                                 |
|     [features](features/)     | [handshake.md](features/handshake.md)                        | Description of SRT handshake mechanism. This<br />document might be outdated, please consult<br />[Section 3.2.1 Handshake](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00#section-3.2.1) and<br />[Section 4.3 Handshake Messages](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00#section-4.3) of the<br />[SRT RFC](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00) additionally. | SRT Handshake                                  |
|     [features](features/)     | [live-streaming.md](features/live-streaming.md)              | Guidelines for live streaming with SRT. See also<br />best practices and configuration tips in<br />[Section 7.1 Live Streaming](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00#section-7.1) of the [SRT RFC](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00). | Live Streaming<br />Guidelines                 |
|     [features](features/)     | [packet-filtering-and-fec.md](features/packet-filtering-and-fec.md) | Description of SRT packet filtering mechanism,<br />including FEC. | SRT Packet<br />Filtering & FEC                |
|     [features](features/)     | [socket-groups.md](features/socket-groups.md)                | Description of socket groups in SRT<br />(Connection Bonding). | SRT Socket Groups                              |
| <img width=100px height=1px/> | <img width=200px height=1px/>                                | <img width=500px height=1px/>                                | <img width=200px height=1px/>                  |

## Sample Applications

|            Folder             | File Name                                         | Description                                                 | Refer as                               |
| :---------------------------: | ------------------------------------------------- | ----------------------------------------------------------- | -------------------------------------- |
|         [apps](apps/)         | [srt-live-transmit.md](apps/srt-live-transmit.md) | How to use the universal data transport tool.               | Using the<br />`srt-live-transmit` App |
|         [apps](apps/)         | [srt-multiplex.md](apps/srt-multiplex.md)         | Description of sample program for sending multiple streams. | Using the<br />`srt-multiplex` App     |
|         [apps](apps/)         | [srt-tunnel.md](apps/srt-tunnel.md)               | How to use the tunnelling application.                      | Using the<br />`srt-tunnel` App        |
| <img width=100px height=1px/> | <img width=200px height=1px/>                     | <img width=500px height=1px/>                               | <img width=200px height=1px/>          |

## Miscellaneous

|            Folder             | File Name                                             | Description                                                  | Refer as                      |
| :---------------------------: | ----------------------------------------------------- | ------------------------------------------------------------ | ----------------------------- |
|         [misc](misc/)         | [why-srt-was-created.md](misc/why-srt-was-created.md) | Background and history of SRT. See also<br />[Section 1. Introduction](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00#section-1) of the [SRT RFC](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00). | Why SRT Was Created           |
| <img width=100px height=1px/> | <img width=200px height=1px/>                         | <img width=500px height=1px/>                                | <img width=200px height=1px/> |
