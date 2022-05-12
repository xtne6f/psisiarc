#include "psiarchiver.hpp"
#include <algorithm>

CPsiArchiver::CPsiArchiver()
    : m_dictionaryDataSize(0)
    , m_dictionaryBuffSize(0)
    , m_dictionaryMaxBuffSize(16 * 1024 * 1024)
    , m_currentTime(UNKNOWN_TIME)
    , m_currentRelTime(0)
    , m_sameTimeCodeCount(0)
    , m_lastWriteTime(UNKNOWN_TIME)
    , m_writeInterval(UNKNOWN_TIME)
    , m_trailerSize(0)
    , m_fp(nullptr)
{
}

void CPsiArchiver::SetWriteInterval(uint32_t interval)
{
    m_writeInterval = interval == 0 ? UNKNOWN_TIME : interval;
}

void CPsiArchiver::SetDictionaryMaxBuffSize(size_t size)
{
    m_dictionaryMaxBuffSize = std::min<size_t>(std::max<size_t>(size, 8 * 1024), 1024 * 1024 * 1024);
}

void CPsiArchiver::Add(int pid, int64_t pcr, size_t psiSize, const uint8_t *psi)
{
    if (psiSize == 0) {
        return;
    }
    if (m_lastWriteTime == UNKNOWN_TIME) {
        m_lastWriteTime = m_currentTime;
    }
    if (m_timeList.size() / 4 >= 65536 - 4 ||
        m_dict.size() >= 65536 - CODE_NUMBER_BEGIN ||
        m_dictionaryBuffSize + 2 + 4096 > m_dictionaryMaxBuffSize ||
        (m_currentTime != UNKNOWN_TIME &&
         m_lastWriteTime != UNKNOWN_TIME &&
         ((0x40000000 + m_currentTime - m_lastWriteTime) & 0x3fffffff) >= m_writeInterval))
    {
        Flush(true);
    }
    AddToTimeList(pcr < 0 ? UNKNOWN_TIME : static_cast<uint32_t>(pcr >> 3));

    uint32_t hash = pid;
    if (psiSize >= 4) {
        hash ^= psi[psiSize - 4] | (psi[psiSize - 3] << 8) | (psi[psiSize - 2] << 16) |
                (static_cast<uint32_t>(psi[psiSize - 1]) << 24);
    }
    auto eqRange = m_dictHashMap.equal_range(hash);
    for (; eqRange.first != eqRange.second; ++eqRange.first) {
        if (m_dict[eqRange.first->second].token.size() == psiSize &&
            m_dict[eqRange.first->second].pid == pid &&
            std::equal(psi, psi + psiSize, m_dict[eqRange.first->second].token.begin())) {
            break;
        }
    }

    uint16_t dictIndex;
    if (eqRange.first == eqRange.second) {
        eqRange = m_lastDictHashMap.equal_range(hash);
        for (; eqRange.first != eqRange.second; ++eqRange.first) {
            if (m_lastDict[eqRange.first->second].token.size() == psiSize &&
                m_lastDict[eqRange.first->second].pid == pid &&
                std::equal(psi, psi + psiSize, m_lastDict[eqRange.first->second].token.begin())) {
                break;
            }
        }
        dictIndex = static_cast<uint16_t>(m_dict.size());
        m_dictHashMap.emplace(hash, dictIndex);
        m_dict.emplace_back();
        auto &item = m_dict.back();
        if (eqRange.first == eqRange.second) {
            item.codeOrSize = static_cast<uint16_t>(psiSize - 1);
            item.token.assign(psi, psi + psiSize);
            m_dictionaryDataSize += 2 + item.token.size();
        }
        else {
            item.codeOrSize = CODE_NUMBER_BEGIN + eqRange.first->second;
            item.token.swap(m_lastDict[eqRange.first->second].token);
        }
        item.pid = static_cast<uint16_t>(pid);
        m_dictionaryBuffSize += 2 + item.token.size();
    }
    else {
        dictIndex = eqRange.first->second;
    }
    m_codeList.push_back(static_cast<uint8_t>(CODE_NUMBER_BEGIN + dictIndex));
    m_codeList.push_back(static_cast<uint8_t>((CODE_NUMBER_BEGIN + dictIndex) >> 8));
}

void CPsiArchiver::Flush(bool suppressTrailer)
{
    uint8_t trailer[] = {0x3d, 0x3d, 0x3d, 0x3d};
    if (m_codeList.empty()) {
        if (!suppressTrailer && m_fp && m_trailerSize > 0) {
            // Write a pending trailer
            fwrite(trailer, 1, m_trailerSize, m_fp);
            m_trailerSize = 0;
            fflush(m_fp);
        }
        return;
    }
    if (m_sameTimeCodeCount > 0) {
        m_timeList.push_back(static_cast<uint8_t>(m_currentRelTime));
        m_timeList.push_back(static_cast<uint8_t>(m_currentRelTime >> 8));
        m_timeList.push_back(static_cast<uint8_t>(m_sameTimeCodeCount - 1));
        m_timeList.push_back(static_cast<uint8_t>((m_sameTimeCodeCount - 1) >> 8));
    }

    size_t dictionaryWindowSize = m_dict.size();
    if (m_writeInterval != UNKNOWN_TIME) {
        // Leave unused items in back of the dictionary
        for (auto it = m_lastDict.cbegin(); it != m_lastDict.end(); ++it) {
            if (!it->token.empty()) {
                // Unused item
                if (dictionaryWindowSize >= 65536 - CODE_NUMBER_BEGIN ||
                    m_dictionaryBuffSize + 2 + it->token.size() > m_dictionaryMaxBuffSize) {
                    break;
                }
                // Leave it
                ++dictionaryWindowSize;
                m_dictionaryBuffSize += 2 + it->token.size();
            }
        }
    }

    if (m_fp) {
        if (m_trailerSize > 0) {
            // Write a pending trailer
            fwrite(trailer, 1, m_trailerSize, m_fp);
        }
        uint8_t header[32] = {
            // Magic number
            0x50, 0x73, 0x73, 0x63, 0x0d, 0x0a, 0x9a, 0x0a,
            // Reserved
            0, 0,
            static_cast<uint8_t>(m_timeList.size() / 4),
            static_cast<uint8_t>((m_timeList.size() / 4) >> 8),
            static_cast<uint8_t>(m_dict.size()),
            static_cast<uint8_t>(m_dict.size() >> 8),
            static_cast<uint8_t>(dictionaryWindowSize),
            static_cast<uint8_t>(dictionaryWindowSize >> 8),
            static_cast<uint8_t>(m_dictionaryDataSize),
            static_cast<uint8_t>(m_dictionaryDataSize >> 8),
            static_cast<uint8_t>(m_dictionaryDataSize >> 16),
            static_cast<uint8_t>(m_dictionaryDataSize >> 24),
            static_cast<uint8_t>(m_dictionaryBuffSize),
            static_cast<uint8_t>(m_dictionaryBuffSize >> 8),
            static_cast<uint8_t>(m_dictionaryBuffSize >> 16),
            static_cast<uint8_t>(m_dictionaryBuffSize >> 24),
            static_cast<uint8_t>(m_codeList.size() / 2),
            static_cast<uint8_t>((m_codeList.size() / 2) >> 8),
            static_cast<uint8_t>((m_codeList.size() / 2) >> 16),
            static_cast<uint8_t>((m_codeList.size() / 2) >> 24),
            // Reserved
            0, 0, 0, 0
        };
        fwrite(header, 1, 32, m_fp);
        fwrite(m_timeList.data(), 1, m_timeList.size(), m_fp);
        for (auto it = m_dict.cbegin(); it != m_dict.end(); ++it) {
            uint8_t buf[] = {
                static_cast<uint8_t>(it->codeOrSize),
                static_cast<uint8_t>(it->codeOrSize >> 8)
            };
            fwrite(buf, 1, 2, m_fp);
        }
        for (auto it = m_dict.cbegin(); it != m_dict.end(); ++it) {
            if (it->codeOrSize < CODE_NUMBER_BEGIN) {
                uint8_t buf[] = {
                    static_cast<uint8_t>(it->pid),
                    static_cast<uint8_t>(it->pid >> 8 | 0xe0)
                };
                fwrite(buf, 1, 2, m_fp);
            }
        }
        for (auto it = m_dict.cbegin(); it != m_dict.end(); ++it) {
            if (it->codeOrSize < CODE_NUMBER_BEGIN) {
                fwrite(it->token.data(), 1, it->token.size(), m_fp);
            }
        }
        if (m_dictionaryDataSize % 2) {
            uint8_t alignment = 0xff;
            fwrite(&alignment, 1, 1, m_fp);
        }
        fwrite(m_codeList.data(), 1, m_codeList.size(), m_fp);

        m_trailerSize = (m_dict.size() + (m_dictionaryDataSize + 1) / 2 + m_codeList.size() / 2) % 2 ? 2 : 4;
        if (!suppressTrailer) {
            fwrite(trailer, 1, m_trailerSize, m_fp);
            m_trailerSize = 0;
        }
        fflush(m_fp);
    }

    // Leave unused items in back of the dictionary
    for (auto it = m_lastDict.begin(); m_dict.size() < dictionaryWindowSize; ++it) {
        if (!it->token.empty()) {
            uint16_t dictIndex = static_cast<uint16_t>(m_dict.size());
            uint32_t hash = it->pid;
            if (it->token.size() >= 4) {
                hash ^= it->token[it->token.size() - 4] | (it->token[it->token.size() - 3] << 8) |
                        (it->token[it->token.size() - 2] << 16) |
                        (static_cast<uint32_t>(it->token[it->token.size() - 1]) << 24);
            }
            m_dictHashMap.emplace(hash, dictIndex);
            m_dict.emplace_back();
            auto &item = m_dict.back();
            item.pid = it->pid;
            item.token.swap(it->token);
        }
    }

    m_timeList.clear();
    m_dict.swap(m_lastDict);
    m_dictHashMap.swap(m_lastDictHashMap);
    m_dict.clear();
    m_dictHashMap.clear();
    m_codeList.clear();
    m_dictionaryDataSize = 0;
    m_dictionaryBuffSize = 0;
    m_currentTime = UNKNOWN_TIME;
    m_currentRelTime = 0;
    m_sameTimeCodeCount = 0;
    m_lastWriteTime = UNKNOWN_TIME;
}

void CPsiArchiver::AddToTimeList(uint32_t pcr11khz)
{
    bool setAbsoluteTime = m_currentTime == UNKNOWN_TIME ? pcr11khz != UNKNOWN_TIME :
        pcr11khz == UNKNOWN_TIME || ((0x40000000 + pcr11khz - m_currentTime) & 0x3fffffff) > 0xffff;

    if (m_sameTimeCodeCount > 0x7fff || (m_sameTimeCodeCount > 0 && (setAbsoluteTime || pcr11khz != m_currentTime))) {
        m_timeList.push_back(static_cast<uint8_t>(m_currentRelTime));
        m_timeList.push_back(static_cast<uint8_t>(m_currentRelTime >> 8));
        m_timeList.push_back(static_cast<uint8_t>(m_sameTimeCodeCount - 1));
        m_timeList.push_back(static_cast<uint8_t>((m_sameTimeCodeCount - 1) >> 8));
        m_sameTimeCodeCount = 0;
        m_currentRelTime = static_cast<uint16_t>(setAbsoluteTime || pcr11khz == m_currentTime ? 0 :
                                                     ((0x40000000 + pcr11khz - m_currentTime) & 0x3fffffff));
    }
    ++m_sameTimeCodeCount;
    m_currentTime = pcr11khz;

    if (setAbsoluteTime) {
        m_timeList.push_back(static_cast<uint8_t>(m_currentTime));
        m_timeList.push_back(static_cast<uint8_t>(m_currentTime >> 8));
        m_timeList.push_back(static_cast<uint8_t>(m_currentTime >> 16));
        m_timeList.push_back(static_cast<uint8_t>(m_currentTime >> 24 | 0x80));
    }
}
