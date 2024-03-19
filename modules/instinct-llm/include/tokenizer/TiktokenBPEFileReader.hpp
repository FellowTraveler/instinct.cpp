//
// Created by RobinQu on 2024/3/2.
//

#ifndef TIKTOKENBPEFILEREADER_HPP
#define TIKTOKENBPEFILEREADER_HPP


#include <fstream>
#include <libbase64.h>

#include "BPETokenRanksReader.hpp"
#include "tools/StringUtils.hpp"
#include "tools/CodecUtils.hpp"



namespace INSTINCT_LLM_NS {
    using namespace INSTINCT_CORE_NS;
    class TiktokenBPEFileReader: public BPETokenRanksReader {
        std::filesystem::path bpe_file_path_;

    public:
        explicit TiktokenBPEFileReader(std::filesystem::path bpe_file_path)
            : bpe_file_path_(std::move(bpe_file_path)) {
        }
        BPETokenRanks Fetch() override;
    };

    inline BPETokenRanks TiktokenBPEFileReader::Fetch() {
        std::ifstream bpe_file(bpe_file_path_);
        if (!bpe_file.is_open()) {
            throw InstinctException("failed to open bpe file at " + bpe_file_path_.string());
        }

        BPETokenRanks bpe_token_ranks;
        for (std::string line; std::getline(bpe_file, line);) {
            // as content is encoded in base64, it's fine to use std::string
            auto splits = StringUtils::ReSplit(line);
            if (splits.size()!=2) {
                // TODO warn about invalid line
                continue;
            }
            auto key = CodecUtils::DecodeBase64(splits[0]);
            bpe_token_ranks[key] = std::stoi(splits[1]);
        }

        return bpe_token_ranks;
    }
}

#endif //TIKTOKENBPEFILEREADER_HPP