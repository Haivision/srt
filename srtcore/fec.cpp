/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */


#include <string>
#include <map>
#include <vector>
#include <deque>

#include "fec.h"
#include "core.h"
#include "logging.h"

using namespace std;

CorrectorBase::CorrectorBase(CUDT* parent)
{
    m_parent = parent;
}

bool ParseCorrectorConfig(std::string s, CorrectorConfig& out)
{
    vector<string> parts;
    Split(s, ',', back_inserter(parts));

    // Minimum arguments are: rows,cols.
    if (parts.size() < 2)
        return false;

    out.rows = atoi(parts[0].c_str());
    out.cols = atoi(parts[1].c_str());

    if (out.rows == 0 && parts[0] != "0")
        return false;

    if (out.cols == 0 && parts[1] != "0")
        return false;

    for (vector<string>::iterator i = parts.begin()+2; i != parts.end(); ++i)
    {
        vector<string> keyval;
        Split(*i, ':', back_inserter(keyval));
        if (keyval.size() != 2)
            return false;
        out.parameters[keyval[0]] = keyval[1];
    }

    return true;
}

class DefaultCorrector: public CorrectorBase
{
    CorrectorConfig cfg;
    size_t number_cols;
    size_t number_rows;

    typedef vector<bool> row_t;

    vector<row_t> rcv_cells;

    struct Group
    {
        int32_t base; //< Sequence of the first packet in the group
        size_t step;      //< by how many packets the sequence should increase to get the next packet
        size_t drop;      //< by how much the sequence should increase after the group is closed
        size_t collected; //< how many packets were taken to collect the clip

        Group(): base(CSeqNo::m_iMaxSeqNo), step(0), collected(0)
        {
        }

        uint16_t length_clip;
        uint32_t timestamp_clip;
        vector<char> payload_clip;
    };

    // Row Groups: every item represents a single row group and collects clips for one row.
    // Col Groups: every item represents a signel column group and collect clips for packets represented in one column

    struct Send
    {
        // We need only ONE horizontal group. Simply after the group
        // is closed (last packet supplied), and the FEC packet extracted,
        // the group is no longer in use.
        Group row;
        vector<Group> cols;
    } snd;

    struct Receive
    {
        // In reception we need to keep as many horizontal groups as required
        // for possible later tracking. A horizontal group should be dismissed
        // when the size of this container exceeds the `number_rows` (size of the column).
        //
        // The 'deque' type is used here for a trial implementation. A desired solution
        // would be a kind of a ring buffer where new groups are added and old (exceeding
        // the size) automatically dismissed.
        deque<Group> rowq;
        vector<Group> cols;
    } rcv;

    void ConfigureGroup(Group& g, int32_t seqno, size_t gstep, size_t drop);
    void ConfigureColumns(vector<Group>& which, size_t gsize, size_t gstep, size_t gslip, int32_t isn);
    void ResetGroup(Group& g, int32_t seqno);
    void CheckGroupClose(Group& g, size_t size);
    void ClipPacket(Group& g, const CPacket& pkt);
    void PackControl(const Group& g, int8_t groupix, CPacket& pkt, int32_t seqno);

    // This translates the sequence number into two indexes
    // for indexing the 'rcv_cells' array.
    static pair<size_t, size_t> FindCell(int32_t base_seq, int32_t seq)
    {
        // base_seq is the sequence which is one before the
        // very first element, that is element at [0][0] has
        // sequence number base_seq +% 1.

        int offset = CSeqNo::seqoff(base_seq, seq)-1;
        size_t row_size = number_cols;

        ldiv_t r = ldiv(offset, row_size);
        return make_pair(r.quot, r.rem);
    }

public:

    DefaultCorrector(CUDT* parent, const std::string& confstr);

    // Sender side

    // This function creates and stores the FEC control packet with
    // a prediction to be immediately sent. This is called in the function
    // that normally is prepared for extracting a data packet from the sender
    // buffer and send it over the channel.
    virtual bool packCorrectionPacket(ref_t<CPacket> r_packet, int32_t seq) ATR_OVERRIDE;

    // This is called at the moment when the sender queue decided to pick up
    // a new packet from the scheduled packets. This should be then used to
    // continue filling the group, possibly followed by final calculating the
    // FEC control packet ready to send.
    virtual void feedSource(ref_t<CPacket> r_packet) ATR_OVERRIDE;


    // Receiver side

    // This function is called at the moment when a new data packet has
    // arrived (no matter if subsequent or recovered). The 'state' value
    // defines the configured level of loss state required to send the
    // loss report.
    virtual bool receive(CUnit* unit, ref_t< vector<CUnit*> > r_incoming, ref_t<loss_seqs_t> r_loss_seqs) ATR_OVERRIDE;
};

DefaultCorrector::DefaultCorrector(CUDT* parent, const std::string& confstr): CorrectorBase(parent)
{
    if (!ParseCorrectorConfig(confstr, cfg))
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    // It is allowed for rows and cols to have negative value,
    // this way it only marks the fact that particular dimension
    // does not form a FEC group (no FEC control packet sent).
    number_cols = abs(cfg.cols);
    number_rows = abs(cfg.rows);

    if (number_cols == 0 || number_rows == 0)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    // Some tautology for better explanation
    size_t row_size = number_cols;
    size_t col_size = number_rows;

    // Setup the bit matrix, initialize everything with false.

    // Vertical size (y)
    rcv_cells.resize(col_size);

    for (size_t x = 0; i < col_size; ++i)
    {
        rcv_cells[x].resize(row_size);
    }

    // These sequence numbers are both the value of ISN-1 at the moment
    // when the handshake is done. The sender ISN is generated here, the
    // receiver ISN by the peer. Both should be known after the handshake.
    // Later they will be updated as packets are transmitted.

    int32_t snd_isn = CSeqNo::incseq(parent->sndSeqNo());
    int32_t rcv_isn = CSeqNo::incseq(parent->rcvSeqNo());

    // Alright, now we need to get the ISN from parent
    // to extract the sequence number allowing qualification to the group.
    // The base values must be prepared so that feedSource can qualify them.
    // Obtain by parent->getSndSeqNo() and parent->getRcvSeqNo()

    // SEPARATE FOR SENDING AND RECEIVING!

    // Now, assignment of the groups requires:
    // For row groups, simply the size of the group suffices.
    // For column groups, you need a whole matrix of all sequence
    // numbers that are base sequence numbers for the group.
    // Sequences that belong to this group are:
    // 1. First packet has seq+1 towards the base.
    // 2. Every next packet has this value + the size of the row group.
    // So: group dispatching is:
    //  - get the column number
    //  - extract the group data for that column
    //  - check if the sequence is later than the group base sequence, if not, report no group for the packet
    //  - sanity check, if the seqdiff divided by row size gets 0 remainder
    //  - The result from the above division can't exceed the column size, otherwise
    //    it's another group. The number of currently collected data should be in 'collected'.

    // In the beginning we need just one reception group. New reception
    // groups will be created in tact with receiving packets outside this one.
    // The value of rcv.row[0].base will be used as an absolute base for calculating
    // the index of the group for a given received packet.
    rcv.rowq.resize(1);

    // Now set up the group starting sequences.
    // The very first group in both dimensions will have the value of ISN in particular direction.

    // Set up sender part.
    //
    // Size: rows
    // Step: 1 (next packet in group is 1 past the previous one)
    // Slip: rows (first packet in the next group is distant to first packet in the previous group by 'rows')
    ConfigureGroup(snd.row, snd_isn, 1, row_size);
    ConfigureGroup(rcv.rowq[0], rcv_isn, 1, row_size);

    // Size: cols
    // Step: rows (the next packet in the group is one row later)
    // Slip: rows+1 (the first packet in the next group is later by 1 column + one whole row down)
    ConfigureColumns(rcv.cols, number_cols, number_rows+1, rcv_isn);
    ConfigureColumns(snd.cols, number_cols, number_rows+1, snd_isn);
}

void DefaultCorrector::ConfigureColumns(vector<Group>& which, size_t gsize, size_t gstep, size_t gslip, int32_t isn)
{
    // This is to initialize the first set of groups.

    // which: group vector.
    // gsize: number of packets in one group
    // gstep: seqdiff between two packets consecutive in the group
    // gslip: seqdiff between the first packet in one group and first packet in the next group
    // isn: sequence number of the first packet in the first group

    which.resize(gsize);

    int32_t seqno = isn;
    for (size_t i = 0; i < gsize; ++i)
    {
        ConfigureGroup(which[i], seqno, gstep, gstep * gsize);
        seqno = CSeqNo::incseq(seqno, gslip);
    }
}

void DefaultCorrector::ConfigureGroup(Group& g, int32_t seqno, size_t gstep, size_t drop)
{
    g.base = seqno;
    g.step = gstep;

    // This actually rewrites the size of the group here, but
    // by having this value precalculated we simply close the
    // group by adding this value to the base sequence.
    g.drop = drop;
    g.collected = 0;

    // Now the buffer spaces for clips.
    g.payload_clip.resize(parent->OPT_PayloadSize());
}

void DefaultCorrector::ResetGroup(Group& g, int32_t seqno)
{
    g.base = seqno;
    g.collected = 0;

    // This isn't necessary for ConfigureGroup because the
    // vector after resizing is filled with a given value,
    // by default the default value of the type, char(), that is 0.
    g.length_clip = 0;
    g.timestamp_clip = 0;
    memset(&g.payload_clip[0], 0, g.payload_clip.size());
}

void DefaultCorrector::feedSource(ref_t<CPacket> r_packet)
{
    // Handy aliases.
    size_t col_size = number_rows;
    size_t row_size = number_cols;

    // Hang on the matrix. Find by r_packet.get()->getSeqNo().

    //    (The "absolute base" is the cell 0 in vertical groups)
    int32_t base = snd.row.base;

    // (we are guaranteed that this packet is a data packet, so
    // we don't have to check if this isn't a control packet)
    int baseoff = CSeqNo::seqoff(base, r_packet.get().getSeqNo());

    // The algorithm is the following:
    //
    // 1. Get the number of group in both vertical and horizontal groups:
    //    - Horizontal: unnecessary, there's only one group.
    //    - Vertical: offset towards base (% row size, but with updated Base seq unnecessary)

    // Just for a case.
    int vert_gx = baseoff % row_size;

    // 2. Define the position of this packet in the group
    //    - Horizontal: offset towards base (of the given group, not absolute!)
    int horiz_pos = baseoff;
    //    - Vertical: (seq-base)/column_size
    int32_t vert_base = snd.cols[vert_gx].base;
    int vert_off = CSeqNo::seqoff(vert_base, r_packet.get().getSeqNo());

    // It MAY HAPPEN that the base is newer than the sequence of the packet.
    // This may normally happen in the beginning period, where the bases
    // set up initially for all columns got the shift, so they are kinda from
    // the future, and "this sequence" is in a group that is already closed.
    // In this case simply can't clip the packet in the column group.

    bool clip_column = vert_off >= 0;

    // SANITY: check if the rule applies on the group
    if (vert_off % row_size)
    {
        LOGC(mglog.Fatal, log << "FEC:feedSource: VGroup #" << vert_gx << " base=%" << vert_base
                << " WRONG with horiz base=%" << base);

        // Do not place it, it would be wrong.
        return;
    }

    int vert_pos = vert_off / row_size;

    // 3. The group should be check for the necessity of being closed.
    // Note that FEC packet extraction doesn't change the state of the
    // VERTICAL groups (it can be potentially extracted multiple times),
    // only the horizontal in order to mark that the vertical FEC is
    // extracted already. So, anyway, check if the group limit was reached
    // and it wasn't closed.
    // 4. Apply the clip
    // 5. Increase collected.

    CheckGroupClose(snd.row, horiz_pos, row_size);
    ClipPacket(snd.row, *r_packet);
    snd.row.collected++;

    if (clip_column)
    {
        CheckGroupClose(snd.cols[vert_gx], vert_pos, col_size);
        ClipPacket(snd.cols[vert_gx], *r_packet);
        snd.cols[vert_gx].collected++;
    }
}

void DefaultCorrector::CheckGroupClose(Group& g, int pos, size_t size)
{
    if (pos < size)
        return;

    int32_t new_seq_base = CSeqNo::incseq(g.base, g.drop);
    ResetGroup(g, new_seq_base);
}

void DefaultCorrector::ClipPacket(Group& g, const CPacket& pkt)
{
    // Both length and timestamp must be taken as NETWORK ORDER
    // before applying the clip.

    uint16_t length_net = htons(pkt.getLength());
    g.length_clip = g.length_clip ^ length_net;

    // NOTE: Unlike length, the TIMESTAMP is NOT endian-reordered
    // because it will be written into the TIMESTAMP field in the
    // header, and header is inverted automatically when sending,
    // unlike the contents of the payload, where the length will be written.
    uint32_t timestamp_hw = pkt.getMsgTimeStamp();
    g.timestamp_clip = g.timestamp_clip ^ timestamp_hw;

    // Payload goes "as is".
    for (int i = 0; i < pkt.getLength(); ++i)
    {
        g.payload_clip[i] = g.payload_clip[i] ^ pkt.m_pcData[i];
    }

    // Fill the rest with zeros. When this packet is going to be
    // recovered, the payload extraced from this process will have
    // the maximum lenght, but it will be cut to the right length
    // and these padding 0s taken out.
    for (int i = pkt.getLength(); i < parent->OPT_PayloadSize(); ++i)
        g.payload_clip[i] = g.payload_clip[i] ^ 0;
}

bool DefaultCorrector::packCorrectionPacket(ref_t<CPacket> r_packet, int32_t seq)
{
    // If the FEC packet is not yet ready for extraction, do nothing and return false.
    // Check if seq is the last sequence of the group.

    // 1. Check horizontal readiness first.

    // If it's ready for extraction, extract it, and write into the packet.
    //
    // NOTE: seq is the sequence number of the LAST PACKET SENT regularly.
    // This is only about to be shifted forward by 1 to be placed on the
    // data packet. The packet in `r_packet` doesn't have the sequence number
    // installed yet

    // For BOTH vertical and horizontal snd groups:
    // - Check if the "full group" condition is satisfied (all packets from the group are clipped)
    // - If not, simply return false and do nothing
    // - If so, store the current clip state into the referenced packet, give it the 'seq' sequence

    // After packing the FEC packet:
    // - update the base sequence in the group for which it's packed
    // - make sure that pointers are reset to not suggest the packet is ready
    if (snd.row.collected >= number_cols)
    {
        // SHIP THE HORIZONTAL FEC packet.
        PackControl(snd.row, -1, *r_packet, CSeqNo::incseq(seq));

        // RESET THE HORIZONTAL GROUP.
        ResetGroup(snd.row);
        return true;
    }

    int offset = CSeqNo::seqoff(snd.row.base, seq);
    int vert_gx = (offset + number_cols) % number_cols;
    if (snd.cols[vert_gx].collected >= number_rows)
    {
        // SHIP THE VERTICAL FEC packet.
        PackControl(snd.cols[vert_gx], vert_gx, *r_packet, CSeqNo::incseq(seq));

        // RESET THE GROUP THAT WAS SENT
        ResetGroup(snd.cols[vert_gx]);
        return true;
    }

    return false;
}

void DefaultCorrector::PackControl(Group& g, signed char index, CPacket& pkt, int32_t seq, int kflg)
{
    // Allocate as much space as needed, regardless of the PAYLOADSIZE value.

    static const size_t
        INDEX_SIZE = 1,
        PAD_SIZE = 1,

    size_t total_size =
        INDEX_SIZE + PAD_SIZE
        + sizeof(g.length_clip)
        + sizeof(g.timestamp_clip);
        + parent->OPT_PayloadSize();

    pkt.allocate(total_size);

    char* out = pkt.m_pcData;
    size_t off = 0;
    // Spread the index. This is the index of the payload in the vertical group.
    // For horizontal group this value is always -1.
    out[off++] = index;
    // Reserved space for flags
    out[off++] = 0;

    // Ok, now the length clip
    memcpy(out+off, &length_clip, sizeof length_clip);
    off += sizeof length_clip;

    // And finally the payload clip
    memcpy(out+off, &g.payload_clip[0], g.payload_clip.size());

    // Ready. Now fill the header and finalize other data.
    pkt.setLength(total_size);

    pkt.m_iTimeStamp = g.timestamp_clip;
    pkt.m_iSeqNo = seq;

    // This sets only the Packet Boundary flags, while all other things:
    // - Order
    // - Rexmit
    // - Crypto
    // - Message Number
    // will be set to 0/false
    pkt.m_iMsgNo = MSGNO_PACKET_BOUNDARY::wrap(PB_SOLO);

    // ... and then fix only the Crypto flags
    pkt.setMsgCryptoFlags(kflg); // THEN, set crypto flags, if needed.

    // Don't set the ID, it will be later set for any kind of packet.
    // Write the timestamp clip into the timestamp field.
}

bool DefaultCorrector::receive(CUnit* unit, ref_t< vector<CUnit*> > r_incoming, ref_t<loss_seqs_t> r_loss_seqs)
{
    // XXX TRIAL IMPLEMENTATION for testing the initial statements.
    // This simply moves the packet to the input queue.
    // Just check if the packet is the FEC packet.

    CPacket& pkt = unit->m_Packet;

    if (pkt.getMsgSeq() == 0)
    {
        // Exit with empty output, it doesn't matter how the loss check is reported.
        return false;
    }

    r_incoming.get().push_back(unit);
    return true;


    // Get the packet from the incoming stream, already recognized
    // as data packet, and then:
    //
    // (Note that the default builtin FEC mechanism uses such rules:
    //  - allows SRT to get the packet, even if it follows the loss
    //  - depending on m_fallback_level, confirms or denies the need that SRT handle the loss
    //  - in loss_seqs we return those that are not recoverable at the current level
    //  - FEC has no extra header provided, so regular data are passed as is
    //)
    // So, the needs to implement:
    // 
    // 1. If this is a FEC packet, close the group, check for lost packets, try to recover.
    // Check if there is recovery possible, if so, request a new unit and pack the recovered packet there.
    // Report the loss to be reported by SRT according to m_fallback_level:
    // - FS_ALWAYS: N/A for a FEC packet
    // - FS_EARLY: When Horizontal group is closed and the packet is not recoverable, report this in loss_seqs
    // - FS_LATELY: When Horizontal and Vertical group is closed and the packet is not recoverable, report it.
    // - FS_NEVER: Always return empty loss_seqs
    //
    // 2. If this is a regular packet, use it for building the FEC group.
    // - FS_ALWAYS: always return true and leave loss_seqs empty.
    // - others: return false and return nothing in loss_seqs
}


Corrector::NamePtr Corrector::builtin_correctors[] =
{
    {"default", Creator<DefaultCorrector>::Create },
};

bool Corrector::IsBuiltin(const string& s)
{
    size_t size = sizeof builtin_correctors/sizeof(builtin_correctors[0]);
    for (size_t i = 0; i < size; ++i)
        if (s == builtin_correctors[i].first)
            return true;

    return false;
}

Corrector::correctors_map_t Corrector::correctors;

void Corrector::globalInit()
{
    // Add the builtin correctors to the global map.
    // Users may add their correctors after that.
    // This function is called from CUDTUnited::startup,
    // which is guaranteed to run the initializing
    // procedures only once per process.

    for (size_t i = 0; i < sizeof builtin_correctors/sizeof (builtin_correctors[0]); ++i)
        correctors[builtin_correctors[i].first] = builtin_correctors[i].second;

    // Actually there's no problem with calling this function
    // multiple times, at worst it will overwrite existing correctors
    // with the same builtin.
}


bool Corrector::configure(CUDT* parent, const std::string& confstr)
{
    CorrectorConfig cfg;
    if (!ParseCorrectorConfig(confstr, cfg))
        return false;

    // Extract the "type" key from parameters, or use
    // builtin if lacking.
    string type = cfg["type"];

    correctors_map_t::iterator selector;
    if ( type == "")
        selector = correctors.begin();
    else
    {
        selector = correctors.find(type);
        if (selector == correctors.end())
            return false;
    }

    // Found a corrector, so call the creation function
    corrector = (*selector->second)(parent, confstr);

    // The corrector should have pinned in all events
    // that are of its interest. It's stated that
    // it's ready after creation.
    return !!corrector;
}

bool Corrector::correctConfig(const CorrectorConfig& conf)
{
    const string* pname = map_getp(conf.parameters, "type");

    if (!pname)
        return true; // default, parameters ignored

    if (*pname == "adaptive")
        return true;

    correctors_map_t::iterator x = correctors.find(*pname);
    if (x == correctors.end())
        return false;

    return true;
}

Corrector::~Corrector()
{
    delete corrector;
    corrector = 0;
}
