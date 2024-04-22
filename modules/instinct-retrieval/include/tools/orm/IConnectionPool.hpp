//
// Created by RobinQu on 2024/4/22.
//

#ifndef CONNECTIONPOOL_HPP
#define CONNECTIONPOOL_HPP

#include "RetrievalGlobals.hpp"

namespace INSTINCT_RETRIEVAL_NS {

    template<typename Impl>
    class IConnection {
    public:
        IConnection()=default;
        virtual ~IConnection()=default;
        IConnection(IConnection&&)=delete;
        IConnection(const IConnection&)=delete;

        virtual Impl* operator*() = 0;
        virtual std::chrono::time_point<std::chrono::system_clock> GetLastActiveTime() = 0;
        virtual void UpdateActiveTime() = 0;
    };


    template<typename Impl>
    class IConnectionPool {
    public:
        using ConnectionPtr = std::shared_ptr<IConnection<Impl>>;
        IConnectionPool()=default;
        virtual ~IConnectionPool()=default;
        IConnectionPool(const IConnectionPool&)=delete;
        IConnectionPool(IConnectionPool&&)=delete;

        virtual ConnectionPtr Create() = 0;
        virtual ConnectionPtr Acquire() = 0;
        virtual bool Check(const ConnectionPtr& connection) = 0;
        virtual void Release(const ConnectionPtr& connection) = 0;

    };
}

#endif //CONNECTIONPOOL_HPP
