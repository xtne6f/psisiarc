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
#include "psiarchiver.hpp"
#include "psiextractor.hpp"
#include "util.hpp"

namespace
{
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
    CPsiArchiver psiArchiver;
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
                uint32_t interval = static_cast<uint32_t>(strtol(GetSmallString(argv[++i]), nullptr, 10) * 11250);
                psiArchiver.SetWriteInterval(interval);
                invalid = interval > 600 * 11250;
            }
            else if (c == 'b') {
                size_t size = static_cast<size_t>(strtol(GetSmallString(argv[++i]), nullptr, 10) * 1024);
                psiArchiver.SetDictionaryMaxBuffSize(size);
                invalid = size < 8 * 1024 || 1024 * 1024 * 1024 < size;
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
            for (int i = bufPos; i + 188 <= bufCount; i += unitSize) {
                psiExtractor.AddPacket(buf + i, [&psiArchiver](int pid, int64_t pcr, size_t psiSize, const uint8_t *psi) {
                    psiArchiver.Add(pid, pcr, psiSize, psi);
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
    psiArchiver.Flush();
    return 0;
}
