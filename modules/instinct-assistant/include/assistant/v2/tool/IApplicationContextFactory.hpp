//
// Created by vscode on 5/4/24.
//

#ifndef INSTINCT_IAPPLICAITONCONTEXTFACTORY_HPP
#define INSTINCT_IAPPLICAITONCONTEXTFACTORY_HPP

#include "AssistantGlobals.hpp"
#include "database/duckdb/DuckDBConnectionPool.hpp"
#include "database/duckdb/DuckDBDataMapper.hpp"
#include "object_store/FileSystemObjectStore.hpp"
#include "task_scheduler/ThreadPoolTaskScheduler.hpp"
#include "assistant/v2/service/AssistantFacade.hpp"
#include "assistant/v2/task_handler/OpenAIToolAgentRunObjectTaskHandler.hpp"
#include "server/httplib/HttpLibServer.hpp"

namespace INSTINCT_ASSISTANT_NS::v2 {

    using namespace INSTINCT_DATA_NS;
    using namespace INSTINCT_SERVER_NS;


    /**
     * Utility class to configure instances required for a complete assistant api service.
     * Implementation should create suitable objects according to configuration.
     * @tparam ConnectionImpl
     * @tparam QueryResultImpl
     * @tparam TaskPayload
     */
    template<typename ConnectionImpl, typename  QueryResultImpl, typename TaskPayload = std::string>
    class IApplicationContextFactory {
    public:
        struct ApplicationContext {
            ConnectionPoolPtr<ConnectionImpl, QueryResultImpl> connection_pool;
            DataMapperPtr<AssistantObject, std::string> assistant_data_mapper;
            DataMapperPtr<ThreadObject, std::string> thread_data_mapper;
            DataMapperPtr<MessageObject, std::string> message_data_mapper;
            DataMapperPtr<FileObject, std::string> file_data_mapper;
            DataMapperPtr<RunObject, std::string> run_data_mapper;
            DataMapperPtr<RunStepObject, std::string> run_step_data_mapper;
            ObjectStorePtr object_store;
            TaskSchedulerPtr<TaskPayload> task_scheduler;
            AssistantFacade assistant_facade;
            HttpLibServerPtr http_server;
            TaskHandlerPtr<TaskPayload> run_object_task_handler;
        };

        IApplicationContextFactory() = default;
        virtual ~IApplicationContextFactory()=default;
        IApplicationContextFactory(IApplicationContextFactory&&)=delete;
        IApplicationContextFactory(const IApplicationContextFactory&)=delete;
        virtual ApplicationContext GetInstance() = 0;
    };

}


#endif //INSTINCT_IAPPLICAITONCONTEXTFACTORY_HPP
