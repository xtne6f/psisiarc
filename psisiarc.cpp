#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "psiarchiver.hpp"
#include "psiextractor.hpp"
#include "util.hpp"

namespace
{
#ifdef _WIN32
std::string NativeToString(const wchar_t *s)
{
    std::string ret;
    for (; *s; ++s) {
        ret += 0 < *s && *s <= 127 ? static_cast<char>(*s) : '?';
    }
    return ret;
}
#else
std::string NativeToString(const char *s)
{
    return s;
}
#endif

bool GetLine(std::string &line, FILE *fp)
{
    line.clear();
    for (;;) {
        char buf[1024];
        bool eof = !fgets(buf, sizeof(buf), fp);
        if (!eof) {
            line += buf;
            if (line.empty() || line.back() != '\n') {
                continue;
            }
        }
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        return eof;
    }
}

bool IsMatchChapterNamePattern(const std::string &s, const std::string &pattern)
{
    if (pattern[0] == '^') {
        if (pattern.back() == '$') {
            // exact
            return pattern.compare(1, pattern.size() - 2, s) == 0;
        }
        // forward
        return s.size() >= pattern.size() - 1 &&
               pattern.compare(1, pattern.size() - 1, s, 0, pattern.size() - 1) == 0;
    }
    if (!pattern.empty() && pattern.back() == '$') {
        // backward
        return s.size() >= pattern.size() - 1 &&
               pattern.compare(0, pattern.size() - 1, s, s.size() - (pattern.size() - 1)) == 0;
    }
    // partial
    return s.find(pattern) != std::string::npos;
}

void UnescapeHex(std::string &s)
{
    for (size_t i = 0; i + 3 < s.size(); ++i) {
        if (s[i] == '\\' && (s[i + 1] == 'X' || s[i + 1] == 'x')) {
            // "\x??"
            char c = s[i + 2];
            char d = s[i + 3];
            s.replace(i, 4, 1, static_cast<char>(((c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0') << 4) |
                                                  (d >= 'a' ? d - 'a' + 10 : d >= 'A' ? d - 'A' + 10 : d - '0')));
        }
    }
}

void ToUpper(std::string &s)
{
    for (size_t i = 0; i < s.size(); ++i) {
        if ('a' <= s[i] && s[i] <= 'z') {
            s[i] = s[i] - 'a' + 'A';
        }
    }
}

std::vector<int> CreateCutListFromOgmStyleChapter(std::string staPattern, std::string endPattern, FILE *fp)
{
    std::string line;
    std::string chapterId;
    int cutTime = 0;
    // The even elements are trim start times
    std::vector<int> cutList;

    UnescapeHex(staPattern);
    UnescapeHex(endPattern);
    ToUpper(staPattern);
    ToUpper(endPattern);
    for (;;) {
        bool eof = GetLine(line, fp);
        if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3);
        }
        ToUpper(line);
        if (!chapterId.empty() && line.compare(0, chapterId.size(), chapterId) == 0) {
            // "CHAPTER[0-9]*NAME="
            line.erase(0, chapterId.size());
            if (IsMatchChapterNamePattern(line, cutList.size() % 2 ? endPattern : staPattern)) {
                cutList.push_back(cutTime);
            }
            chapterId.clear();
        }
        else if (line.compare(0, 7, "CHAPTER") == 0) {
            size_t i = 7;
            while ('0' <= line[i] && line[i] <= '9') {
                ++i;
            }
            if (line[i] == '=') {
                chapterId.clear();
                // "CHAPTER[0-9]*=HH:MM:SS.sss"
                if (line.size() >= i + 13 && line[i + 3] == ':' && line[i + 6] == ':' && line[i + 9] == '.') {
                    cutTime = static_cast<int>(strtol(line.c_str() + i + 1, nullptr, 10) * 3600000 +
                                               strtol(line.c_str() + i + 4, nullptr, 10) * 60000 +
                                               strtol(line.c_str() + i + 7, nullptr, 10) * 1000 +
                                               strtol(line.c_str() + i + 10, nullptr, 10));
                    if (0 <= cutTime && cutTime < 360000000 && (cutList.empty() || cutList.back() <= cutTime)) {
                        chapterId.assign(line, 0, i);
                        chapterId += "NAME=";
                    }
                }
            }
        }
        if (eof) {
            break;
        }
    }
    if (cutList.size() % 2) {
        // Add an end time
        cutList.push_back(360000000);
    }

    // Erase zero-duration / join adjacent items
    for (size_t i = 0; i + 1 < cutList.size(); ) {
        if (cutList[i] == cutList[i + 1]) {
            cutList.erase(cutList.begin() + i, cutList.begin() + i + 2);
        }
        else {
            ++i;
        }
    }
    return cutList;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
#else
int main(int argc, char **argv)
#endif
{
    CPsiArchiver psiArchiver;
    CPsiExtractor psiExtractor;
    std::string staPattern = "^ix";
    std::string endPattern = "^ox";
#ifdef _WIN32
    const wchar_t *srcName = L"";
    const wchar_t *destName = L"";
    const wchar_t *chapterFileName = L"";
#else
    const char *srcName = "";
    const char *destName = "";
    const char *chapterFileName = "";
#endif

    for (int i = 1; i < argc; ++i) {
        char c = '\0';
        std::string s = NativeToString(argv[i]);
        if (s[0] == '-' && s[1] && !s[2]) {
            c = s[1];
        }
        if (c == 'h') {
            fprintf(stderr, "Usage: psisiarc [-p pids][-n prog_num_or_index][-t stream_types][-r preset][-i interval][-b maxbuf_kbytes][-c chapter][-s pattern][-e pattern] src dest\n");
            return 2;
        }
        bool invalid = false;
        if (i < argc - 2) {
            if (c == 'p') {
                s = NativeToString(argv[++i]);
                for (size_t j = 0; j < s.size();) {
                    char *endp;
                    int pid = static_cast<int>(strtol(s.c_str() + j, &endp, 10));
                    psiExtractor.AddTargetPid(pid);
                    invalid = !(0 <= pid && pid <= 8191 && s.c_str() + j != endp && (!*endp || *endp == '/'));
                    if (invalid || !*endp) {
                        break;
                    }
                    j = endp - s.c_str() + 1;
                }
            }
            else if (c == 'n') {
                int n = static_cast<int>(strtol(NativeToString(argv[++i]).c_str(), nullptr, 10));
                invalid = !(-256 <= n && n <= 65535);
                psiExtractor.SetProgramNumberOrIndex(n);
            }
            else if (c == 't') {
                s = NativeToString(argv[++i]);
                for (size_t j = 0; j < s.size();) {
                    char *endp;
                    int streamType = static_cast<int>(strtol(s.c_str() + j, &endp, 10));
                    psiExtractor.AddTargetStreamType(streamType);
                    invalid = !(0 <= streamType && streamType <= 255 && s.c_str() + j != endp && (!*endp || *endp == '/'));
                    if (invalid || !*endp) {
                        break;
                    }
                    j = endp - s.c_str() + 1;
                }
            }
            else if (c == 'r') {
                s = NativeToString(argv[++i]);
                bool isAribData = s == "arib-data";
                bool isAribEpg = s == "arib-epg";
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
                uint32_t interval = static_cast<uint32_t>(strtol(NativeToString(argv[++i]).c_str(), nullptr, 10) * 11250);
                psiArchiver.SetWriteInterval(interval);
                invalid = interval > 600 * 11250;
            }
            else if (c == 'b') {
                size_t size = static_cast<size_t>(strtol(NativeToString(argv[++i]).c_str(), nullptr, 10) * 1024);
                psiArchiver.SetDictionaryMaxBuffSize(size);
                invalid = size < 8 * 1024 || 1024 * 1024 * 1024 < size;
            }
            else if (c == 'c') {
                chapterFileName = argv[++i];
            }
            else if (c == 's') {
                staPattern = NativeToString(argv[++i]);
            }
            else if (c == 'e') {
                endPattern = NativeToString(argv[++i]);
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

    struct
    {
        bool enabled;
        int totalCutMsec;
        long long initialPcr;
        long long lastPcr;
        std::vector<int> cutList;
    } cutContext;
    cutContext.enabled = false;
    cutContext.totalCutMsec = 0;
    cutContext.initialPcr = -1;
    cutContext.lastPcr = -1;

    if (chapterFileName[0]) {
#ifdef _WIN32
        std::unique_ptr<FILE, decltype(&fclose)> chapterFile(_wfopen(chapterFileName, L"r"), fclose);
#else
        std::unique_ptr<FILE, decltype(&fclose)> chapterFile(fopen(chapterFileName, "r"), fclose);
#endif
        if (chapterFile) {
            cutContext.enabled = true;
            cutContext.cutList = CreateCutListFromOgmStyleChapter(staPattern, endPattern, chapterFile.get());
            std::reverse(cutContext.cutList.begin(), cutContext.cutList.end());
        }
        else {
            fprintf(stderr, "Error: cannot open chapterfile.\n");
            return 1;
        }
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

    psiArchiver.SetFile(destFile ? destFile.get() : stdout);
    FILE *fpSrc = srcFile ? srcFile.get() : stdin;

    static uint8_t buf[65536];
    int bufCount = 0;
    int unitSize = 0;
    for (;;) {
        int n = static_cast<int>(fread(buf + bufCount, 1, sizeof(buf) - bufCount, fpSrc));
        bufCount += n;
        if (bufCount == sizeof(buf) || n == 0) {
            int bufPos = resync_ts(buf, bufCount, &unitSize);
            for (int i = bufPos; unitSize != 0 && i + unitSize <= bufCount; i += unitSize) {
                bool writeFailed = false;
                psiExtractor.AddPacket(buf + i, [&psiArchiver, &cutContext, &writeFailed](int pid, int64_t pcr, size_t psiSize, const uint8_t *psi) {
                    if (!cutContext.enabled) {
                        writeFailed = !psiArchiver.Add(pid, pcr, psiSize, psi);
                        return;
                    }
                    if (cutContext.initialPcr < 0) {
                        cutContext.initialPcr = cutContext.lastPcr = pcr;
                    }
                    // Check if PCR is valid and not go back.
                    if (pcr < 0 || ((0x200000000 + pcr - cutContext.lastPcr) & 0x1ffffffff) >= 0x100000000) {
                        return;
                    }
                    cutContext.lastPcr = pcr;
                    int pcrMsec = static_cast<int>(((0x200000000 + pcr - cutContext.initialPcr) & 0x1ffffffff) / 90);
                    while (cutContext.cutList.size() >= 2 && cutContext.cutList[cutContext.cutList.size() - 2] <= pcrMsec) {
                        cutContext.totalCutMsec += cutContext.cutList[cutContext.cutList.size() - 2] - cutContext.cutList.back();
                        cutContext.cutList.pop_back();
                        cutContext.cutList.pop_back();
                    }
                    if (cutContext.cutList.empty() || cutContext.cutList.back() > pcrMsec) {
                        writeFailed = !psiArchiver.Add(pid, (0x200000000 + pcr - cutContext.totalCutMsec * 90) & 0x1ffffffff, psiSize, psi);
                    }
                });
                if (writeFailed) {
                    return 1;
                }
            }
            if (n == 0) {
                break;
            }
            if (unitSize == 0) {
                bufCount = 0;
            }
            else {
                if ((bufPos != 0 || bufCount >= unitSize) && (bufCount - bufPos) % unitSize != 0) {
                    std::copy(buf + bufPos + (bufCount - bufPos) / unitSize * unitSize, buf + bufCount, buf);
                }
                bufCount = (bufCount - bufPos) % unitSize;
            }
        }
    }
    if (!psiArchiver.Flush()) {
        return 1;
    }
    return 0;
}
