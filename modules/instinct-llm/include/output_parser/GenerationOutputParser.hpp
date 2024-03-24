//
// Created by RobinQu on 2024/3/10.
//

#ifndef STRINGOUTPUTPARSER_HPP
#define STRINGOUTPUTPARSER_HPP

#include <google/protobuf/util/json_util.h>
#include "IOutputParser.hpp"
#include "LLMGlobals.hpp"
#include "tools/Assertions.hpp"
#include "output_parser/BaseOutputParser.hpp"
#include "prompt/MessageUtils.hpp"

namespace INSTINCT_LLM_NS {
    using namespace INSTINCT_CORE_NS;

    class GenerationOutputParser final : public BaseOutputParser<Generation> {
    public:
        explicit GenerationOutputParser(const OutputParserOptions &options = {}) :
                BaseOutputParser<Generation>(options) {}

        /**
         * passthrough implementation
         * @param result
         * @return
         */
        Generation ParseResult(const JSONContextPtr &result) override {
            return result->RequireMessage<Generation>(GetOptions().generation_input_key);
        }

        std::string GetFormatInstruction() override {
            // tend to instruct nothing
            return "";
        }

    };

}

#endif //STRINGOUTPUTPARSER_HPP