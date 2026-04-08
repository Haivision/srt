/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 01/22/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#include "platform_sys.h"

#include "list.h"
#include "packet.h"
#include "logging.h"
#include "logger_fas.h"

using namespace srt::logging;
using namespace srt::sync;

namespace srt
{

////////////////////////////////////////////////////////////////////////////////

CRcvLossList::CRcvLossList(int size)
    : m_caSeq()
    , m_iHead(-1)
    , m_iTail(-1)
    , m_iLength(0)
    , m_iSize(size)
    , m_iLargestSeq(SRT_SEQNO_NONE)
{
    m_caSeq = new Seq[m_iSize];

    // -1 means there is no data in the node
    for (int i = 0; i < size; ++i)
    {
        m_caSeq[i].seqstart = SRT_SEQNO_NONE;
        m_caSeq[i].seqend   = SRT_SEQNO_NONE;
    }
}

CRcvLossList::~CRcvLossList()
{
    delete[] m_caSeq;
}

int CRcvLossList::insert(int32_t seqno1, int32_t seqno2)
{
    SRT_ASSERT(seqno1 != SRT_SEQNO_NONE && seqno2 != SRT_SEQNO_NONE);
    // Make sure that seqno2 isn't earlier than seqno1.
    SRT_ASSERT(CSeqNo::seqcmp(seqno1, seqno2) <= 0);

    // Data to be inserted must be larger than all those in the list
    if (m_iLargestSeq != SRT_SEQNO_NONE && CSeqNo::seqcmp(seqno1, m_iLargestSeq) <= 0)
    {
        if (CSeqNo::seqcmp(seqno2, m_iLargestSeq) > 0)
        {
            LOGC(qrlog.Warn,
                 log << "RCV-LOSS/insert: seqno1=" << seqno1 << " too small, adjust to "
                     << CSeqNo::incseq(m_iLargestSeq));
            seqno1 = CSeqNo::incseq(m_iLargestSeq);
        }
        else
        {
            LOGC(qrlog.Warn,
                 log << "RCV-LOSS/insert: (" << seqno1 << "," << seqno2
                     << ") to be inserted is too small: m_iLargestSeq=" << m_iLargestSeq << ", m_iLength=" << m_iLength
                     << ", m_iHead=" << m_iHead << ", m_iTail=" << m_iTail << " -- REJECTING");
            return 0;
        }
    }
    m_iLargestSeq = seqno2;

    if (0 == m_iLength)
    {
        // insert data into an empty list
        m_iHead                   = 0;
        m_iTail                   = 0;
        m_caSeq[m_iHead].seqstart = seqno1;
        if (seqno2 != seqno1)
            m_caSeq[m_iHead].seqend = seqno2;

        m_caSeq[m_iHead].inext  = -1;
        m_caSeq[m_iHead].iprior = -1;
        const int n = CSeqNo::seqlen(seqno1, seqno2);
        m_iLength += n;
        return n;
    }

    // otherwise searching for the position where the node should be
    const int offset = CSeqNo::seqoff(m_caSeq[m_iHead].seqstart, seqno1);
    if (offset < 0)
    {
        LOGC(qrlog.Error,
             log << "RCV-LOSS/insert: IPE: new LOSS %(" << seqno1 << "-" << seqno2 << ") PREDATES HEAD %"
                 << m_caSeq[m_iHead].seqstart << " -- REJECTING");
        return -1;
    }

    int loc = (m_iHead + offset) % m_iSize;

    if ((SRT_SEQNO_NONE != m_caSeq[m_iTail].seqend) && (CSeqNo::incseq(m_caSeq[m_iTail].seqend) == seqno1))
    {
        // coalesce with prior node, e.g., [2, 5], [6, 7] becomes [2, 7]
        loc                 = m_iTail;
        m_caSeq[loc].seqend = seqno2;
    }
    else
    {
        // create new node
        m_caSeq[loc].seqstart = seqno1;

        if (seqno2 != seqno1)
            m_caSeq[loc].seqend = seqno2;

        m_caSeq[m_iTail].inext = loc;
        m_caSeq[loc].iprior    = m_iTail;
        m_caSeq[loc].inext     = -1;
        m_iTail                = loc;
    }

    const int n = CSeqNo::seqlen(seqno1, seqno2);
    m_iLength += n;
    return n;
}

bool CRcvLossList::remove(int32_t seqno)
{
    if (m_iLargestSeq == SRT_SEQNO_NONE || CSeqNo::seqcmp(seqno, m_iLargestSeq) > 0)
        m_iLargestSeq = seqno;

    if (0 == m_iLength)
        return false;

    // locate the position of "seqno" in the list
    int offset = CSeqNo::seqoff(m_caSeq[m_iHead].seqstart, seqno);
    if (offset < 0)
        return false;

    int loc = (m_iHead + offset) % m_iSize;

    if (seqno == m_caSeq[loc].seqstart)
    {
        // This is a seq. no. that starts the loss sequence

        if (SRT_SEQNO_NONE == m_caSeq[loc].seqend)
        {
            // there is only 1 loss in the sequence, delete it from the node
            if (m_iHead == loc)
            {
                m_iHead = m_caSeq[m_iHead].inext;
                if (-1 != m_iHead)
                    m_caSeq[m_iHead].iprior = -1;
                else
                    m_iTail = -1;
            }
            else
            {
                m_caSeq[m_caSeq[loc].iprior].inext = m_caSeq[loc].inext;
                if (-1 != m_caSeq[loc].inext)
                    m_caSeq[m_caSeq[loc].inext].iprior = m_caSeq[loc].iprior;
                else
                    m_iTail = m_caSeq[loc].iprior;
            }

            m_caSeq[loc].seqstart = SRT_SEQNO_NONE;
        }
        else
        {
            // there are more than 1 loss in the sequence
            // move the node to the next and update the starter as the next loss inSeqNo(seqno)

            // find next node
            int i = (loc + 1) % m_iSize;

            // remove the "seqno" and change the starter as next seq. no.
            m_caSeq[i].seqstart = CSeqNo::incseq(m_caSeq[loc].seqstart);

            // process the sequence end
            if (CSeqNo::seqcmp(m_caSeq[loc].seqend, CSeqNo::incseq(m_caSeq[loc].seqstart)) > 0)
                m_caSeq[i].seqend = m_caSeq[loc].seqend;

            // remove the current node
            m_caSeq[loc].seqstart = SRT_SEQNO_NONE;
            m_caSeq[loc].seqend   = SRT_SEQNO_NONE;

            // update list pointer
            m_caSeq[i].inext  = m_caSeq[loc].inext;
            m_caSeq[i].iprior = m_caSeq[loc].iprior;

            if (m_iHead == loc)
                m_iHead = i;
            else
                m_caSeq[m_caSeq[i].iprior].inext = i;

            if (m_iTail == loc)
                m_iTail = i;
            else
                m_caSeq[m_caSeq[i].inext].iprior = i;
        }

        m_iLength--;
        if (m_iLength == 0)
            m_iLargestSeq = SRT_SEQNO_NONE;

        return true;
    }

    // There is no loss sequence in the current position
    // the "seqno" may be contained in a previous node

    // searching previous node
    int i = (loc - 1 + m_iSize) % m_iSize;
    while (SRT_SEQNO_NONE == m_caSeq[i].seqstart)
        i = (i - 1 + m_iSize) % m_iSize;

    // not contained in this node, return
    if ((SRT_SEQNO_NONE == m_caSeq[i].seqend) || (CSeqNo::seqcmp(seqno, m_caSeq[i].seqend) > 0))
        return false;

    if (seqno == m_caSeq[i].seqend)
    {
        // it is the sequence end

        if (seqno == CSeqNo::incseq(m_caSeq[i].seqstart))
            m_caSeq[i].seqend = SRT_SEQNO_NONE;
        else
            m_caSeq[i].seqend = CSeqNo::decseq(seqno);
    }
    else
    {
        // split the sequence

        // construct the second sequence from CSeqNo::incseq(seqno) to the original sequence end
        // located at "loc + 1"
        loc = (loc + 1) % m_iSize;

        m_caSeq[loc].seqstart = CSeqNo::incseq(seqno);
        if (CSeqNo::seqcmp(m_caSeq[i].seqend, m_caSeq[loc].seqstart) > 0)
            m_caSeq[loc].seqend = m_caSeq[i].seqend;

        // the first (original) sequence is between the original sequence start to CSeqNo::decseq(seqno)
        if (seqno == CSeqNo::incseq(m_caSeq[i].seqstart))
            m_caSeq[i].seqend = SRT_SEQNO_NONE;
        else
            m_caSeq[i].seqend = CSeqNo::decseq(seqno);

        // update the list pointer
        m_caSeq[loc].inext  = m_caSeq[i].inext;
        m_caSeq[i].inext    = loc;
        m_caSeq[loc].iprior = i;

        if (m_iTail == i)
            m_iTail = loc;
        else
            m_caSeq[m_caSeq[loc].inext].iprior = loc;
    }

    m_iLength--;
    if (m_iLength == 0)
        m_iLargestSeq = SRT_SEQNO_NONE;

    return true;
}

bool CRcvLossList::remove(int32_t seqno1, int32_t seqno2)
{
    if (CSeqNo::seqcmp(seqno1, seqno2) > 0)
    {
        return false;
    }
    for (int32_t i = seqno1; CSeqNo::seqcmp(i, seqno2) <= 0; i = CSeqNo::incseq(i))
    {
        remove(i);
    }
    return true;
}

int32_t CRcvLossList::removeUpTo(int32_t seqno_last)
{
    int32_t first = getFirstLostSeq();
    if (first == SRT_SEQNO_NONE)
    {
        //HLOGC(tslog.Debug, log << "rcv-loss: DROP to %" << seqno_last << " - empty list");
        return first; // empty, so nothing to remove
    }

    if (CSeqNo::seqcmp(seqno_last, first) < 0)
    {
        //HLOGC(tslog.Debug, log << "rcv-loss: DROP to %" << seqno_last << " - first %" << first << " is newer, exiting");
        return first; // seqno_last older than first - nothing to remove
    }

    HLOGC(tslog.Debug, log << "rcv-loss: DROP to %" << seqno_last << " ...");

    // NOTE: seqno_last is past-the-end here. Removed are only seqs
    // that are earlier than this.
    for (int32_t i = first; CSeqNo::seqcmp(i, seqno_last) <= 0; i = CSeqNo::incseq(i))
    {
        //HLOGC(tslog.Debug, log << "... removing %" << i);
        remove(i);
    }

    return first;
}

bool CRcvLossList::find(int32_t seqno1, int32_t seqno2) const
{
    if (0 == m_iLength)
        return false;

    int p = m_iHead;

    while (-1 != p)
    {
        if ((CSeqNo::seqcmp(m_caSeq[p].seqstart, seqno1) == 0) ||
            ((CSeqNo::seqcmp(m_caSeq[p].seqstart, seqno1) > 0) && (CSeqNo::seqcmp(m_caSeq[p].seqstart, seqno2) <= 0)) ||
            ((CSeqNo::seqcmp(m_caSeq[p].seqstart, seqno1) < 0) && (m_caSeq[p].seqend != SRT_SEQNO_NONE) &&
             CSeqNo::seqcmp(m_caSeq[p].seqend, seqno1) >= 0))
            return true;

        p = m_caSeq[p].inext;
    }

    return false;
}

int CRcvLossList::getLossLength() const
{
    return m_iLength;
}

int32_t CRcvLossList::getFirstLostSeq() const
{
    if (0 == m_iLength)
        return SRT_SEQNO_NONE;

    return m_caSeq[m_iHead].seqstart;
}

void CRcvLossList::getLossArray(int32_t* array, int& len, int limit)
{
    len = 0;

    int i = m_iHead;

    while ((len < limit - 1) && (-1 != i))
    {
        array[len] = m_caSeq[i].seqstart;
        if (SRT_SEQNO_NONE != m_caSeq[i].seqend)
        {
            // there are more than 1 loss in the sequence
            array[len] |= LOSSDATA_SEQNO_RANGE_FIRST;
            ++len;
            array[len] = m_caSeq[i].seqend;
        }

        ++len;

        i = m_caSeq[i].inext;
    }
}

CRcvFreshLoss::CRcvFreshLoss(int32_t seqlo, int32_t seqhi, int initial_age)
    : ttl(initial_age)
    , timestamp(steady_clock::now())
{
    seq[0] = seqlo;
    seq[1] = seqhi;
}

CRcvFreshLoss::Emod CRcvFreshLoss::revoke(int32_t sequence)
{
    int32_t diffbegin = CSeqNo::seqcmp(sequence, seq[0]);
    int32_t diffend   = CSeqNo::seqcmp(sequence, seq[1]);

    if (diffbegin < 0 || diffend > 0)
    {
        return NONE; // not within the range at all.
    }

    if (diffbegin == 0)
    {
        if (diffend == 0) // exactly at begin and end
        {
            return DELETE;
        }

        // only exactly at begin. Shrink the range
        seq[0] = CSeqNo::incseq(seq[0]);
        return STRIPPED;
    }

    if (diffend == 0) // exactly at end
    {
        seq[1] = CSeqNo::decseq(seq[1]);
        return STRIPPED;
    }

    return SPLIT;
}

CRcvFreshLoss::Emod CRcvFreshLoss::revoke(int32_t lo, int32_t hi)
{
    // This should only if the range lo-hi is anyhow covered by seq[0]-seq[1].

    // Note: if the checked item contains sequences that are OLDER
    // than the oldest sequence in this range, they should be deleted,
    // even though this wasn't explicitly requested.

    // LOHI:               <lo, hi>
    // ITEM:  <lo, hi>                      <--- delete
    // If the sequence range is older than the range to be revoked,
    // delete it anyway.
    if (lo != SRT_SEQNO_NONE && CSeqNo::seqcmp(lo, seq[1]) > 0)
        return DELETE;
    // IF <lo> is NONE, then rely simply on that item.hi <% arg.hi,
    // which is a condition at the end.

    // LOHI:  <lo, hi>
    // ITEM:             <lo, hi>  <-- NOTFOUND
    // This element is newer than the given sequence, so match failed.
    if (CSeqNo::seqcmp(hi, seq[0]) < 0)
        return NONE;

    // LOHI:     <lo,     hi>
    // ITEM:       <lo,    !     hi>
    // RESULT:            <lo,   hi>
    // 2. If the 'hi' is in the middle (less than seq[1]), delete partially.
    // That is, take care of this range for itself and return STRIPPED.
    if (CSeqNo::seqcmp(hi, seq[1]) < 0)
    {
        seq[0] = CSeqNo::incseq(hi);
        return STRIPPED;
    }

    // LOHI:            <lo,         hi>
    // ITEM:       <lo,    !     hi>
    // RESULT: DELETE.
    // 3. Otherwise delete the record, even if this was covering only part of this range.
    // This is not possible that the sequences OLDER THAN THIS are not required to be
    // revoken together with this one.

    return DELETE;
}

bool CRcvFreshLoss::removeOne(std::deque<CRcvFreshLoss>& w_container, int32_t sequence, int* pw_had_ttl)
{
    for (size_t i = 0; i < w_container.size(); ++i)
    {
        const int had_ttl = w_container[i].ttl;
        Emod wh = w_container[i].revoke(sequence);

        if (wh == NONE)
            continue;  // Not found. Search again.

        if (wh == DELETE)   //  ... oo ... x ... o ... => ... oo ... o ...
        {
            // Removed the only element in the record - remove the record.
            w_container.erase(w_container.begin() + i);
        }
        else if (wh == SPLIT) // ... ooxooo ... => ... oo ... ooo ...
        {
            // Create a new element that will hold the upper part of the range,
            // and the found one modify to be the lower part of the range.

            // Keep the current end-of-sequence value for the second element
            int32_t next_end = w_container[i].seq[1];

            // seq-1 set to the end of this element
            w_container[i].seq[1] = CSeqNo::decseq(sequence);
            // seq+1 set to the begin of the next element
            int32_t next_begin = CSeqNo::incseq(sequence);

            // Use position of the NEXT element because insertion happens BEFORE pointed element.
            // Use the same TTL (will stay the same in the other one).
            w_container.insert(w_container.begin() + i + 1,
                    CRcvFreshLoss(next_begin, next_end, w_container[i].ttl));
        }
        // For STRIPPED:  ... xooo ... => ... ooo ...
        // i.e. there's nothing to do.

        // Every loss is unique. We're done here.
        if (pw_had_ttl)
            *pw_had_ttl = had_ttl;

        return true;
    }

    if (pw_had_ttl)
        *pw_had_ttl = 0;
    return false;

}

}
