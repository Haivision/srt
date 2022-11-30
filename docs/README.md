# Documentation Overview

## SRT API Documents

| Document Title                                              | Folder                        | File Name                                                      | Description                                          |
| :---------------------------------------------------------- | :---------------------------- | :------------------------------------------------------------- | :--------------------------------------------------- |
| [SRT API](API/API.md)                                       | [API](API/)                   | [API.md](API/API.md)                                           | Detailed description of the SRT C API.               |
| [SRT API Functions](API/API-functions.md)                   | [API](API/)                   | [API-functions.md](API/API-functions.md)                       | Reference document for SRT API functions.            |
| [SRT API Socket Options](API/API-socket-options.md)         | [API](API/)                   | [API-socket-options.md](API/API-socket-options.md)             | Instructions and list of socket options for SRT API. |
| [SRT Statistics](API/statistics.md)                         | [API](API/)                   | [statistics.md](API/statistics.md)                             | How to use SRT socket and socket group statistics.   |
| [Configuration Guidelines](API/configuration-guidelines.md) | [API](API/)                   | [configuration-guidelines.md](API/configuration-guidelines.md) | How to configure SRT buffers.                        |
| <img width=200px height=1px/>                               | <img width=100px height=1px/> | <img width=200px height=1px/>                                  | <img width=500px height=1px/>                        |

## Build Instructions

| Document Title                                                | Folder                        | File Name                                        | Description                                                             |
| :------------------------------------------------------------ | :---------------------------- | :----------------------------------------------- | :---------------------------------------------------------------------- |
| [SRT Build Options](build/build-options.md)                   | [build](build/)               | [build-options.md](build/build-options.md)       | Description of CMake build system, configure script, and build options. |
| [Building SRT on Linux (Ubuntu/CentOS)](build/build-linux.md) | [build](build/)               | [build-linux.md](build/build-linux.md)           | Build instructions for Linux (Ubuntu/CentOS).                           |
| [Building SRT on Windows](build/build-win.md)                 | [build](build/)               | [build-win.md](build/build-win.md)               | Build instructions for Windows.                                         |
| [Building SRT on macOS](build/build-macOS.md)                 | [build](build/)               | [build-macOS.md](build/build-macOS.md)           | Build instructions for macOS.                                           |
| [Building SRT on iOS](build/build-iOS.md)                     | [build](build/)               | [build-iOS.md](build/build-iOS.md)               | Build instructions for iOS.                                             |
| [Building SRT on Android](build/build-android.md)             | [build](build/)               | [build-android.md](build/build-android.md)       | Build instructions for Android.                                         |
| [Package Managers](build/package-managers.md)                 | [build](build/)               | [package-managers.md](build/package-managers.md) | Package managers supporting SRT library.                                |
| <img width=200px height=1px/>                                 | <img width=100px height=1px/> | <img width=200px height=1px/>                    | <img width=500px height=1px/>                                           |

## Development Documents

| Document Title                                   | Folder                        | File Name                                        | Description                                                                            |
| :----------------------------------------------- | :---------------------------- | :----------------------------------------------- | :------------------------------------------------------------------------------------- |
| [SRT Developer's Guide](dev/developers-guide.md) | [dev](dev/)                   | [developers-guide.md](dev/developers-guide.md)   | Development setup, project structure, coding rules,<br />submitting issues & PRs, etc. |
| [Low Level Info](dev/low-level-info.md)          | [dev](dev/)                   | [low-level-info.md](dev/low-level-info.md)       | Low level information for the SRT project (only<br />mutex locking).                   |
| [Making SRT Better](dev/making-srt-better.md)    | [dev](dev/)                   | [making-srt-better.md](dev/making-srt-better.md) | Guidelines for problem reporting, collecting debug logs<br />and pcaps.                |
| <img width=200px height=1px/>                    | <img width=100px height=1px/> | <img width=200px height=1px/>                    | <img width=500px height=1px/>                                                          |

## Features

| Document Title                                               |            Folder             | File Name                                                    | Description                                                  |
| :----------------------------------------------------------- | :---------------------------- | :----------------------------------------------------------- | :----------------------------------------------------------- |
| [SRT Access Control<br /> (Stream ID) Guidelines](features/access-control.md) |     [features](features/)     | [access-control.md](features/access-control.md)              | Access Control (Stream ID) guidelines.                       |
| [SRT Connection Bonding: Quick Start](features/bonding-quick-start.md) | [features](features/) | [bonding-quick-start.md](features/bonding-quick-start.md) | SRT Connection Bonding Quick Start. |
| [SRT Connection Bonding: Introduction](features/bonding-intro.md)          |     [features](features/)     | [bonding-intro.md](features/bonding-intro.md)                | Introduction to Connection Bonding. Description<br />of group (bonded) connections. |
| [SRT Connection Bonding: Socket Groups](features/socket-groups.md)               |     [features](features/)     | [socket-groups.md](features/socket-groups.md)                | Description of socket groups in SRT (Connection<br />Bonding). Here you will also find information<br />regarding the `srt-test-live` application for testing<br />Connection Bonding. |
| [SRT Connection Bonding: Main/Backup][main-backup]           |     [features](features/)     | [bonding-main-backup.md][main-backup]                        | Main/Backup mode description.                          |
| [SRT Encryption](features/encryption.md)                     |     [features](features/)     | [encryption.md](features/encryption.md)                      | Description of SRT encryption mechanism. This<br />document might be outdated, please consult<br />[Section 6. Encryption][srt-internet-draft-sec-6] of the [Internet Draft][srt-internet-draft] additionally. |
| [SRT Handshake](features/handshake.md)                       |     [features](features/)     | [handshake.md](features/handshake.md)                        | Description of SRT handshake mechanism. This<br />document might be outdated, please consult<br />[Section 3.2.1 Handshake][srt-internet-draft-sec-3-2-1] and<br />[Section 4.3 Handshake Messages](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-4.3) of the<br />[Internet Draft][srt-internet-draft] additionally. |
| [Live Streaming <br /> Guidelines](features/live-streaming.md) |   [features](features/)     | [live-streaming.md](features/live-streaming.md)              | Guidelines for live streaming with SRT. See also<br />best practices and configuration tips in<br />[Section 7.1 Live Streaming](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-7.1) of the [Internet Draft][srt-internet-draft]. |
| [SRT Packet <br /> Filtering & FEC][packet-filter]           |     [features](features/)     | [packet-filtering-and-fec.md][packet-filter]                 | Description of SRT packet filtering mechanism,<br />including FEC. |
| <img width=200px height=1px/>                                | <img width=100px height=1px/> | <img width=200px height=1px/>                                | <img width=500px height=1px/>                                |

## Sample Applications

| Document Title                                                         |            Folder     | File Name                                           | Description                                                   |
| :--------------------------------------------------------------------- | :-------------------- | :-------------------------------------------------- | :------------------------------------------------------------ |
| [Using the<br /> `srt-live-transmit` App](apps/srt-live-transmit.md)   | [apps](apps/)         | [srt-live-transmit.md](apps/srt-live-transmit.md)   | A sample application to transmit a live stream from<br />source medium (UDP/SRT/`stdin`) to the target medium<br />(UDP/SRT/`stdout`). |
| [Using the<br /> `srt-file-transmit` App](apps/srt-file-transmit.md)   | [apps](apps/)         | [srt-file-transmit.md](apps/srt-file-transmit.md)   | A sample application to transmit a file over SRT              |
| [Using the<br /> `srt-tunnel` App](apps/srt-tunnel.md)                 | [apps](apps/)         | [srt-tunnel.md](apps/srt-tunnel.md)                 | A sample application to set up an SRT tunnel for TCP traffic. |
| [Using the<br /> `srt-test-multiplex` App](apps/srt-test-multiplex.md) | [apps](apps/)         | [srt-test-multiplex.md](apps/srt-test-multiplex.md) | Testing application that allows to send multiple streams over one UDP link. |
| [Using the<br /> `srt-test-relay` App](apps/srt-test-relay.md)         | [apps](apps/)         | [srt-test-relay.md](apps/srt-test-relay.md)         | Testing application for bidirectional stream sending over one connection.   |
| <img width=200px height=1px/>                                  | <img width=100px height=1px/> | <img width=200px height=1px/>                       | <img width=500px height=1px/>                                      |

## Miscellaneous

| Document Title                                     |            Folder             | File Name                                             | Description                                                  |
| :------------------------------------------------- | :---------------------------- | :---------------------------------------------------- | :----------------------------------------------------------- |
| [Why SRT Was Created](misc/why-srt-was-created.md) |         [misc](misc/)         | [why-srt-was-created.md](misc/why-srt-was-created.md) | Background and history of SRT. See also<br />[Section 1. Introduction][srt-internet-draft-sec-1] of the [Internet Draft][srt-internet-draft]. |
| <img width=200px height=1px/>                      | <img width=100px height=1px/> | <img width=200px height=1px/>                         | <img width=500px height=1px/>                                |


[srt-internet-draft]: https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01
[srt-internet-draft-sec-1]: https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-1
[srt-internet-draft-sec-3-2-1]: https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-3.2.1
[srt-internet-draft-sec-6]: https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-6

[main-backup]: features/bonding-main-backup.md
[packet-filter]: features/packet-filtering-and-fec.md
