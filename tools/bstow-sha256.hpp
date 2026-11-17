#ifndef INC_BSTOW_SHA256_H
#define INC_BSTOW_SHA256_H

#include <cstdint>
#include <cstddef>
#include <fstream>
#include <string>

// Minimal SHA-256 (FIPS 180-4) for the file identity checks of BSTOW.

namespace bstow
{

class Sha256
{
    uint32_t m_h[8];
    unsigned char m_buf[64];
    size_t m_buflen;
    uint64_t m_total;

    static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block(const unsigned char* p)
    {
        static const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
        {
            w[i] = (uint32_t(p[4*i]) << 24) | (uint32_t(p[4*i+1]) << 16)
                | (uint32_t(p[4*i+2]) << 8) | uint32_t(p[4*i+3]);
        }
        for (int i = 16; i < 64; ++i)
        {
            uint32_t s0 = ror(w[i-15], 7) ^ ror(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = ror(w[i-2], 17) ^ ror(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }

        uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3];
        uint32_t e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];

        for (int i = 0; i < 64; ++i)
        {
            uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = h + S1 + ch + K[i] + w[i];
            uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }

        m_h[0] += a; m_h[1] += b; m_h[2] += c; m_h[3] += d;
        m_h[4] += e; m_h[5] += f; m_h[6] += g; m_h[7] += h;
    }

public:
    Sha256(): m_buflen(0), m_total(0)
    {
        static const uint32_t init[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        };
        for (int i = 0; i < 8; ++i)
            m_h[i] = init[i];
    }

    void update(const void* data, size_t len)
    {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        m_total += len;
        while (len)
        {
            size_t take = 64 - m_buflen;
            if (take > len)
                take = len;
            for (size_t i = 0; i < take; ++i)
                m_buf[m_buflen + i] = p[i];
            m_buflen += take;
            p += take;
            len -= take;
            if (m_buflen == 64)
            {
                block(m_buf);
                m_buflen = 0;
            }
        }
    }

    // Returns the 32-byte raw digest. The object can't be used after that.
    std::string final()
    {
        uint64_t bits = m_total * 8;
        unsigned char pad[72] = { 0x80 };
        size_t padlen = (m_buflen < 56) ? (56 - m_buflen) : (120 - m_buflen);
        unsigned char lenbytes[8];
        for (int i = 0; i < 8; ++i)
            lenbytes[i] = (unsigned char)(bits >> (56 - 8*i));
        update(pad, padlen);
        update(lenbytes, 8);

        std::string out(32, '\0');
        for (int i = 0; i < 8; ++i)
        {
            out[4*i] = char(m_h[i] >> 24);
            out[4*i+1] = char(m_h[i] >> 16);
            out[4*i+2] = char(m_h[i] >> 8);
            out[4*i+3] = char(m_h[i]);
        }
        return out;
    }
};

inline std::string ToHex(const std::string& raw)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i)
    {
        unsigned char c = raw[i];
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out;
}

// Returns the raw 32-byte digest of the file content, or empty string on failure.
inline std::string Sha256File(const std::string& path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in)
        return std::string();

    Sha256 sha;
    std::string chunk(1 << 20, '\0');
    while (in)
    {
        in.read(&chunk[0], chunk.size());
        sha.update(chunk.data(), size_t(in.gcount()));
    }
    if (in.bad())
        return std::string();
    return sha.final();
}

}

#endif
