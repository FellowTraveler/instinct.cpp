//
// Created by RobinQu on 2024/3/5.
//

#include <gtest/gtest.h>

#include "Corpus.hpp"
#include "document/CharacterTextSplitter.hpp"
#include "document/LanguageSplitters.hpp"
#include "tools/StringUtils.hpp"


namespace INSTINCT_CORE_NS {
    TEST(CharacterTextSplitter, TestSimpleSplit) {
        auto* text_splitter = new CharacterTextSplitter({.chunk_size = 100});
        auto result = text_splitter->SplitText(corpus::text5);
        u32_utils::print_splits("splits: ", result);
        ASSERT_EQ(result.size(), 4);
        delete text_splitter;

        text_splitter = new CharacterTextSplitter({.chunk_size = 5, .chunk_overlap = 2, .separator =   " "});
        result = text_splitter->SplitText("朱雀 玄武 青龙 白虎");
        u32_utils::print_splits("cn splits", result);
        ASSERT_EQ(result.size(), 3);
        ASSERT_TRUE(result[2] == "青龙 白虎");
        delete text_splitter;

        text_splitter = new CharacterTextSplitter({.chunk_size = 7, .chunk_overlap = 3, .separator=" "});
        result = text_splitter->SplitText("abc def jkl mna");
        u32_utils::print_splits("splits", result);
        ASSERT_EQ(result.size(), 3);
        ASSERT_TRUE(result[2] == "jkl mna");
        delete text_splitter;
    }
}