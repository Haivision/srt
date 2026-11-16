#ifndef INC_BSTOW_VERIFY_H
#define INC_BSTOW_VERIFY_H

#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "bstow-read.hpp"
#include "bstow-bits.hpp"
#include "bstow-sha256.hpp"

// Verification of the BSTOW file together with the external files
// that it refers to. Used by PacketReader::Verify, bstow-verify-ts
// and bstow-generate-ts.

namespace bstow
{

const size_t VERIFY_TS_SIZE = 188;

// Arguments: file name, position in this file, error description.
// Errors found in the BSTOW stream itself are reported with the BSTOW path.
typedef std::function<void(const std::string&, int64_t, const std::string&)> VerifyReporter;

inline std::string DirOf(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return std::string();
    return path.substr(0, slash + 1);
}

// Relative paths are resolved against the directory of the BSTOW file.
inline std::string ResolveExternal(const std::string& basedir, const std::string& path)
{
    // Treat as absolute: leading slash, or drive letter, or UNC.
    bool absolute = !path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'));
    return absolute ? path : basedir + path;
}

class Verifier
{
    std::string m_Path;
    std::string m_BaseDir;
    VerifyReporter m_Report;
    bool m_StopFirst;
    size_t m_Errors;

    struct ExtFile
    {
        bool exists = false;
        uint64_t size = 0;
        std::string sha;
        std::set<std::string> mismatches;
    };
    std::map<std::string, ExtFile> m_Files;

    // Currently selected external file
    ExtFile* m_Cur = nullptr;
    bool m_CurOk = false;
    std::string m_CurName;
    std::ifstream m_Ext;

    bool Stop() const { return m_StopFirst && m_Errors; }

    void Err(const std::string& file, int64_t pos, const std::string& msg)
    {
        ++m_Errors;
        m_Report(file, pos, msg);
    }

    static uint64_t ParseValue(const std::vector<unsigned char>& data)
    {
        uint64_t v = 0;
        for (size_t i = 0; i < data.size(); ++i)
            v = (v << 8) | data[i];
        return v;
    }

    ExtFile& Load(const std::string& full)
    {
        auto it = m_Files.find(full);
        if (it != m_Files.end())
            return it->second;

        ExtFile& f = m_Files[full];
        std::ifstream in(full, std::ios::in | std::ios::binary);
        if (!in)
        {
            Err(full, 0, "Can't open the file");
            return f;
        }
        in.seekg(0, std::ios::end);
        f.size = uint64_t(in.tellg());
        in.close();

        f.sha = Sha256File(full);
        if (f.sha.empty())
        {
            Err(full, 0, "Can't read the file to calculate the checksum");
            return f;
        }
        f.exists = true;
        return f;
    }

    void SelectFile(const std::string& name, bool has_sha, const std::string& declared_sha, int64_t unitpos)
    {
        std::string full = ResolveExternal(m_BaseDir, name);
        m_Cur = nullptr;
        m_CurOk = false;
        m_Ext.close();
        m_Ext.clear();
        m_CurName = full;

        ExtFile& f = Load(full);
        m_Cur = &f;
        if (!f.exists)
            return;

        // A missing CHECKSUM is reported by the caller; the ranges are still checked.
        if (has_sha)
        {
            if (declared_sha.size() != 32)
            {
                Err(m_Path, unitpos, "CHECKSUM for '" + name + "' must be 32 bytes long");
                return;
            }

            if (f.sha != declared_sha)
            {
                if (f.mismatches.insert(declared_sha).second)
                    Err(full, 0, "Checksum mismatch: declared " + ToHex(declared_sha) + ", actual " + ToHex(f.sha));
                return;
            }
        }

        m_Ext.open(full, std::ios::in | std::ios::binary);
        m_CurOk = bool(m_Ext);
        if (!m_CurOk)
            Err(full, 0, "Can't open the file");
    }

    void CheckRange(uint64_t offset, uint64_t length)
    {
        const std::string& file = m_CurName;
        if (length == 0)
        {
            Err(file, offset, "Zero payload length");
            return;
        }
        if (offset > m_Cur->size || length > m_Cur->size - offset)
        {
            Err(file, offset, "Payload of " + std::to_string(length) + " bytes exceeds the file size "
                    + std::to_string(m_Cur->size));
            return;
        }

        bool aligned = true;
        if (offset % VERIFY_TS_SIZE)
        {
            Err(file, offset, "Offset is not aligned to the TS packet size");
            aligned = false;
        }
        if (length % VERIFY_TS_SIZE)
        {
            Err(file, offset, "Length " + std::to_string(length) + " is not a multiple of the TS packet size");
            aligned = false;
        }
        if (!aligned || Stop())
            return;

        std::vector<char> buf(length);
        m_Ext.clear();
        m_Ext.seekg(offset);
        m_Ext.read(buf.data(), buf.size());
        if (uint64_t(m_Ext.gcount()) != length)
        {
            Err(file, offset, "Can't read the declared payload from the file");
            return;
        }

        for (size_t i = 0; i < length && !Stop(); i += VERIFY_TS_SIZE)
        {
            if (static_cast<unsigned char>(buf[i]) != 0x47)
            {
                Err(file, offset + i, "No TS sync byte (0x47) at the packet start, found 0x"
                        + ToHex(std::string(1, buf[i])));
            }
        }
    }

public:
    // If stop_first is set, Run() returns just after the first error.
    Verifier(const std::string& bstow_path, const VerifyReporter& reporter, bool stop_first)
        : m_Path(bstow_path), m_BaseDir(DirOf(bstow_path)), m_Report(reporter),
        m_StopFirst(stop_first), m_Errors(0)
    {
    }

    // Returns the number of errors found
    size_t Run()
    {
        std::ifstream in(m_Path, std::ios::in | std::ios::binary);
        if (!in)
        {
            Err(m_Path, 0, "Can't open the BSTOW file");
            return m_Errors;
        }
        in.seekg(0, std::ios::end);
        const int64_t bsize = in.tellg();
        in.seekg(0);

        while (!Stop())
        {
            const int64_t upos = in.tellg();
            if (upos >= bsize)
                break;

            char hdr[4];
            in.read(hdr, 4);
            if (in.gcount() < 4)
            {
                Err(m_Path, upos, "Truncated header");
                break;
            }
            if (memcmp(hdr, g_header, 4) != 0)
            {
                Err(m_Path, upos, "No 4-byte BSTOW signature found");
                break;
            }

            bool have_len = false, have_time = false, have_fn = false, have_ck = false;
            bool ended = false, broken = false;
            int endlabel = 0;
            uint64_t length = 0, offset = 0;
            std::string fname, checksum;

            while (!ended)
            {
                const int64_t ppos = in.tellg();
                unsigned char p[4];
                in.read((char*)p, 4);
                if (in.gcount() < 4)
                {
                    Err(m_Path, ppos, "Truncated parameter");
                    broken = true;
                    break;
                }

                int label;
                uint64_t value = 0;
                std::vector<unsigned char> bytes;
                if (Bits<7>::unwrap(p[0]) == 0)
                {
                    label = p[0];
                    value = Bits<23, 16>::wrap(p[1]) | Bits<15, 8>::wrap(p[2]) | p[3];
                }
                else
                {
                    label = Bits<14, 8>::wrap(p[0]) | p[1];
                    size_t len = Bits<15, 8>::wrap(p[2]) | p[3];
                    bytes.resize(len);
                    in.read((char*)bytes.data(), len);
                    if (size_t(in.gcount()) < len)
                    {
                        Err(m_Path, ppos, "Truncated value of the parameter " + std::to_string(label));
                        broken = true;
                        break;
                    }
                    if (len <= 8)
                        value = ParseValue(bytes);
                }

                switch (label)
                {
                case DEF_LENGTH:
                    have_len = true;
                    length = value;
                    break;

                case DEF_PLAYTIME:
                case DEF_SENDTIME:
                    have_time = true;
                    break;

                case DEF_FILENAME:
                    have_fn = true;
                    fname.assign(bytes.begin(), bytes.end());
                    break;

                case DEF_CHECKSUM:
                    have_ck = true;
                    checksum.assign(bytes.begin(), bytes.end());
                    break;

                case DEF_DATA:
                case DEF_OFFSET:
                    ended = true;
                    endlabel = label;
                    offset = value;
                    break;

                default:
                    Err(m_Path, ppos, "Unknown parameter " + std::to_string(label));
                }
            }

            if (broken)
                break; // can't resync

            if (!have_len)
                Err(m_Path, upos, "LENGTH not specified");
            if (!have_time)
                Err(m_Path, upos, "Neither PLAYTIME nor SENDTIME specified");
            if (have_fn && !have_ck)
                Err(m_Path, upos, "FILENAME '" + fname + "' specified without CHECKSUM");
            if (have_ck && !have_fn)
                Err(m_Path, upos, "CHECKSUM specified without FILENAME");

            if (have_fn)
                SelectFile(fname, have_ck, checksum, upos);

            if (endlabel == DEF_DATA)
            {
                int64_t here = in.tellg();
                if (here < 0 || uint64_t(bsize - here) < length)
                {
                    Err(m_Path, upos, "Payload exceeds the BSTOW file size");
                    break;
                }
                in.seekg(here + int64_t(length));
            }
            else if (!m_Cur)
            {
                Err(m_Path, upos, "OFFSET specified without any FILENAME");
            }
            else if (m_CurOk && have_len)
            {
                CheckRange(offset, length);
            }
        }

        return m_Errors;
    }
};

}

#endif
