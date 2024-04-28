//
// Created by RobinQu on 2024/4/25.
//

#include <gtest/gtest.h>
#include <google/protobuf/util/message_differencer.h>

#include "AssistantTestGlobals.hpp"
#include "assistant/v2/service/IRunService.hpp"
#include "assistant/v2/service/impl/RunServiceImpl.hpp"


namespace INSTINCT_ASSISTANT_NS::v2 {
    class RunServiceTest: public BaseAssistantApiTest {
    public:
        RunServicePtr CreateService() {
            return std::make_shared<RunServiceImpl>(thread_data_mapper, run_data_mapper, run_step_data_mapper, message_data_mapper);
        }

        AssistantServicePtr assistant_service = std::make_shared<AssistantServiceImpl>(assistant_data_mapper);
    };

    TEST_F(RunServiceTest, SimpleCRUDWihtRunObjects) {
        util::MessageDifferencer message_differencer;
        const auto run_service = CreateService();

        // create asssitant
        AssistantObject create_assitant_request;
        create_assitant_request.set_model("ollama/llama3:latest");
        const auto obj1 = assistant_service->CreateAssistant(create_assitant_request);
        LOG_INFO("CreateAssistant returned: {}", obj1->ShortDebugString());

        // create thread and run
        CreateThreadAndRunRequest create_thread_and_run_request1;
        create_thread_and_run_request1.set_assistant_id(obj1->id());
        auto* msg = create_thread_and_run_request1.mutable_thread()->add_messages();
        msg->set_role(user);
        msg->mutable_content()->mutable_text()->set_value("What's the population of India?");
        msg->mutable_content()->set_type(MessageObject_MessageContentType_text);

        const auto obj2 = run_service->CreateThreadAndRun(create_thread_and_run_request1);
        LOG_INFO("CreateThreadAndRun returned: {}", obj2->ShortDebugString());

        // create run
        CreateRunRequest create_run_request1;
        create_run_request1.set_assistant_id(obj1->id());
        create_run_request1.set_thread_id(obj2->thread_id());
        auto* msg2 = create_run_request1.mutable_additional_messages()->Add();
        msg2->set_role(user);
        msg2->mutable_content()->mutable_text()->set_value("How many planets in solar system?");
        msg2->mutable_content()->set_type(MessageObject_MessageContentType_text);
        const auto obj3 = run_service->CreateRun(create_run_request1);
        LOG_INFO("CreateRun returned: {}", obj3->ShortDebugString());

        // get run
        GetRunRequest get_run_request;
        get_run_request.set_run_id(obj2->id());
        get_run_request.set_thread_id(obj2->thread_id());
        const auto obj4 = run_service->RetrieveRun(get_run_request);
        LOG_INFO("RetrieveRun returned: {}", obj4->ShortDebugString());
        ASSERT_EQ(obj4->object(), "thread.run");
        ASSERT_EQ(obj4->status(), RunObject_RunObjectStatus_queued);
        ASSERT_GT(obj4->created_at(), 0);
        ASSERT_GT(obj4->modified_at(), 0);

        // list run
        ListRunsRequest list_runs_request1;
        list_runs_request1.set_thread_id(obj2->thread_id());
        list_runs_request1.set_order(desc);
        ListRunsResponse list_runs_response1 = run_service->ListRuns(list_runs_request1);
        LOG_INFO("ListRuns returned: {}", list_runs_response1.ShortDebugString());
        ASSERT_EQ(list_runs_response1.object(), "list");
        ASSERT_EQ(list_runs_response1.data_size(), 2);
        ASSERT_TRUE(message_differencer.Compare(list_runs_response1.data(0), obj3.value()));
        ASSERT_TRUE(message_differencer.Compare(list_runs_response1.data(1), obj2.value()));

        // update run
        ModifyRunRequest modify_run_request;
        modify_run_request.set_run_id(obj3->id());
        modify_run_request.set_thread_id(obj3->thread_id());
        google::protobuf::Value string_value;
        string_value.set_string_value("bar");
        modify_run_request.mutable_metadata()->mutable_fields()->emplace("foo", string_value);
        const auto obj5 = run_service->ModifyRun(modify_run_request);
        LOG_INFO("ModifyRun returned: {}", obj5->ShortDebugString());
        ASSERT_EQ(obj5->metadata().fields().at("foo").string_value(), "bar");

        // cancel run
        CancelRunRequest cancel_run_request;
        cancel_run_request.set_run_id(obj2->id());
        cancel_run_request.set_thread_id(obj2->thread_id());
        const auto obj6 = run_service->CancelRun(cancel_run_request);
        LOG_INFO("CancelRun returned: {}", obj6->ShortDebugString());
        ASSERT_EQ(obj6->status(), RunObject::RunObjectStatus::RunObject_RunObjectStatus_cancelling);
    }
}