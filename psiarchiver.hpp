#ifndef INCLUDE_PSIARCHIVER_HPP
#define INCLUDE_PSIARCHIVER_HPP

#include <stdint.h>
#include <stdio.h>
#include <unordered_map>
#include <vector>

class CPsiArchiver
{
public:
    CPsiArchiver();
    void SetFile(FILE *fp) { m_fp = fp; }
    void SetWriteInterval(uint32_t interval);
    void SetDictionaryMaxBuffSize(size_t size);
    void Add(int pid, int64_t pcr, size_t psiSize, const uint8_t *psi);
    void Flush(bool suppressTrailer = false);

private:
    struct DICTIONARY_ITEM
    {
        uint16_t codeOrSize;
        uint16_t pid;
        std::vector<uint8_t> token;
    };
    void AddToTimeList(uint32_t pcr11khz);

    static const uint32_t UNKNOWN_TIME = 0xffffffff;
    static const uint16_t CODE_NUMBER_BEGIN = 4096;
    std::vector<uint8_t> m_timeList;
    std::vector<DICTIONARY_ITEM> m_dict, m_lastDict;
    std::unordered_multimap<uint32_t, uint16_t> m_dictHashMap, m_lastDictHashMap;
    std::vector<uint8_t> m_codeList;
    size_t m_dictionaryDataSize;
    size_t m_dictionaryBuffSize;
    size_t m_dictionaryMaxBuffSize;
    uint32_t m_currentTime;
    uint16_t m_currentRelTime;
    uint16_t m_sameTimeCodeCount;
    uint32_t m_lastWriteTime;
    uint32_t m_writeInterval;
    size_t m_trailerSize;
    FILE *m_fp;
};

#endif
