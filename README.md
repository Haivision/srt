# Secure Reliable Transport (SRT) Protocol

[About SRT](#what-is-srt) | [Features](#features) | [Getting Started](#getting-started-with-srt) | [Build Instructions](#build-instructions) | [Sample Apps and Tools](#sample-applications-and-tools) | [Contribute](#contributing) | [License](#license) | [Releases](#release-history)

<p align="left">
  <a href="http://srtalliance.org/">
    <img alt="SRT" src="http://www.srtalliance.org/wp-content/uploads/SRT_text_hor_logo_grey.png" width="500"/>
  </a>
</p>

[![License: MPLv2.0][license-badge]](./LICENSE)
[![Latest release][release-badge]][github releases]
[![codecov][codecov-badge]][codecov-project]
[![Build Status Linux and macOS][travis-badge]][travis]
[![Build Status Windows][appveyor-badge]][appveyor]

[![Ubuntu 23.04][Ubuntu-badge]][Ubuntu-package]
[![Fedora 37][fedora-badge]][fedora-package]
[![Debian][debian-badge]][debian-package]
[![Homebrew][Homebrew-badge]][Homebrew-package]
[![Vcpkg][Vcpkg-badge]][Vcpkg-package]
[![ConanCenter][ConanCenter-badge]][ConanCenter-package]


## What is SRT?

**Secure Reliable Transport (SRT)** is a transport protocol for ultra low (sub-second) latency live video and audio streaming, as well as for generic bulk data transfer[^1]. SRT is available as an open-source technology with the code on GitHub, a published [Internet Draft](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01), and a growing [community of SRT users](https://www.srtalliance.org/).

SRT is applied to contribution and distribution endpoints as part of a video stream workflow to deliver the best quality and lowest latency video at all times.

|               |                                                   |
| ------------- | ------------------------------------------------- |
| **S**ecure    | Encrypts video streams                            |
| **R**eliable  | Recovers from severe packet loss                  |
| **T**ransport | Dynamically adapts to changing network conditions |

In live streaming configurations, the SRT protocol maintains a constant end-to-end latency. This allows the live stream's signal characteristics to be recreated on the receiver side, reducing the need for buffering. As packets are streamed from source to destination, SRT detects and adapts to real-time network conditions between the two endpoints. It helps compensate for jitter and bandwidth fluctuations due to congestion over noisy networks.

[SRT implements AES encryption](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-6) to protect the payload of the media streams, and offers various error recovery mechanisms for minimizing the packet loss that is typical of Internet connections, of which Automatic Repeat reQuest (ARQ) is the primary method. With ARQ, when a receiver detects that a packet is missing it sends an alert to the sender requesting retransmission of this missing packet. [Forward Error Correction (FEC)](./docs/features/packet-filtering-and-fec.md) and [Connection Bonding](./docs/features/bonding-quick-start.md), which adds seamless stream protection and hitless failover, are also supported by the protocol.

<p align="right"><em>To learn more about the protocol subscribe to the <a href="https://medium.com/innovation-labs-blog/tagged/secure-reliable-transport">Innovation Labs Blog</a> on &nbsp;<img alt="slack logo" src="https://upload.wikimedia.org/wikipedia/commons/thumb/0/0d/Medium_%28website%29_logo.svg/500px-Medium_%28website%29_logo.svg.png" width="80"></em></p>

<p align="right"><em>To ask a question <a href="https://slackin-srtalliance.azurewebsites.net">join the conversation</a> in the <b>#development</b> channel on &nbsp;<a href="https://srtalliance.slack.com"><img alt="slack logo" src="https://github.com/stevomatthews/srt/blob/master/docs/images/Slack_RGB2.svg" width="60"></a></em></p>

## Features

> :point_down: Click on the &#9658; button to expand a feature description.

<details>
  <summary>Pristine Quality and Reliability</summary>

  <p>

  No matter how unreliable your network, SRT can recover from severe packet loss and jitter, ensuring the integrity and quality of your video streams.

  </p>
</details>

<details>
  <summary>Low Latency</summary>

  <p>

  SRT’s stream error correction is configurable to accommodate a user’s deployment conditions. Leveraging real-time IP communications development to extend traditional network error recovery practices, SRT delivers media with significantly lower latency than TCP/IP, while offering the speed of UDP transmission with greatly improved reliability.

  </p>
</details>

<details>
  <summary>Content Agnostic</summary>

  <p>

  Unlike some other streaming protocols that only support specific video and audio formats, SRT is payload agnostic. Because SRT operates at the network transport level, acting as a wrapper around your content, it can transport any type of video format, codec, resolution, or frame rate.

  </p>
</details>

<details>
  <summary>Easy Firewall Traversal with Rendezvous Mode</summary>

  <p>

  The handshaking process used by SRT supports outbound connections without the potential risks and dangers of permanent exterior ports being opened in a firewall, thereby maintaining corporate LAN security policies and minimizing the need for IT intervention.

  </p>
</details>

<details>
  <summary><a href="https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-6">AES Encryption</a></summary>

  <p>

  Using 128/192/256-bit AES encryption trusted by governments and organizations around the world, SRT ensures that valuable content is protected end-to-end from contribution to distribution so that no unauthorized parties can listen.

  </p>
</details>

<details>
  <summary><a href="./docs/features/packet-filtering-and-fec.md">Forward Error Correction (FEC) and Packet Filter API</a></summary>

  <p>

  [SRT 1.4](https://github.com/Haivision/srt/releases/tag/v1.4.0) sees the introduction of the _packet filter API_. This mechanism allows custom processing to be performed on network packets on the sender side before they are sent, and on the receiver side once received from the network. The API allows users to write their own plugin, thereby extending the SRT protocol's capabilities even further with all kinds of different packet filtering. Users can manipulate the resulting packet filter data in any way, such as for custom encryption, packet inspection, or accessing data before it is sent.

  The first plugin created as an example of what can be achieved with the packet filter API is for Forward Error Correction (FEC) which, in certain use cases, can offer slightly lower latency than Automatic Repeat reQuest (ARQ). This plugin allows three different modes:

   - ARQ only – retransmits lost packets,
   - FEC only – provides the overhead needed for FEC recovery on the receiver side,
   - FEC and ARQ – retransmits lost packets that FEC fails to recover.

  </p>
</details>

<details>
  <summary><a href="./docs/features/bonding-quick-start.md">Connection Bonding</a></summary>

  <p>

  Similar to SMPTE-2022-7 over managed networks, Connection Bonding adds seamless stream protection and hitless failover to the SRT protocol. This technology relies on more than one IP network path to prevent disruption to live video streams in the event of network congestion or outages, maintaining continuity of service.

  This is accomplished using the [socket groups](./docs/features/socket-groups.md) introduced in [SRT v1.5](https://github.com/Haivision/srt/releases/tag/v1.5.0). The general concept of socket groups means having a group that contains multiple sockets, where one operation for sending one data signal is applied to the group. Single sockets inside the group will take over this operation and do what is necessary to deliver the signal to the receiver.

  Two modes are supported:

  - [Broadcast](./docs/features/socket-groups.md#1-broadcast) - In *Broadcast* mode, data is sent redundantly over all the member links in a group. If one of the links fails or experiences network jitter and/or packet loss, the missing data will be received over another link in the group. Redundant packets are simply discarded at the receiver side.

  - [Main/Backup](./docs/features/bonding-main-backup.md) - In *Main/Backup* mode, only one (main) link at a time is used for data transmission while other (backup) connections are on standby to ensure the transmission will continue if the main link fails. The goal of Main/Backup mode is to identify a potential link break before it happens, thus providing a time window within which to seamlessly switch to one of the backup links.

  </p>
</details>

<details>
  <summary><a href="./docs/features/access-control.md">Access Control (Stream ID)</a></summary>

  <p>

  Access Control enables the upstream application to assign a Stream ID to individual SRT streams. By using a unique Stream ID, either automatically generated or customized, the upstream application can send multiple SRT streams to a single IP address and UDP port. The Stream IDs can then be used by a receiver to identify and differentiate between ingest streams, apply user password access methods, and in some cases even apply automation based on the naming of the Stream ID. For example, contribution could be sent to a video production workflow and monitoring to a monitoring service.

  For broadcasters, Stream ID is key to replacing RTMP for ingesting video streams, especially HEVC/H.265 content, into cloud service or CDNs that have a single IP socket (address + port) open for incoming video.

  </p>
</details>

## Getting Started with SRT

|                                                                                                                               |                                                                                      |                                                                                   |
|:-----------------------------------------------------------------------------------------------------------------------------:|:------------------------------------------------------------------------------------:|:---------------------------------------------------------------------------------:|
| [The SRT API](./docs#srt-api-documents)                                                                                       | [IETF Internet Draft](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01) | [Sample Apps](./docs#sample-applications)                                         |
| Reference documentation for the SRT library API                                                                               | The SRT Protocol Internet Draft                                                      | Instructions for using test apps (`srt-live-transmit`, `srt-file-transmit`, etc.) |
| [SRT Technical Overview](https://github.com/Haivision/srt/files/2489142/SRT_Protocol_TechnicalOverview_DRAFT_2018-10-17.pdf)  | [SRT Deployment Guide](https://www.srtalliance.org/srt-deployment-guide/)            | [SRT CookBook](https://srtlab.github.io/srt-cookbook)                             |
| Early draft technical overview (precursor to the Internet Draft)                                                              | A comprehensive overview of the protocol with deployment guidelines                  | Development notes on the SRT protocol                                             |
| [Innovation Labs Blog](https://medium.com/innovation-labs-blog/tagged/secure-reliable-transport)                              | [SRTLab YouTube Channel](https://www.youtube.com/channel/UCr35JJ32jKKWIYymR1PTdpA)   | [Slack](https://srtalliance.slack.com)                                            |
| The blog on Medium with SRT-related technical articles                                                                        | Technical YouTube channel with useful videos                                         | Slack channels to get the latest updates and ask questions <br />[Join SRT Alliance on Slack](https://slackin-srtalliance.azurewebsites.net/) |

### Additional Documentation

- [Why SRT?](./docs/misc/why-srt-was-created.md) - A brief history and rationale for SRT by Marc Cymontkowski.
- [RTMP vs. SRT: Comparing Latency and Maximum Bandwidth](https://www.haivision.com/resources/white-paper/srt-versus-rtmp/) White Paper.
- [Documentation on GitHub](./docs#documentation-overview) with SRT API documents, features decsriptions, etc.
- The SRT Protocol Internet Draft: [Datatracker](https://datatracker.ietf.org/doc/draft-sharabayko-srt/) | [Latest Version](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01) | [Latest Working Copy](https://haivision.github.io/srt-rfc/draft-sharabayko-srt.html) | [GitHub Repo](https://github.com/Haivision/srt-rfc)

## Build Instructions

[Linux (Ubuntu/CentOS)](./docs/build/build-linux.md) | [Windows](./docs/build/build-win.md) | [macOS](./docs/build/build-macOS.md) | [iOS](./docs/build/build-iOS.md) | [Android](./docs/build/build-android.md) | [Package Managers](./docs/build/package-managers.md)

### Requirements

* C++03 or above compliant compiler.
* CMake 2.8.12 or above as a build system.
* OpenSSL 1.1 to enable encryption, otherwise build with [`-DENABLE_ENCRYPTION=OFF`](./docs/build/build-options.md#enable_encryption).
* Multithreading is provided by either of the following:
  * C++11: standard library (`std` by [`-DENABLE_STDCXX_SYNC=ON`](./docs/build/build-options.md#enable_stdcxx_sync) CMake option),
  * C++03: Pthreads (for POSIX systems it's built in, for Windows there is a ported library).
* Tcl 8.5 is optional and is used by `./configure` script. Otherwise, use CMake directly.

### Build Options

For detailed descriptions of the build system and options, please read the [SRT Build Options](./docs/build/build-options.md) document.

## Sample Applications and Tools

The current repo provides [sample applications](./apps) and [code examples](./examples) that demonstrate the usage of the SRT library API. Among them are [`srt-live-transmit`](./apps/srt-live-transmit.cpp), [`srt-file-transmit`](./apps/srt-file-transmit.cpp), and other applications. The respective documentation can be found [here](./docs#sample-applications). Note that all samples are provided for instructional purposes, and should not be used in a production environment.

The [`srt-xtransmit`](https://github.com/maxsharabayko/srt-xtransmit) utility is actively used for internal testing and performance evaluation. Among other features it supports dummy payload generation, traffic routings, and connection bonding. Additional details are available in the [`srt-xtransmit`](https://github.com/maxsharabayko/srt-xtransmit) repo itself.

Python tools that might be useful during development are:

- [`srt-stats-plotting`](https://github.com/mbakholdina/srt-stats-plotting) - A script designed to plot graphs based on SRT `.csv` statistics.
- [`lib-tcpdump-processing`](https://github.com/mbakholdina/lib-tcpdump-processing) - A library designed to process `.pcap(ng)` [tcpdump](https://www.tcpdump.org/) or [Wireshark](https://www.wireshark.org/) trace files and extract SRT packets of interest for further analysis.
- [`lib-srt-utils`](https://github.com/mbakholdina/lib-srt-utils) - A Python library containing supporting code for running SRT tests based on an experiment configuration.

## Contributing

Anyone is welcome to contribute. If you decide to get involved, please take a moment to review the guidelines:

* [SRT Developer's Guide](docs/dev/developers-guide.md)
* [Contributing](CONTRIBUTING.md)
* [Reporting Issues](docs/dev/making-srt-better.md)

For information on contributing to the [Internet Draft](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01) or to submit issues please go to the following [repo](https://github.com/Haivision/srt-rfc). The repo for contributing in [SRT CookBook](https://srtlab.github.io/srt-cookbook/) can be found [here](https://github.com/SRTLab/srt-cookbook/).

## License

By contributing code to the SRT project, you agree to license your contribution under the [MPLv2.0 License](LICENSE).

## Release History

- [Release notes](https://github.com/Haivision/srt/releases)
- [SRT versioning](./docs/dev/developers-guide.md#versioning)


[^1]: The term “live streaming” refers to continuous data transmission (MPEG-TS or equivalent) with latency management. Live streaming based on segmentation and transmission of files like in the HTTP Live Streaming (HLS) protocol (as described in RFC8216) is not part of this use case. File transmission in either message or buffer mode should be considered in this case. See [Section 7. Best Practices and Configuration Tips for Data Transmission via SRT](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-7) of the Internet Draft for details. Note that SRT is content agnostic, meaning that any type of data can be transmitted via its payload.


[appveyor-badge]: https://img.shields.io/appveyor/ci/Haivision/srt/master.svg?label=Windows
[appveyor]: https://ci.appveyor.com/project/Haivision/srt
[travis-badge]: https://img.shields.io/travis/Haivision/srt/master.svg?label=Linux/macOS
[travis]: https://travis-ci.org/Haivision/srt
[license-badge]: https://img.shields.io/badge/License-MPLv2.0-blue

[Vcpkg-package]: https://repology.org/project/srt/versions
[Vcpkg-badge]: https://repology.org/badge/version-for-repo/vcpkg/srt.svg

[ConanCenter-package]: https://repology.org/project/srt/versions
[ConanCenter-badge]: https://repology.org/badge/version-for-repo/conancenter/srt.svg

[codecov-project]: https://codecov.io/gh/haivision/srt
[codecov-badge]: https://codecov.io/gh/haivision/srt/branch/master/graph/badge.svg

[github releases]: https://github.com/Haivision/srt/releases
[release-badge]: https://img.shields.io/github/release/Haivision/srt.svg

[debian-badge]: https://badges.debian.net/badges/debian/testing/libsrt1.5-gnutls/version.svg
[debian-package]: https://packages.debian.org/testing/libs/libsrt1.5-gnutls

[fedora-package]: https://repology.org/project/srt/versions
[fedora-badge]: https://repology.org/badge/version-for-repo/fedora_37/srt.svg

[homebrew-package]: https://repology.org/project/srt/versions
[homebrew-badge]: https://repology.org/badge/version-for-repo/homebrew/srt.svg

[Ubuntu-package]: https://repology.org/project/srt/versions
[Ubuntu-badge]: https://repology.org/badge/version-for-repo/ubuntu_23_04/srt.svg
