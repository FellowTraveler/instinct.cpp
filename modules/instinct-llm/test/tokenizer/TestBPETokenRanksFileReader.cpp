//
// Created by RobinQu on 2024/3/3.
//
#include <gtest/gtest.h>


#include "CoreGlobals.hpp"
#include "tokenizer/TiktokenBPEFileReader.hpp"
#include "tools/ChronoUtils.hpp"

namespace INSTINCT_LLM_NS {
    TEST(BPETokenRanksFileReader, TestLoad) {
        const std::filesystem::path assets_dir = std::filesystem::current_path() / "./modules/instinct-core/test/_assets";
        auto t1 = ChronoUtils::GetCurrentTimeMillis();
        TiktokenBPEFileReader reader (assets_dir / "cl100k_base/cl100k_base.tiktoken");
        auto bpe_ranks = reader.Fetch();
        std::cout << "bpe loaded in " << ChronoUtils::GetCurrentTimeMillis()-t1 << "ms" << std::endl;
        std::cout << "item_cout=" << bpe_ranks.size() << std::endl;
        ASSERT_EQ(bpe_ranks.size(), 100256);
     }
}

