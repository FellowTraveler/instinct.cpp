//
// Created by RobinQu on 2024/3/15.
//

#ifndef OPENAICHAT_HPP
#define OPENAICHAT_HPP

#include "BaseChatModel.hpp"
#include "LLMGlobals.hpp"
#include "commons/OpenAICommons.hpp"
#include "tools/HttpRestClient.hpp"
#include <llm.pb.h>

namespace INSTINCT_LLM_NS {


    /**
    * OpenAI API endpoint reference:
    * https://platform.openai.com/docs/api-reference/chat/create
    */
    class OpenAIChat final: public BaseChatModel {
        OpenAIConfiguration configuration_;
        HttpRestClient client_;

    public:
        explicit OpenAIChat(OpenAIConfiguration configuration)
            : BaseChatModel(configuration.base_options), configuration_(std::move(configuration)), client_(configuration_.endpoint) {
        }

        void CallOpenAI(const MessageList& message_list, BatchedLangaugeModelResult& batched_langauge_model_result) {
            const auto req = BuildRequest_(message_list, false);
            const auto resp = client_.PostObject<OpenAIChatCompletionRequest, OpenAIChatCompletionResponse>(DEFAULT_OPENAI_CHAT_COMPLETION_ENDPOINT, req);

            auto* langauge_model_result = batched_langauge_model_result.add_generations();
            for(const auto& choice: resp.choices()) {
                auto* single_result = langauge_model_result->add_generations();
                single_result->set_text(choice.message().content());
                single_result->set_is_chunk(false);
                single_result->mutable_message()->CopyFrom(choice.message());
            }
        }

        BatchedLangaugeModelResult Generate(const std::vector<MessageList>& message_matrix) override {
            // TODO make it parelle
            BatchedLangaugeModelResult batched_langauge_model_result;
            for (const auto& mesage_list: message_matrix) {
                CallOpenAI(mesage_list, batched_langauge_model_result);
            }
            return batched_langauge_model_result;
        }

        AsyncIterator<LangaugeModelResult> StreamGenerate(const MessageList& messages) override {
            const auto req = BuildRequest_(messages, true);
            const auto chunk_itr = client_.StreamChunkObject<OpenAIChatCompletionRequest, OpenAIChatCompletionChunk>(DEFAULT_OPENAI_CHAT_COMPLETION_ENDPOINT, req, true, {"[DONE]"});
            return chunk_itr | rpp::operators::map([](const OpenAIChatCompletionChunk& chunk) {
                LangaugeModelResult langauge_model_result;
                for (const auto& choice: chunk.choices()) {
                    auto* single_result = langauge_model_result.add_generations();
                    single_result->set_text(choice.delta().content());
                    single_result->set_is_chunk(false);
                    single_result->mutable_message()->CopyFrom(choice.delta());
                }
                return langauge_model_result;
            });
        }

    private:
        OpenAIChatCompletionRequest BuildRequest_(const MessageList& message_list, const bool stream) {
            OpenAIChatCompletionRequest req;
            for (const auto& msg: message_list.messages()) {
                req.add_messages()->CopyFrom(msg);
            }
            req.set_model(configuration_.model_name);
            req.set_n(1);
            req.set_seed(configuration_.seed);
            req.set_temperature(configuration_.temperature);
            req.set_max_tokens(configuration_.max_tokens);
            if (configuration_.json_object) {
                req.mutable_response_format()->set_type("json_object");
            }
            req.set_stream(stream);
            return req;
        }

    };

    static ChatModelPtr CreateOpenAIChatModel(const OpenAIConfiguration& configuration) {
        return std::make_shared<OpenAIChat>(configuration);
    }
}


#endif //OPENAICHAT_HPP
