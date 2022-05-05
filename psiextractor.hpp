#ifndef INCLUDE_PSIEXTRACTOR_HPP
#define INCLUDE_PSIEXTRACTOR_HPP

#include "util.hpp"
#include <stdint.h>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class CPsiExtractor
{
public:
    CPsiExtractor();
    void SetProgramNumberOrIndex(int n) { m_programNumberOrIndex = n; }
    void AddTargetPid(int pid);
    void AddTargetStreamType(int streamType);
    void AddPacket(const uint8_t *packet, const std::function<void (int, int64_t, size_t, const uint8_t *)> &onExtract);

private:
    struct PSI_SI
    {
        bool specified;
        bool existsOnPmt;
        int continuityCounter;
        int dataCount;
        uint8_t data[4096];
    };
    static std::vector<PMT_REF>::const_iterator FindNitRef(const std::vector<PMT_REF> &pmt);
    std::vector<PMT_REF>::const_iterator FindTargetPmtRef(const std::vector<PMT_REF> &pmt) const;
    void AddPat(int transportStreamID, int programNumber, int pmtPid, int nitPid,
                const std::function<void (int, int64_t, size_t, const uint8_t *)> &onExtract);
    void AddPmt(const PSI &psi, int pid, const std::function<void (int, int64_t, size_t, const uint8_t *)> &onExtract);
    static void ExtractPsiSi(PSI_SI &psiSi, const uint8_t *payload, int payloadSize, int unitStart, int counter,
                             const std::function<void (int, const uint8_t *)> &onExtract);

    int m_programNumberOrIndex;
    PAT m_pat;
    PSI m_pmtPsi;
    std::unordered_map<int, PSI_SI> m_targetPsiSiMap;
    std::unordered_set<int> m_targetStreamTypes;
    int m_nitPid;
    int m_pcrPid;
    int64_t m_pcr;
    std::vector<uint8_t> m_lastPat;
    std::vector<uint8_t> m_lastPmt;
};

#endif
