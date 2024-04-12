//
// Created by RobinQu on 2024/2/12.
//

#ifndef COREGLOBALS_H
#define COREGLOBALS_H

#include <core.pb.h>
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <unicode/unistr.h>
#include <unicode/uversion.h>
#include <google/protobuf/message.h>
#include <fmtlog/fmtlog.h>


#define INSTINCT_CORE_NS instinct::core

// another set of marcos to hide log implementation
#define LOG_ERROR loge
#define LOG_WARN logw
#define LOG_INFO logi
#define LOG_DEBUG logd

namespace INSTINCT_CORE_NS {

    static void SetupLogging() {
        // TODO setup coloring for each level
        fmtlog::setLogLevel(fmtlog::DBG);
        fmtlog::startPollingThread();
    }

    template<typename R>
    concept sized_range = std::ranges::input_range<R> && std::ranges::sized_range<R>;

    template<typename T>
    concept numberic = std::integral<T> || std::floating_point<T>;

    template<class>
    inline constexpr bool always_false_v = false;

    template<class... Ts>
    struct overloaded : Ts... { using Ts::operator()...; };
    // explicit deduction guide (not needed as of C++20)
    template<class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

    template <typename R, typename V>
        concept RangeOf = std::ranges::range<R> && std::same_as<std::ranges::range_value_t<R>, V>;

    using U32String = U_ICU_NAMESPACE::UnicodeString;

    template <typename T>
    concept IsProtobufMessage = std::derived_from<T, google::protobuf::Message>;


    class InstinctException: public std::runtime_error {
        std::string reason_;

    public:
        explicit InstinctException(const std::string& basic_string)
            : std::runtime_error(basic_string), reason_(basic_string) {
        }

        explicit InstinctException(const char* const string)
            : std::runtime_error(string), reason_(string) {
        }

        explicit InstinctException(const runtime_error& runtime_error, std::string  basic_string)
            : std::runtime_error(runtime_error),reason_(std::move(basic_string)) {

        }

        [[nodiscard]] const char* what() const noexcept override {
            return reason_.data();
        }
    };

    static const std::string METADATA_SCHEMA_PARENT_DOC_ID_KEY = "parent_doc_id";

    static const std::string METADATA_SCHEMA_PAGE_NO_KEY = "page_no";

    static const std::string METADATA_SCHEMA_FILE_SOURCE_KEY = "file_source";

    using MetadataSchemaPtr = std::shared_ptr<MetadataSchema>;

}


template <> struct fmt::formatter<INSTINCT_CORE_NS::PrimitiveType>: formatter<string_view> {
    template <typename FormatContext>
    auto format(INSTINCT_CORE_NS::PrimitiveType c, FormatContext& ctx) {
        string_view name = "";
        switch (c) {
            case INSTINCT_CORE_NS::INT32:   name = "INT32"; break;
            case INSTINCT_CORE_NS::INT64: name = "INT64"; break;
            case INSTINCT_CORE_NS::FLOAT: name = "FLOAT"; break;
            case INSTINCT_CORE_NS::DOUBLE: name = "DOUBLE"; break;
            case INSTINCT_CORE_NS::BOOL: name = "BOOL"; break;
            case INSTINCT_CORE_NS::VARCHAR: name = "VARCHAR"; break;
            default: name = "";
        }
        return formatter<string_view>::format(name, ctx);
    }
};

#endif //COREGLOBALS_H
