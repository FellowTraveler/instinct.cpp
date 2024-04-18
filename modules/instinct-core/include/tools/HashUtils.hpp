//
// Created by RobinQu on 2024/4/18.
//

#ifndef HASHUTILS_HPP
#define HASHUTILS_HPP
#include <md5.h>
#include <sha1.h>
#include <sha256.h>


#include "CoreGlobals.hpp"

namespace INSTINCT_CORE_NS {

    template<typename H, typename Buffer = const void*, typename ByteCount = size_t>
    concept IsHashImplementation = requires(H hash, Buffer buf, ByteCount count)
    {
        hash.add(buf, count);
        { hash.getHash() } -> std::same_as<std::string>;
    };


    class HashUtils final {
    public:
        /**
         * Compute md5 hash for given buffer
         * @param buf
         * @return
         */
        template<typename Hash>
        requires IsHashImplementation<Hash>
        static std::string HashForString(const std::string& buf) {
            Hash hash;
            hash.add(buf.data(), buf.size());
            return hash.getHash();
        }

        template<typename Hash>
        requires IsHashImplementation<Hash>
        static std::string HashForStream(std::istream& input_stream) {
            Hash hash;
            static constexpr size_t BUFFER_SIZE = 144*7*1024;
            char buf[BUFFER_SIZE];
            while (input_stream) {
                input_stream.read(buf, BUFFER_SIZE);
                auto bytes_read = size_t(input_stream.gcount());
                hash.add(buf, bytes_read);
            }
            return hash.getHash();
        }
    };

}


#endif //HASHUTILS_HPP
