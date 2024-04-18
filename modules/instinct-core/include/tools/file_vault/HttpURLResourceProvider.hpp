//
// Created by RobinQu on 2024/4/18.
//
#ifndef IHTTPURLRESOURCEPROVIDER_HPP
#define IHTTPURLRESOURCEPROVIDER_HPP


#include "BaseFileVaultResourceProvider.hpp"
#include "tools/http/CURLHttpClient.hpp"
#include "tools/http/IHttpClient.hpp"

namespace INSTINCT_CORE_NS {

    class HttpURLResourceProvider final: public BaseFileVaultResourceProvider {
        HttpClientPtr client_;
        HttpRequest call_;

    public:
        HttpURLResourceProvider(
            const std::string &resource_name,
            HttpRequest call,
            const std::unordered_map<std::string, std::string> &metadata = {},
            HttpClientPtr client = nullptr)
            : BaseFileVaultResourceProvider(resource_name, metadata),
              client_(std::move(client)),
              call_(std::move(call)) {
            if (!client_) {
                client_ = CreateCURLHttpClient();
            }
            HttpUtils::AssertHttpRequest(call_);
        }


        HttpURLResourceProvider(
            const std::string &resource_name,
            const std::string &request_line,
            const std::unordered_map<std::string, std::string> &metadata = {},
            const HttpClientPtr& client = nullptr
            ): HttpURLResourceProvider(resource_name, HttpUtils::CreateRequest(request_line), metadata, client)  {
        }

        void Write(std::ostream &output_stream) override {
            const auto [headers, status_code] = client_->ExecuteWithCallback(call_, [&](const std::string& buf) {
                output_stream.write(buf.data(), buf.size());
                return true;
            });
            assert_true(status_code < 400, fmt::format("Status code {} for URL {}", status_code, HttpUtils::CreateUrlString(call_)));
            // add response headers to metadata
            GetMetadata().insert(headers.begin(), headers.end());
        }

    };
}

#endif