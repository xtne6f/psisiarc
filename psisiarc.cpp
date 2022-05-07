#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>
#include "psiextractor.hpp"
#include "util.hpp"

namespace
{
const uint32_t UNKNOWN_TIME = 0xffffffff;
const uint16_t CODE_NUMBER_BEGIN = 4096;

struct ARCHIVE_CONTEXT
{
    struct DICTIONARY_ITEM
    {
        uint16_t codeOrSize;
        uint16_t pid;
        std::vector<uint8_t> token;
    };
    std::vector<uint8_t> timeList;
    std::vector<DICTIONARY_ITEM> dict, lastDict;
    std::unordered_multimap<uint32_t, uint16_t> dictHashMap, lastDictHashMap;
    std::vector<uint8_t> codeList;
    size_t dictionaryDataSize;
    size_t dictionaryBuffSize;
    size_t dictionaryMaxBuffSize;
    uint32_t currentTime;
    uint16_t currentRelTime;
    uint16_t sameTimeCodeCount;
    uint32_t lastWriteTime;
    uint32_t writeInterval;
    FILE *fp;
};

void WriteArchive(ARCHIVE_CONTEXT &arc)
{
    if (arc.codeList.empty()) {
        return;
    }
    if (arc.sameTimeCodeCount > 0) {
        arc.timeList.push_back(static_cast<uint8_t>(arc.currentRelTime));
        arc.timeList.push_back(static_cast<uint8_t>(arc.currentRelTime >> 8));
        arc.timeList.push_back(static_cast<uint8_t>(arc.sameTimeCodeCount - 1));
        arc.timeList.push_back(static_cast<uint8_t>((arc.sameTimeCodeCount - 1) >> 8));
    }

    size_t dictionaryWindowSize = arc.dict.size();
    if (arc.writeInterval != UNKNOWN_TIME) {
        // Leave unused items in back of the dictionary
        for (auto it = arc.lastDict.cbegin(); it != arc.lastDict.end(); ++it) {
            if (!it->token.empty()) {
                // Unused item
                if (dictionaryWindowSize >= 65536 - CODE_NUMBER_BEGIN ||
                    arc.dictionaryBuffSize + 2 + it->token.size() > arc.dictionaryMaxBuffSize) {
                    break;
                }
                // Leave it
                ++dictionaryWindowSize;
                arc.dictionaryBuffSize += 2 + it->token.size();
            }
        }
    }

    uint8_t header[32] = {
        // Magic number
        0x50, 0x73, 0x73, 0x63, 0x0d, 0x0a, 0x9a, 0x0a,
        // Reserved
        0, 0,
        static_cast<uint8_t>(arc.timeList.size() / 4),
        static_cast<uint8_t>((arc.timeList.size() / 4) >> 8),
        static_cast<uint8_t>(arc.dict.size()),
        static_cast<uint8_t>(arc.dict.size() >> 8),
        static_cast<uint8_t>(dictionaryWindowSize),
        static_cast<uint8_t>(dictionaryWindowSize >> 8),
        static_cast<uint8_t>(arc.dictionaryDataSize),
        static_cast<uint8_t>(arc.dictionaryDataSize >> 8),
        static_cast<uint8_t>(arc.dictionaryDataSize >> 16),
        static_cast<uint8_t>(arc.dictionaryDataSize >> 24),
        static_cast<uint8_t>(arc.dictionaryBuffSize),
        static_cast<uint8_t>(arc.dictionaryBuffSize >> 8),
        static_cast<uint8_t>(arc.dictionaryBuffSize >> 16),
        static_cast<uint8_t>(arc.dictionaryBuffSize >> 24),
        static_cast<uint8_t>(arc.codeList.size() / 2),
        static_cast<uint8_t>((arc.codeList.size() / 2) >> 8),
        static_cast<uint8_t>((arc.codeList.size() / 2) >> 16),
        static_cast<uint8_t>((arc.codeList.size() / 2) >> 24),
        // Reserved
        0, 0, 0, 0
    };
    fwrite(header, 1, 32, arc.fp);
    fwrite(arc.timeList.data(), 1, arc.timeList.size(), arc.fp);
    for (auto it = arc.dict.cbegin(); it != arc.dict.end(); ++it) {
        uint8_t buf[] = {
            static_cast<uint8_t>(it->codeOrSize),
            static_cast<uint8_t>(it->codeOrSize >> 8)
        };
        fwrite(buf, 1, 2, arc.fp);
    }
    for (auto it = arc.dict.cbegin(); it != arc.dict.end(); ++it) {
        if (it->codeOrSize < CODE_NUMBER_BEGIN) {
            uint8_t buf[] = {
                static_cast<uint8_t>(it->pid),
                static_cast<uint8_t>(it->pid >> 8 | 0xe0)
            };
            fwrite(buf, 1, 2, arc.fp);
        }
    }
    for (auto it = arc.dict.cbegin(); it != arc.dict.end(); ++it) {
        if (it->codeOrSize < CODE_NUMBER_BEGIN) {
            fwrite(it->token.data(), 1, it->token.size(), arc.fp);
        }
    }
    if (arc.dictionaryDataSize % 2) {
        uint8_t alignment = 0xff;
        fwrite(&alignment, 1, 1, arc.fp);
    }
    fwrite(arc.codeList.data(), 1, arc.codeList.size(), arc.fp);
    uint8_t trailer[] = {0x3d, 0x3d, 0x3d, 0x3d};
    fwrite(trailer, 1, (arc.dict.size() + (arc.dictionaryDataSize + 1) / 2 + arc.codeList.size() / 2) % 2 ? 2 : 4, arc.fp);
    fflush(arc.fp);

    // Leave unused items in back of the dictionary
    for (auto it = arc.lastDict.begin(); arc.dict.size() < dictionaryWindowSize; ++it) {
        if (!it->token.empty()) {
            uint16_t dictIndex = static_cast<uint16_t>(arc.dict.size());
            uint32_t hash = it->pid;
            if (it->token.size() >= 4) {
                hash ^= it->token[it->token.size() - 4] | (it->token[it->token.size() - 3] << 8) |
                        (it->token[it->token.size() - 2] << 16) |
                        (static_cast<uint32_t>(it->token[it->token.size() - 1]) << 24);
            }
            arc.dictHashMap.emplace(hash, dictIndex);
            arc.dict.emplace_back();
            auto &item = arc.dict.back();
            item.pid = it->pid;
            item.token.swap(it->token);
        }
    }

    arc.timeList.clear();
    arc.dict.swap(arc.lastDict);
    arc.dictHashMap.swap(arc.lastDictHashMap);
    arc.dict.clear();
    arc.dictHashMap.clear();
    arc.codeList.clear();
    arc.dictionaryDataSize = 0;
    arc.dictionaryBuffSize = 0;
    arc.currentTime = UNKNOWN_TIME;
    arc.currentRelTime = 0;
    arc.sameTimeCodeCount = 0;
    arc.lastWriteTime = UNKNOWN_TIME;
}

void AddToTimeList(ARCHIVE_CONTEXT &arc, uint32_t pcr11khz)
{
    bool setAbsoluteTime = arc.currentTime == UNKNOWN_TIME ? pcr11khz != UNKNOWN_TIME :
        pcr11khz == UNKNOWN_TIME || ((0x40000000 + pcr11khz - arc.currentTime) & 0x3fffffff) > 0xffff;

    if (arc.sameTimeCodeCount > 0x7fff || (arc.sameTimeCodeCount > 0 && (setAbsoluteTime || pcr11khz != arc.currentTime))) {
        arc.timeList.push_back(static_cast<uint8_t>(arc.currentRelTime));
        arc.timeList.push_back(static_cast<uint8_t>(arc.currentRelTime >> 8));
        arc.timeList.push_back(static_cast<uint8_t>(arc.sameTimeCodeCount - 1));
        arc.timeList.push_back(static_cast<uint8_t>((arc.sameTimeCodeCount - 1) >> 8));
        arc.sameTimeCodeCount = 0;
        arc.currentRelTime = static_cast<uint16_t>(setAbsoluteTime || pcr11khz == arc.currentTime ? 0 :
                                                       ((0x40000000 + pcr11khz - arc.currentTime) & 0x3fffffff));
    }
    ++arc.sameTimeCodeCount;
    arc.currentTime = pcr11khz;

    if (setAbsoluteTime) {
        arc.timeList.push_back(static_cast<uint8_t>(arc.currentTime));
        arc.timeList.push_back(static_cast<uint8_t>(arc.currentTime >> 8));
        arc.timeList.push_back(static_cast<uint8_t>(arc.currentTime >> 16));
        arc.timeList.push_back(static_cast<uint8_t>(arc.currentTime >> 24 | 0x80));
    }
}

void AddToArchive(ARCHIVE_CONTEXT &arc, int pid, int64_t pcr, size_t psiSize, const uint8_t *psi)
{
    if (psiSize == 0) {
        return;
    }
    if (arc.lastWriteTime == UNKNOWN_TIME) {
        arc.lastWriteTime = arc.currentTime;
    }
    if (arc.timeList.size() / 4 >= 65536 - 4 ||
        arc.dict.size() >= 65536 - CODE_NUMBER_BEGIN ||
        arc.dictionaryBuffSize + 2 + 4096 > arc.dictionaryMaxBuffSize ||
        (arc.currentTime != UNKNOWN_TIME &&
         arc.lastWriteTime != UNKNOWN_TIME &&
         ((0x40000000 + arc.currentTime - arc.lastWriteTime) & 0x3fffffff) >= arc.writeInterval))
    {
        WriteArchive(arc);
    }
    AddToTimeList(arc, pcr < 0 ? UNKNOWN_TIME : static_cast<uint32_t>(pcr >> 3));

    uint32_t hash = pid;
    if (psiSize >= 4) {
        hash ^= psi[psiSize - 4] | (psi[psiSize - 3] << 8) | (psi[psiSize - 2] << 16) |
                (static_cast<uint32_t>(psi[psiSize - 1]) << 24);
    }
    auto eqRange = arc.dictHashMap.equal_range(hash);
    for (; eqRange.first != eqRange.second; ++eqRange.first) {
        if (arc.dict[eqRange.first->second].token.size() == psiSize &&
            arc.dict[eqRange.first->second].pid == pid &&
            std::equal(psi, psi + psiSize, arc.dict[eqRange.first->second].token.begin())) {
            break;
        }
    }

    uint16_t dictIndex;
    if (eqRange.first == eqRange.second) {
        eqRange = arc.lastDictHashMap.equal_range(hash);
        for (; eqRange.first != eqRange.second; ++eqRange.first) {
            if (arc.lastDict[eqRange.first->second].token.size() == psiSize &&
                arc.lastDict[eqRange.first->second].pid == pid &&
                std::equal(psi, psi + psiSize, arc.lastDict[eqRange.first->second].token.begin())) {
                break;
            }
        }
        dictIndex = static_cast<uint16_t>(arc.dict.size());
        arc.dictHashMap.emplace(hash, dictIndex);
        arc.dict.emplace_back();
        auto &item = arc.dict.back();
        if (eqRange.first == eqRange.second) {
            item.codeOrSize = static_cast<uint16_t>(psiSize - 1);
            item.token.assign(psi, psi + psiSize);
            arc.dictionaryDataSize += 2 + item.token.size();
        }
        else {
            item.codeOrSize = CODE_NUMBER_BEGIN + eqRange.first->second;
            item.token.swap(arc.lastDict[eqRange.first->second].token);
        }
        item.pid = static_cast<uint16_t>(pid);
        arc.dictionaryBuffSize += 2 + item.token.size();
    }
    else {
        dictIndex = eqRange.first->second;
    }
    arc.codeList.push_back(static_cast<uint8_t>(CODE_NUMBER_BEGIN + dictIndex));
    arc.codeList.push_back(static_cast<uint8_t>((CODE_NUMBER_BEGIN + dictIndex) >> 8));
}

#ifdef _WIN32
const char *GetSmallString(const wchar_t *s)
{
    static char ss[32];
    size_t i = 0;
    for (; i < sizeof(ss) - 1 && s[i]; ++i) {
        ss[i] = 0 < s[i] && s[i] <= 127 ? static_cast<char>(s[i]) : '?';
    }
    ss[i] = '\0';
    return ss;
}
#else
const char *GetSmallString(const char *s)
{
    return s;
}
#endif
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
#else
int main(int argc, char **argv)
#endif
{
    ARCHIVE_CONTEXT arc;
    arc.dictionaryMaxBuffSize = 16 * 1024 * 1024;
    arc.writeInterval = UNKNOWN_TIME;
    CPsiExtractor psiExtractor;
#ifdef _WIN32
    const wchar_t *srcName = L"";
    const wchar_t *destName = L"";
#else
    const char *srcName = "";
    const char *destName = "";
#endif

    for (int i = 1; i < argc; ++i) {
        char c = '\0';
        const char *ss = GetSmallString(argv[i]);
        if (ss[0] == '-' && ss[1] && !ss[2]) {
            c = ss[1];
        }
        if (c == 'h') {
            fprintf(stderr, "Usage: psisiarc [-p pids][-n prog_num_or_index][-t stream_types][-r preset][-i interval][-b maxbuf_kbytes] src dest\n");
            return 2;
        }
        bool invalid = false;
        if (i < argc - 2) {
            if (c == 'p') {
                ++i;
                for (size_t j = 0; argv[i][j];) {
                    ss = GetSmallString(argv[i] + j);
                    char *endp;
                    int pid = static_cast<int>(strtol(ss, &endp, 10));
                    psiExtractor.AddTargetPid(pid);
                    invalid = !(0 <= pid && pid <= 8191 && ss != endp && (!*endp || *endp == '/'));
                    if (invalid || !*endp) {
                        break;
                    }
                    j += endp - ss + 1;
                }
            }
            else if (c == 'n') {
                int n = static_cast<int>(strtol(GetSmallString(argv[++i]), nullptr, 10));
                invalid = !(-256 <= n && n <= 65535);
                psiExtractor.SetProgramNumberOrIndex(n);
            }
            else if (c == 't') {
                ++i;
                for (size_t j = 0; argv[i][j];) {
                    ss = GetSmallString(argv[i] + j);
                    char *endp;
                    int streamType = static_cast<int>(strtol(ss, &endp, 10));
                    psiExtractor.AddTargetStreamType(streamType);
                    invalid = !(0 <= streamType && streamType <= 255 && ss != endp && (!*endp || *endp == '/'));
                    if (invalid || !*endp) {
                        break;
                    }
                    j += endp - ss + 1;
                }
            }
            else if (c == 'r') {
                ++i;
                bool isAribData = strcmp(GetSmallString(argv[i]), "arib-data") == 0;
                bool isAribEpg = strcmp(GetSmallString(argv[i]), "arib-epg") == 0;
                if (isAribData || isAribEpg) {
                    psiExtractor.SetProgramNumberOrIndex(-1);
                    psiExtractor.AddTargetPid(17);
                    psiExtractor.AddTargetPid(18);
                    psiExtractor.AddTargetPid(20);
                    psiExtractor.AddTargetPid(31);
                    psiExtractor.AddTargetPid(36);
                    if (isAribData) {
                        psiExtractor.AddTargetStreamType(11);
                        psiExtractor.AddTargetStreamType(12);
                        psiExtractor.AddTargetStreamType(13);
                    }
                }
                invalid = !isAribData && !isAribEpg;
            }
            else if (c == 'i') {
                arc.writeInterval = static_cast<uint32_t>(strtol(GetSmallString(argv[++i]), nullptr, 10) * 11250);
                invalid = arc.writeInterval > 600 * 11250;
                if (arc.writeInterval == 0) {
                    arc.writeInterval = UNKNOWN_TIME;
                }
            }
            else if (c == 'b') {
                arc.dictionaryMaxBuffSize = static_cast<size_t>(strtol(GetSmallString(argv[++i]), nullptr, 10) * 1024);
                invalid = arc.dictionaryMaxBuffSize < 8 * 1024 || 1024 * 1024 * 1024 < arc.dictionaryMaxBuffSize;
            }
        }
        else if (i < argc - 1) {
            srcName = argv[i];
            invalid = !srcName[0];
        }
        else {
            destName = argv[i];
            invalid = !destName[0];
        }
        if (invalid) {
            fprintf(stderr, "Error: argument %d is invalid.\n", i);
            return 1;
        }
    }
    if (!srcName[0] || !destName[0]) {
        fprintf(stderr, "Error: not enough arguments.\n");
        return 1;
    }

    std::unique_ptr<FILE, decltype(&fclose)> srcFile(nullptr, fclose);
    std::unique_ptr<FILE, decltype(&fclose)> destFile(nullptr, fclose);

#ifdef _WIN32
    if (srcName[0] != L'-' || srcName[1]) {
        srcFile.reset(_wfopen(srcName, L"rbS"));
        if (!srcFile) {
            fprintf(stderr, "Error: cannot open file.\n");
            return 1;
        }
    }
    else if (_setmode(_fileno(stdin), _O_BINARY) < 0) {
        fprintf(stderr, "Error: _setmode.\n");
        return 1;
    }
    if (destName[0] != L'-' || destName[1]) {
        destFile.reset(_wfopen(destName, L"wb"));
        if (!destFile) {
            fprintf(stderr, "Error: cannot create file.\n");
            return 1;
        }
    }
    else if (_setmode(_fileno(stdout), _O_BINARY) < 0) {
        fprintf(stderr, "Error: _setmode.\n");
        return 1;
    }
#else
    if (srcName[0] != '-' || srcName[1]) {
        srcFile.reset(fopen(srcName, "r"));
        if (!srcFile) {
            fprintf(stderr, "Error: cannot open file.\n");
            return 1;
        }
    }
    if (destName[0] != '-' || destName[1]) {
        destFile.reset(fopen(destName, "w"));
        if (!destFile) {
            fprintf(stderr, "Error: cannot create file.\n");
            return 1;
        }
    }
#endif

    arc.dictionaryDataSize = 0;
    arc.dictionaryBuffSize = 0;
    arc.currentTime = UNKNOWN_TIME;
    arc.currentRelTime = 0;
    arc.sameTimeCodeCount = 0;
    arc.lastWriteTime = UNKNOWN_TIME;
    arc.fp = destFile ? destFile.get() : stdout;
    FILE *fpSrc = srcFile ? srcFile.get() : stdin;

    static uint8_t buf[65536];
    int bufCount = 0;
    int unitSize = 0;
    for (;;) {
        int n = static_cast<int>(fread(buf + bufCount, 1, sizeof(buf) - bufCount, fpSrc));
        bufCount += n;
        if (bufCount == sizeof(buf) || n == 0) {
            int bufPos = resync_ts(buf, bufCount, &unitSize);
            for (int i = bufPos; i + 188 <= bufCount; i += unitSize) {
                psiExtractor.AddPacket(buf + i, [&arc](int pid, int64_t pcr, size_t psiSize, const uint8_t *psi) {
                    AddToArchive(arc, pid, pcr, psiSize, psi);
                });
            }
            if (n == 0) {
                break;
            }
            if ((bufPos != 0 || bufCount >= 188) && (bufCount - bufPos) % 188 != 0) {
                std::copy(buf + bufPos + (bufCount - bufPos) / 188 * 188, buf + bufCount, buf);
            }
            bufCount = (bufCount - bufPos) % 188;
        }
    }
    WriteArchive(arc);
    return 0;
}
