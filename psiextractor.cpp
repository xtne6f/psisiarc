#include "psiextractor.hpp"
#include <algorithm>

CPsiExtractor::CPsiExtractor()
    : m_programNumberOrIndex(0)
    , m_nitPid(0)
    , m_pcrPid(0)
    , m_pcr(-1)
{
    static const PAT zeroPat = {};
    m_pat = zeroPat;
    m_pmtPsi = zeroPat.psi;
}

void CPsiExtractor::AddTargetPid(int pid)
{
    static const PSI_SI zeroPsiSi = {};
    m_targetPsiSiMap[pid] = zeroPsiSi;
    m_targetPsiSiMap[pid].specified = true;
}

void CPsiExtractor::AddTargetStreamType(int streamType)
{
    m_targetStreamTypes.insert(streamType);
}

void CPsiExtractor::AddPacket(const uint8_t *packet, const std::function<void (int, int64_t, size_t, const uint8_t *)> &onExtract)
{
    int unitStart = extract_ts_header_unit_start(packet);
    int pid = extract_ts_header_pid(packet);
    int adaptation = extract_ts_header_adaptation(packet);
    int counter = extract_ts_header_counter(packet);
    int payloadSize = get_ts_payload_size(packet);
    const uint8_t *payload = packet + 188 - payloadSize;

    if (pid == 0 && m_programNumberOrIndex != 0) {
        extract_pat(&m_pat, payload, payloadSize, unitStart, counter);
        auto itPmt = FindTargetPmtRef(m_pat.pmt);
        if (itPmt != m_pat.pmt.end()) {
            if (unitStart) {
                auto itNit = FindNitRef(m_pat.pmt);
                AddPat(m_pat.transport_stream_id, itPmt->program_number, itPmt->pmt_pid,
                       itNit != m_pat.pmt.end() ? itNit->pmt_pid : 0, onExtract);
            }
        }
        else {
            m_pcrPid = 0;
            m_pcr = -1;
        }
    }
    else {
        if (m_programNumberOrIndex != 0) {
            auto itPmt = FindTargetPmtRef(m_pat.pmt);
            if (itPmt != m_pat.pmt.end()) {
                if (pid == itPmt->pmt_pid) {
                    int done;
                    do {
                        done = extract_psi(&m_pmtPsi, payload, payloadSize, unitStart, counter);
                        if (m_pmtPsi.version_number && m_pmtPsi.table_id == 2 && m_pmtPsi.current_next_indicator) {
                            AddPmt(m_pmtPsi, pid, onExtract);
                        }
                    }
                    while (!done);
                }
                if (pid == m_pcrPid) {
                    if (adaptation & 2) {
                        int adaptationLength = packet[4];
                        if (adaptationLength >= 6 && !!(packet[5] & 0x10)) {
                            m_pcr = (packet[10] >> 7) |
                                    (packet[9] << 1) |
                                    (packet[8] << 9) |
                                    (packet[7] << 17) |
                                    (static_cast<int64_t>(packet[6]) << 25);
                        }
                    }
                }
            }
        }
        auto it = m_targetPsiSiMap.find(pid);
        if (it != m_targetPsiSiMap.end()) {
            ExtractPsiSi(it->second, payload, payloadSize, unitStart, counter, [this, pid, &onExtract](int dataSize, const uint8_t *data) {
                onExtract(pid, m_pcr, dataSize, data);
            });
        }
    }
}

std::vector<PMT_REF>::const_iterator CPsiExtractor::FindNitRef(const std::vector<PMT_REF> &pmt)
{
    return std::find_if(pmt.begin(), pmt.end(), [](const PMT_REF &a) { return a.program_number == 0; });
}

std::vector<PMT_REF>::const_iterator CPsiExtractor::FindTargetPmtRef(const std::vector<PMT_REF> &pmt) const
{
    if (m_programNumberOrIndex < 0) {
        int index = -m_programNumberOrIndex;
        for (auto it = pmt.begin(); it != pmt.end(); ++it) {
            if (it->program_number != 0) {
                if (--index == 0) {
                    return it;
                }
            }
        }
        return pmt.end();
    }
    return std::find_if(pmt.begin(), pmt.end(), [=](const PMT_REF &a) { return a.program_number == m_programNumberOrIndex; });
}

void CPsiExtractor::AddPat(int transportStreamID, int programNumber, int pmtPid, int nitPid,
                           const std::function<void (int, int64_t, size_t, const uint8_t *)> &onExtract)
{
    // Create PAT
    uint8_t buf[20];
    buf[0] = 0x00;
    buf[1] = 0xb0;
    buf[2] = nitPid != 0 ? 17 : 13;
    buf[3] = static_cast<uint8_t>(transportStreamID >> 8);
    buf[4] = static_cast<uint8_t>(transportStreamID);
    buf[5] = m_lastPat.size() > 5 ? m_lastPat[5] : 0xc1;
    buf[6] = 0;
    buf[7] = 0;
    size_t bufLen = 8;
    if (m_nitPid != nitPid) {
        m_targetPsiSiMap.erase(m_nitPid);
        if (nitPid != 0) {
            static const PSI_SI zeroPsiSi = {};
            m_targetPsiSiMap[nitPid] = zeroPsiSi;
            m_targetPsiSiMap[nitPid].specified = true;
        }
        m_nitPid = nitPid;
    }
    if (nitPid != 0) {
        buf[bufLen++] = 0;
        buf[bufLen++] = 0;
        buf[bufLen++] = 0xe0 | static_cast<uint8_t>(nitPid >> 8);
        buf[bufLen++] = static_cast<uint8_t>(nitPid);
    }
    buf[bufLen++] = static_cast<uint8_t>(programNumber >> 8);
    buf[bufLen++] = static_cast<uint8_t>(programNumber);
    buf[bufLen++] = 0xe0 | static_cast<uint8_t>(pmtPid >> 8);
    buf[bufLen++] = static_cast<uint8_t>(pmtPid);
    if (m_lastPat.size() == bufLen + 4 &&
        std::equal(buf, buf + bufLen, m_lastPat.begin())) {
        // Copy CRC
        std::copy(m_lastPat.end() - 4, m_lastPat.end(), buf + bufLen);
        bufLen += 4;
    }
    else {
        // Increment version number
        buf[5] = 0xc1 | (((buf[5] >> 1) + 1) & 0x1f) << 1;
        uint32_t crc = calc_crc32(buf, static_cast<int>(bufLen));
        buf[bufLen++] = crc >> 24;
        buf[bufLen++] = (crc >> 16) & 0xff;
        buf[bufLen++] = (crc >> 8) & 0xff;
        buf[bufLen++] = crc & 0xff;
        m_lastPat.assign(buf, buf + bufLen);
    }

    onExtract(0, m_pcr, bufLen, buf);
}

void CPsiExtractor::AddPmt(const PSI &psi, int pid, const std::function<void (int, int64_t, size_t, const uint8_t *)> &onExtract)
{
    if (psi.section_length < 9) {
        return;
    }
    const uint8_t *table = psi.data;
    m_pcrPid = ((table[8] & 0x1f) << 8) | table[9];
    if (m_pcrPid == 0x1fff) {
        m_pcr = -1;
    }
    int programInfoLength = ((table[10] & 0x03) << 8) | table[11];
    int pos = 3 + 9 + programInfoLength;
    if (3 + psi.section_length < pos) {
        return;
    }

    // Create PMT
    uint8_t buf[1024];
    buf[0] = 0x02;
    buf[3] = table[3];
    buf[4] = table[4];
    buf[5] = m_lastPmt.size() > 5 ? m_lastPmt[5] : 0xc1;
    buf[6] = 0;
    buf[7] = 0;
    // PCR_PID=0x1fff (no pcr)
    buf[8] = 0xff;
    buf[9] = 0xff;
    buf[10] = table[10];
    buf[11] = table[11];
    // Copy 1st descriptor loop
    std::copy(table + 12, table + pos, buf + 12);
    size_t bufLen = pos;

    int tableLen = 3 + psi.section_length - 4/*CRC32*/;
    while (pos + 4 < tableLen) {
        uint8_t streamType = table[pos];
        int esPid = ((table[pos + 1] & 0x1f) << 8) | table[pos + 2];
        int esInfoLength = ((table[pos + 3] & 0x03) << 8) | table[pos + 4];
        if (pos + 5 + esInfoLength <= tableLen) {
            if (m_targetStreamTypes.count(streamType)) {
                // Copy 2nd descriptor
                std::copy(table + pos, table + pos + 5 + esInfoLength, buf + bufLen);
                bufLen += 5 + esInfoLength;
                auto it = m_targetPsiSiMap.find(esPid);
                if (it == m_targetPsiSiMap.end()) {
                    static const PSI_SI zeroPsiSi = {};
                    it = m_targetPsiSiMap.emplace(esPid, zeroPsiSi).first;
                }
                it->second.existsOnPmt = true;
            }
        }
        pos += 5 + esInfoLength;
    }

    // Unmap
    for (auto it = m_targetPsiSiMap.begin(); it != m_targetPsiSiMap.end(); ) {
        if (!it->second.specified && !it->second.existsOnPmt) {
            it = m_targetPsiSiMap.erase(it);
        }
        else {
            (it++)->second.existsOnPmt = false;
        }
    }

    buf[1] = 0xb0 | static_cast<uint8_t>((bufLen + 4 - 3) >> 8);
    buf[2] = static_cast<uint8_t>(bufLen + 4 - 3);

    if (m_lastPmt.size() == bufLen + 4 &&
        std::equal(buf, buf + bufLen, m_lastPmt.begin())) {
        // Copy CRC
        std::copy(m_lastPmt.end() - 4, m_lastPmt.end(), buf + bufLen);
        bufLen += 4;
    }
    else {
        // Increment version number
        buf[5] = 0xc1 | (((buf[5] >> 1) + 1) & 0x1f) << 1;
        uint32_t crc = calc_crc32(buf, static_cast<int>(bufLen));
        buf[bufLen++] = crc >> 24;
        buf[bufLen++] = (crc >> 16) & 0xff;
        buf[bufLen++] = (crc >> 8) & 0xff;
        buf[bufLen++] = crc & 0xff;
        m_lastPmt.assign(buf, buf + bufLen);
    }

    onExtract(pid, m_pcr, bufLen, buf);
}

void CPsiExtractor::ExtractPsiSi(PSI_SI &psiSi, const uint8_t *payload, int payloadSize, int unitStart, int counter,
                                 const std::function<void (int, const uint8_t *)> &onExtract)
{
    int copyPos = 0;
    if (unitStart) {
        if (payloadSize < 1) {
            psiSi.continuityCounter = psiSi.dataCount = 0;
            return;
        }
        int pointer = payload[0];
        psiSi.continuityCounter = (psiSi.continuityCounter + 1) & 0x2f;
        if (pointer > 0 && psiSi.continuityCounter == (0x20 | counter)) {
            copyPos = 1;
            if (copyPos + pointer <= payloadSize) {
                int copySize = std::min(pointer, static_cast<int>(sizeof(psiSi.data)) - psiSi.dataCount);
                std::copy(payload + copyPos, payload + copyPos + copySize, psiSi.data + psiSi.dataCount);
                psiSi.dataCount += copySize;
            }

            if (psiSi.dataCount >= 3 && psiSi.data[0] != 0xff) {
                // Non-stuffing section
                int sectionLength = ((psiSi.data[1] & 0x0f) << 8) | psiSi.data[2];
                if (psiSi.dataCount >= 3 + sectionLength) {
                    onExtract(3 + sectionLength, psiSi.data);
                }
            }
        }
        psiSi.continuityCounter = 0x20 | counter;
        psiSi.dataCount = 0;
        copyPos = 1 + pointer;
    }
    else {
        if (payloadSize < 1) {
            // counter is non-incrementing
            return;
        }
        psiSi.continuityCounter = (psiSi.continuityCounter + 1) & 0x2f;
        if (psiSi.continuityCounter != (0x20 | counter)) {
            psiSi.continuityCounter = psiSi.dataCount = 0;
            return;
        }
    }

    for (;;) {
        if (copyPos < payloadSize) {
            int copySize = std::min(payloadSize - copyPos, static_cast<int>(sizeof(psiSi.data)) - psiSi.dataCount);
            std::copy(payload + copyPos, payload + copyPos + copySize, psiSi.data + psiSi.dataCount);
            psiSi.dataCount += copySize;
            copyPos += copySize;
        }
        if (psiSi.dataCount < 3 || psiSi.data[0] == 0xff) {
            break;
        }
        // Non-stuffing section
        int sectionLength = ((psiSi.data[1] & 0x0f) << 8) | psiSi.data[2];
        if (psiSi.dataCount < 3 + sectionLength) {
            break;
        }
        onExtract(3 + sectionLength, psiSi.data);
        std::copy(psiSi.data + 3 + sectionLength, psiSi.data + psiSi.dataCount, psiSi.data);
        psiSi.dataCount -= 3 + sectionLength;
    }
}
