# Introduction
This document describes an encryption mechanism that protects the payload of SRT streams. Despite using standard cryptographic algorithms, the mechanism is unique and does not interoperate with any known third party stream encryption method.

### Terminology
| Term | Description |
|------|-------------|
| AEAD | Authenticated Encryption with Associated Data |
| AES | Advanced Encryption Standard |
| AESkw | AES key wrap not specified ([RFC3394] or [ANSX9.102]) |
| AESKW | AES Key Wrap with associated data authentication [ANSX9.102] |
| ARM | Advanced RISC Machine (Texas Instrument processor) |
| CCM | Counter with CBC-MAC |
| CTR | Counter |
| DSP | Digital Signal Processor |
| DVB | Digital Video Broadcast |
| DVB-CA | DVB - Conditional Access |
| ECB | Electronic Code Book |
| ECM | Entitlement Control Message (DVB/MPEG) |
| EKT | Encrypted Key Transport (SRTP) |
| eSEK | Even SEK |
| FIPS | Federal Information Processing Standard |
| GCM | Galois/Counter mode  |
| GDOI | Group Domain Of Interpretation |
| GOP | Group Of Pictures |
| HDCP | High-bandwidth Digital Content Protection |
| HMAC | Hash-based Message Authentication Code |
| HTTP | Hypertext Transfer Protocol |
| HTTPS | HTTP Secure |
| IV | Initialisation Vector |
| KEK | Key Encrypting Key |
| LSB | Least Significant Bits |
| MAC | Message Authentication Code |
| MD5 | Message Digest 5 |
| MIKEY | Multimedia Internet KEYing |
| MPEG | Motion Picture Expert Group |
| MSB | Most Significant Bits |
| oSEK | Odd SEK |
| PBKDF2 | Password-Based Key Derivation Function version 2 |
| PES | Packetized Elementary Stream (MPEG) |
| PKCS | Public-Key Cryptography Standards |
| PRNG | Pseudo Random Number Generator |
| RISC | Reduced Instruction Set Computer |
| RTP | Read-time Transport Protocol |
| SEK | Stream Encrypting Key |
| SHA | Secure Hash Algorithm |
| SIV | Synthetic Initialisation Vector |
| SO | Security Officer |
| SRT | Secure Reliable Transport |
| SRTP | Secure Real-time Transport Protocol |
| SSL | Secure Socket Layer |
| TP | Transmission Payload |
| TS | Transport Stream (MPEG) |
| TU | Transmission Unit |
| UDP | User Datagram Protocol |

### References
* [ANSX9.102] Accredited Standards Committee, Wrapping of Keys and Associated Data, ANS X9.102, not for free document.
* [FIPS 140-2] Security Requirements for Cryptographic Modules, NIST, [FIPS PUB 140-2](https://csrc.nist.gov/csrc/media/publications/fips/140/2/final/documents/fips1402.pdf), May 2001.
* [FIPS 140-3] Security Requirements for Cryptographic Modules, NIST, [FIPS PUB 140-3](https://csrc.nist.gov/publications/detail/fips/140/3/archive/2009-12-11), December 2009.
* [SP800-38A] Recommendation for Block Cipher Modes of Operation, M. Dworkin, NIST, [FP800-38A](https://csrc.nist.gov/publications/detail/sp/800-38a/final), December 2001.
* [HDCP2] High-bandwidth Digital Content Protection System, Interface Independent Adaptation, Revision 2.0, [HDCP IIA 2.0](https://www.digital-cp.com/files/static_page_files/2C1C0F30-0E09-E813-BFAB6BAAE8A76080/HDCP%20Interface%20Independent%20Adaptation%20Specification%20Rev2_0.pdf), Digital Content Protection, LLC, October 2008.
* [PKCS5] [PKCS #5 v2.0 Password-Based Cryptography Standard](http://www.rsa.com/rsalabs/node.asp?id=2127), RSA Laboratories, March 1999.
* [RFC2998] PKCS #5: Password-Based Cryptography Specification Version 2.0, B. Kaliski, [RFC2898](https://tools.ietf.org/html/rfc2898), September 2000.
* [RFC3394] Advanced Encryption Standard (AES) Key Wrap Algorithm, J. Schaad, R. Housley, [RFC3394](https://tools.ietf.org/html/rfc3394), September 2002.
* [RFC3547] The Group Domain of Interpretation, M. Baugher, B. Weis, T. Hardjono, H. Harney, [RFC3547](https://tools.ietf.org/html/rfc3547), July 2003.
* [RFC3610] Counter with CBC-MAC (CCM), D. Whiting, R. Housley, N. Ferguson, [RFC3610](https://tools.ietf.org/html/rfc3610), September 2003.
* [RFC3711] The Secure Real-time Transport Protocol (SRTP), M. Baugher, D. McGrew, M. Naslund, E. Carrara, K. Norrman, [RFC3711](https://tools.ietf.org/html/rfc3711), March 2004.
* [RFC3830] MIKEY: Multimedia Internet KEYing, J, Arkko, E. Carrara, F. Lindholm, M. Naslund, K. Norrman, [RFC3830](https://tools.ietf.org/html/rfc3830), August 2004.
* [RFC5297] Synthetic Initialization Vector (SIV) Authenticated Encryption Using the Advanced Encryption Standard (AES), D. Harkins, [RFC5297](https://tools.ietf.org/html/rfc5297), October 2008.
* [RFC5649] Advanced Encryption Standard (AES) Key Wrap with Padding Algorithm, R. Housley, M. Dworkin, [RFC5649](https://tools.ietf.org/html/rfc5649), August 2009.
* [RFC6070] PBKDF2 Test Vectors
* [SRTP-EKT] Encrypted Key Transport for Secure RTP, D. McGrew, F. Andreasen, D. Wing, K. Fisher, [draft-ietf-avt-srtp-ekt-02](https://tools.ietf.org/html/draft-ietf-avt-srtp-ekt-02), March 2011.

### Operators
| Operator | Setting |
|----------|---------|
| a \|\| b | Concatenation |
| a XOR b | Bit-wise exclusive or |
| a ^ b | a exponent b |
| a<sub>n</sub> | a is n bits long |
| AESkw(kek,k) | AES key wrap k with kek (Key Encrypting Key) |
| LSB(n,v) | Least significant n bits of v |
| MSB(n,v) | Most significant n bits of v |
| PRNG(n) | Pseudo Random Number Generator (n bits) |
| PBKDF2(p,s,i,l) | Password-based Key Derivation Function (PKCS #5)<br> p: password, s: salt, i: iterations, l: key length |

# Overview
AES in counter mode (AES-CTR) is used with a short lived key to encrypt the media stream. This cipher is suitable for random access of a continuous stream, content protection (used by HDCP 2.0), and strong confidentiality when the counter is managed properly.

The short lived key is randomly generated by the sender and transmitted within the stream (KM Tx Period), wrapped with another longer-term key, the Key Encrypting Key (KEK). For connection-oriented transport such as SRT, there is no need to periodically transmit the short lived key since no party can join the stream at any time.

The KEK is derived from a secret shared between the sender and the receiver. The shared secret provides access to the stream key which provides access to the protected media stream. The distribution and management of the secret is more flexible than the stream encrypting key.

A pre-shared password used with a password-based key derivation mechanism is proposed in this document as the default shared secret but other automated key distribution methods that scale better could be proposed in a separate document.

The short lived key, hereafter called the Stream Encrypting Key (SEK), is regenerated for cryptographic reasons when enough packets have been encrypted with it (KM Refresh Rate). To ensure seamless rekeying, the next key to use is transmitted in advance to receivers (KM Pre-Announce) so they can switch keys without disruption when rekeying occurs. KM Refresh Rate and KM Pre-Announce are system parameters that can be configurable options if shorter time than the cryptographic limit is required (for example to limit the material obtained from a compromised SEK).

## Definitions
This section defines the elements of the SRT encryption mechanism. Figure 1 shows the encryption of arbitrary SRT payload.

![Figure 1][figure1] Figure 1

### Ciphers (AES-CTR)
The payload is encrypted with a cipher in counter mode (AES-CTR). The counter mode is one of the only cipher mode suitable for continuous stream encryption that permits decryption from any point, without access to start of the stream (random access), and for the same reason tolerates packet lost. The Electronic Code Book (ECB) mode also has these characteristics but does not provide serious confidentiality and is not recommended in cryptography.

### Media Stream message (MSmsg)
The Media Stream message is formed from the SRT media stream (data) packets with some elements of the SRT header used for the cryptography. SRT header already carries a 32-bit packet sequence that is used for the cipher’s counter (ctr) and 2 bits are stolen from the header’s message number (then reduced to 27-bits) for the encryption key (odd/even) indicator.

### Keying Material
For each stream, the sender generates a Stream Encrypting Key (SEK) and a Salt (not shown in Figure 1). For the initial implementation and for most envisioned scenarios where no separate authentication algorithm is used for message integrity, the SEK is used directly to encrypt the media stream. The Initial Vector (IV) for the counter is derived from the Salt only. In other scenarios, the SEK can be used along with the Salt as a key generating material to produce distinct encryption, authentication, and salt keys.

### Stream Encrypting Key (SEK)
The Stream Encrypting Key (SEK) is pseudo-random and different for each stream. It must be 128, 192, or 256 bits long for the AES-CTR ciphers. It is non-persistent and relatively short lived. In a typical scenario the SEK is expected to last, cryptographically, around 37 days for a 31-bit counter (2<sup>31</sup> packets / 667 packets/second).

The SEK is regenerated every time a stream starts. It must be discarded before 2<sup>31</sup> packets are encrypted (31-bit packet index) and replaced seamlessly using an odd/even key mechanism described further. SRT is conservative and regenerates the SEK key every 2<sup>25</sup> packets (~6 hours in the above scenario of a 667 packets per second stream). Reusing an IV (often called nonce) with the same key on different clear text is a known catastrophic issue of counter mode ciphers. By regenerating the SEK each time a stream starts we remove the need for fancier management of the IV to ensure uniqueness.

### Initialization Vector (IV)
The IV (also named nonce in the AES-CTR context) is a 112 bit random number. For the initial implementation and for most envisioned scenarios where no separate authentication algorithm is used for message integrity (Auth=0), the IV is derived from the salt only.

IV = MSB(112, Salt)	; Most significant 112 bits of the salt.

### Counter (ctr)
The counter for AES-CTR is the size of the cipher’s block, i.e. 128 bits. It is made of a block counter in the least significant 16 bits, counting blocks of a packet, and a 32 bits packet index in the next 32 bits. The upper 112 bits are XORed with the IV to produce a unique counter for each crypto block.

![Figure 2][figure2] Figure 2

The block counter (bctr) is incremented for each cipher block while producing the key stream. The packet index is incremented for each packet submitted to the cipher. The IV is derived from the Salt provided with the Keying Material.

### Keying Material message (KMmsg)
The SEK and a Salt are transported in-stream, in a Keying Material message (KMmsg), implemented as a SRT custom control packet, wrapped with a longer term Key Encrypting Key (KEK) using AES key wrap [RFC3394]. There are possibilities for an eventual key wrapper with integrity such as AESKW [ANSX9.102] or AES-SIV [RFC5297].

Transmitting a key in-band is not original to this specification. It is used in DVB MPEG-TS where the stream encrypting key is transmitted in an Entitlement Control Message (ECM). It is also proposed in an IETF draft for SRTP for Encrypted Key Transport [SRTP-EKT].

The connection-oriented SRT KM ctrl packet is transmitted at the start of the connection, before any data packet. In most case, if the control packet is not lost, the receiver is able to decrypt from the first packet. Otherwise, the initial packets are dropped (or stored for later decryption) until the KM control packet is received. The SRT ctrl packet is retransmitted until acknowledged by the receiver.

### Odd/Even Stream Encrypting Key (oSEK/eSEK)
To ensure seamless rekeying for cryptographic (counter exhausted) or access control reasons, a two-key mechanism, similar to the one used with DVB systems is used. The two keys are identified as the odd key and the even key (oSEK/eSEK). Basically, an odd/even flag in the SRT data header tells which key is in use. The next key to use is transmitted in advance (KM Pre-Announce) to the receivers in a SRT ctrl packet. When rekeying occurs, the SRT data header odd/even flag flips and the receiver already have the new key in hand to continue decrypting the stream without missing a packet.

### Key Encrypting Key (KEK)
The KEK is used by the sender to wrap the SEK and by the receiver to unwrap it and then decrypt the stream. The KEK must be at least the size of the key it protects, the SEK. The KEK is derived from a shared secret, a pre-shared password by default.

The KEK is derived with the PBKDF2 [PCKS5] derivation function with the stream Salt and the shared secret for input. Each stream then uses a unique KEK to encrypt its Keying Material. A compromised KEK does not compromise other streams protected with the same shared secret (but a compromised shared secret compromises all streams protected with KEK derived from it). Late derivation of the KEK using stream Salt also permits to generate a KEK of the proper size, based on the size of the key it protects.

The shared secret can be pre-shared; password derived [PKCS5]; distributed using a proprietary mechanism; or using a standard key distribution mechanism such as GDOI [RFC3547] or MIKEY [RFC3830].

The cryptographic usage limit of the KEK is 2<sup>48</sup> wraps (AESKW) which means virtual infinity at the expected SEK rekeying rate (90000 years to rekey 100 keys every second).

[figure1]: images/srt-encryption-1.png
[figure2]: images/srt-encryption-2.png
