//
// Created by RobinQu on 2024/4/29.
//

#ifndef RUNOBJECTHANDLER_HPP
#define RUNOBJECTHANDLER_HPP

#include "AssistantGlobals.hpp"
#include "agent/executor/BaseAgentExecutor.hpp"
#include "task_scheduler/ThreadPoolTaskScheduler.hpp"
#include "agent/patterns/openai_tool/Agent.hpp"

namespace INSTINCT_ASSISTANT_NS::v2 {
    using namespace INSTINCT_DATA_NS;

    /**
     * Task handler for run objects using `OpenAIToolAgentExecutor`.
     */
    class OpenAIToolAgentRunObjectTaskHandler final: public CommonTaskScheduler::ITaskHandler {
        RunServicePtr run_service_;
        MessageServicePtr message_service_;
        AssistantServicePtr assistant_service_;
        ChatModelPtr  chat_model_;
        FunctionToolkitPtr built_in_toolkit_;

    public:
        static inline std::string CATEGORY = "run_object";

        OpenAIToolAgentRunObjectTaskHandler(RunServicePtr run_service, MessageServicePtr message_service,
            AssistantServicePtr assistant_service, ChatModelPtr chat_model, FunctionToolkitPtr built_in_toolkit)
            : run_service_(std::move(run_service)),
              message_service_(std::move(message_service)),
              assistant_service_(std::move(assistant_service)),
              chat_model_(std::move(chat_model)),
              built_in_toolkit_(std::move(built_in_toolkit)) {
        }

        bool Accept(const ITaskScheduler<std::string>::Task &task) override {
            return task.category == CATEGORY;
        }

        void Handle(const ITaskScheduler<std::string>::Task &task) override {
            RunObject run_object;
            ProtobufUtils::Deserialize(task.payload, run_object);

            if (CheckPreconditions_(run_object)) {
                LOG_WARN("Precondition failure for run object: {}", run_object.ShortDebugString());
                return;
            }

            // update run object to status of `in_progress`
            if(UpdateRunObjectStatus(run_object.thread_id(), run_object.id(), RunObject_RunObjectStatus_in_progress)) {
                LOG_ERROR("Illegal response for updating run object: {}", run_object.ShortDebugString());
                return;
            }

            const auto state_opt = RecoverAgentState(run_object);
            if (!state_opt) {
                LOG_ERROR("Failed to recover state with run object: {}", run_object.ShortDebugString());
                return;
            }

            const auto& state = state_opt.value();
            const auto executor = BuildAgentExecutor_(run_object);

            // execute possible steps
            executor->Stream(state)
                | rpp::operators::as_blocking()
                | rpp::operators::subscribe([&](const AgentState& current_state) {

                    // respond to state changes
                    const auto last_step = current_state.previous_steps().rbegin();
                    if (last_step->has_thought()) {
                        if (last_step->thought().has_continuation()
                            && last_step->thought().continuation().openai().has_tool_call_message()) { // should contain thought of calling code interpreter and file search, which are invoked automatically
                            OnAgentContinuation(last_step->thought().continuation(), run_object);
                            return;
                        }

                        if (last_step->thought().has_pause()
                            && last_step->thought().pause().has_openai()) { // should contain thought of calling function tools
                            OnAgentPause_(last_step->thought().pause(), run_object);
                            return;
                        }

                        if(last_step->thought().has_finish()) { // finish message
                            OnAgentFinish_(last_step->thought().finish(), run_object);
                            return;
                        }
                    }

                    if (last_step->has_observation() && last_step->observation().has_openai()) {
                        // 1. function tool call results are submitted
                        // 2. or only contain tool calls for code interpreter and file search
                        OnAgentObservation_(last_step->observation(), run_object);
                        return;
                    }

                    LOG_WARN("Illegal message from agent: {}", last_step->ShortDebugString());
                }, [&](const std::exception_ptr& e) {
                    // translate exception to AgentFinish
                    AgentFinish agent_finish;
                    agent_finish.set_is_failed(true);
                    RunEarlyStopDetails run_early_stop_details;
                    try {
                        std::rethrow_exception(e);
                    } catch (const ClientException& client_exception) {
                        run_early_stop_details.mutable_error()->set_type(invalid_request_error);
                        run_early_stop_details.mutable_error()->set_message(client_exception.what());
                    } catch (const InstinctException& instinct_exception) {
                        run_early_stop_details.mutable_error()->set_type(server_error);
                        run_early_stop_details.mutable_error()->set_message(instinct_exception.what());
                    } catch (const std::runtime_error& runtime_error) {
                        run_early_stop_details.mutable_error()->set_type(server_error);
                        run_early_stop_details.mutable_error()->set_message(runtime_error.what());
                    } catch (...) {
                        run_early_stop_details.mutable_error()->set_type(server_error);
                        run_early_stop_details.mutable_error()->set_message("Uncaught exception");
                    }
                    agent_finish.mutable_details()->PackFrom(run_early_stop_details);
                    OnAgentFinish_(agent_finish, run_object);
                });
        }


        [[nodiscard]] std::optional<AgentState> RecoverAgentState(const RunObject& run_object) const {
            AgentState state;
            if (!LoadAgentStateFromRun_(run_object, state)) {
                LOG_ERROR("Cannot load agent state from run object: {}", run_object.ShortDebugString());
                return std::nullopt;
            }

            // load function tools
            if (!LoadFunctionTools_(run_object, state)) {
                LOG_ERROR("Cannot load function tool schamas for agent state from run object: {}", run_object.ShortDebugString());
                return std::nullopt;
            }

            return state;
        }


    private:

        /**
         * Check following conditions:
         * 1. run object is in status of `queued` or `requires_action`.
         * 2. file resources referenced are valid
         * @param run_object
         * @return
         */
        // ReSharper disable once CppMemberFunctionMayBeStatic
        [[nodiscard]] bool CheckPreconditions_(const RunObject& run_object) const { // NOLINT(*-convert-member-functions-to-static)
            return run_object.status() == RunObject_RunObjectStatus_queued || run_object.status() == RunObject_RunObjectStatus_requires_action;
        }

        /**
         * Just return `OpenAIToolAgentExecutor`
         * @param run_object
         * @return
         */
        [[nodiscard]] AgentExecutorPtr BuildAgentExecutor_(const RunObject& run_object) const {
            // no built-in toolkit for now
            return CreateOpenAIToolAgentExecutor(chat_model_, {}, [&](const AgentState& state, AgentStep& step) {
                return CheckRunObjectForExecution_(run_object.thread_id(), run_object.id(), state, step);
            });
        }

        /**
         * Predicate if early stop is required for run object
         * @param thread_id
         * @param run_id
         * @param state
         * @param step
         * @return
         */
        bool CheckRunObjectForExecution_(const std::string& thread_id, const std::string& run_id, const AgentState& state, AgentStep& step) const {
            GetRunRequest get_run_request;
            get_run_request.set_run_id(run_id);
            get_run_request.set_thread_id(thread_id);
            const auto get_run_resp = run_service_->RetrieveRun(get_run_request);
            RunEarlyStopDetails run_early_stop_details;
            auto *finish = step.mutable_thought()->mutable_finish();
            if (get_run_resp.has_value()) {
                if (get_run_resp->status() == RunObject_RunObjectStatus_cancelling
                    || get_run_resp->status() == RunObject_RunObjectStatus_cancelled
                    // it's unlikely for a `canceled` run object to be passed in, but just in case of that, we choose to set it to `cancelled` again.
                    ) {
                    // save to any field
                    finish->mutable_details()->PackFrom(run_early_stop_details);
                    finish->set_is_cancelled(true);
                    return true;
                }
                if (get_run_resp->status() == RunObject_RunObjectStatus_expired) {
                    finish->set_is_expired(true);
                    // save to any field
                    step.mutable_thought()->mutable_finish()->mutable_details()->PackFrom(run_early_stop_details);
                    return true;
                }
            } else {
                run_early_stop_details.mutable_error()->set_type(invalid_request_error);
                finish->set_is_failed(true);
                step.mutable_thought()->mutable_finish()->mutable_details()->PackFrom(run_early_stop_details);
                return false;
            }
            return false;
        }


        /**
         * After continuation message is generated,
         * 1. create run step of `message_creation` if `OpenAIToolAgentContinuation.tool_call_message` contains content string
         * 2. create run step object with type of `tool_calls` and status of `in_progress` if `OpenAIToolAgentContinuation.tool_call_message` contains non-empty `tool_calls`.
         * 3. update run object to status of `in_progress`.
         * @param agent_continuation
         * @param run_object
         */
        void OnAgentContinuation(const AgentContinuation& agent_continuation, const RunObject& run_object) const {
            LOG_INFO("OnAgentContinuation Start, run_object={}", run_object.ShortDebugString());

            RunStepObject run_step_object;
            run_step_object.set_thread_id(run_object.thread_id());
            run_step_object.set_run_id(run_object.id());
            run_step_object.set_type(RunStepObject_RunStepType_tool_calls);
            run_step_object.set_assistant_id(run_object.assistant_id());
            auto* step_details = run_step_object.mutable_step_details();

            // create step with message if tool message has content string
            if (StringUtils::IsNotBlankString(agent_continuation.openai().tool_call_message().content())) {
                if (const auto resp = CreateMessageStep_(agent_continuation.openai().tool_call_message().content(), run_object);
                    !resp.has_value()) {
                    LOG_ERROR("Illegal reponse for creating step object with message. tool_call_message={}, run_object={}", agent_continuation.openai().tool_call_message().DebugString(), run_object.ShortDebugString());
                    return;
                }
            }

            // create step with tool step if tool message contains tool call requests
            if (agent_continuation.openai().tool_call_message().tool_calls_size() > 0) {
                // TODO support code interpreter and file serach
                for(const auto& tool_request: agent_continuation.openai().tool_call_message().tool_calls()) {
                    auto* tool_call_detail = step_details->mutable_tool_calls()->Add();
                    tool_call_detail->set_id(tool_request.id());
                    tool_call_detail->set_type(function);
                    auto* function_call = tool_call_detail->mutable_function();
                    function_call->set_name(tool_request.function().name());
                    function_call->set_arguments(tool_request.function().arguments());
                }
                if (const auto create_run_resp = run_service_->CreateRunStep(run_step_object);
                    !create_run_resp.has_value()) {
                    LOG_ERROR("Illegal response for creating run step object: {}", run_step_object.ShortDebugString());
                    return;
                    }
            }

            if(UpdateRunObjectStatus(run_object.thread_id(), run_object.id(), RunObject_RunObjectStatus_in_progress)) {
                LOG_ERROR("Illegal response for updating run object: {}", run_object.ShortDebugString());
                return;
            }

            LOG_INFO("OnAgentContinuation Done, run_object={}", run_object.ShortDebugString());
        }

        [[nodiscard]] std::optional<RunObject> UpdateRunObjectStatus(const std::string& thread_id, const std::string& run_id, const RunObject_RunObjectStatus status) const {
            ModifyRunRequest modify_run_request;
            modify_run_request.set_run_id(run_id);
            modify_run_request.set_thread_id(thread_id);
            modify_run_request.set_status(RunObject_RunObjectStatus_in_progress);
            return run_service_->ModifyRun(modify_run_request);
        }

        [[nodiscard]] std::optional<std::pair<RunStepObject, MessageObject>> CreateMessageStep_(const std::string& content, const RunObject& run_object) const {
            // TODO need transaction
            CreateMessageRequest create_message_request;
            create_message_request.set_thread_id(run_object.thread_id());
            create_message_request.set_role(assistant);
            create_message_request.set_content(content);
            const auto message_object = message_service_->CreateMessage(create_message_request);
            if (!message_object.has_value()) {
                LOG_ERROR("Cannot create message for this step. run_object={}, create_message_request={}", run_object.ShortDebugString(), create_message_request.ShortDebugString());
                return std::nullopt;
            }

            RunStepObject run_step_object;
            run_step_object.set_run_id(run_object.id());
            run_step_object.set_thread_id(run_object.thread_id());
            run_step_object.set_assistant_id(run_object.assistant_id());
            run_step_object.set_type(RunStepObject_RunStepType_message_creation);
            run_step_object.mutable_step_details()->set_type(RunStepObject_RunStepType_message_creation);
            run_step_object.mutable_step_details()->mutable_message_creation()->set_message_id(message_object->id());
            const auto create_run_step_resp = run_service_->CreateRunStep(run_step_object);
            if(!create_run_step_resp.has_value()) {
                LOG_ERROR("Cannot create run step, run_step_object={}", run_step_object.ShortDebugString());
                return std::nullopt;
            }

            return std::pair {create_run_step_resp.value(), message_object.value()};
        }


        /**
         * After pause message is generated,
         * 1. update `step_details` of run step object with completed tool call results.
         * 2. update `run_object` with status of `required_action` and correct content of `required_actions`
         * @param agent_pause
         * @param run_object
         */
        void OnAgentPause_(const AgentPause& agent_pause, const RunObject& run_object) {
            LOG_INFO("OnAgentPause Start, run_object={}", run_object.ShortDebugString());
            const auto last_run_step_opt = RetrieveLastRunStep_(run_object);
            if (!last_run_step_opt.has_value()) {
                LOG_ERROR("Cannot find last run step for run object: {}", run_object.ShortDebugString());
                return;
            }

            // update run step
            ModifyRunStepRequest modify_run_step_request;
            modify_run_step_request.set_run_id(run_object.id());
            modify_run_step_request.set_step_id(last_run_step_opt->id());
            modify_run_step_request.set_thread_id(run_object.thread_id());
            auto* step_details = modify_run_step_request.mutable_step_details();
            step_details->CopyFrom(last_run_step_opt->step_details());

            // TODO support code interpreter and file serach
            for(const auto& tool_message: agent_pause.openai().completed()) {
                for(int i=0;i<step_details->tool_calls_size();++i) {
                    if (auto* tool_call = step_details->mutable_tool_calls(i);
                        tool_call->id() == tool_message.tool_call_id()) {
                        tool_call->mutable_function()->set_output(tool_message.content());
                    }
                }
            }
            if (const auto modify_run_stpe_resp = run_service_->ModifyRunStep(modify_run_step_request);
                !modify_run_stpe_resp.has_value()) {
                LOG_ERROR("Illegal response for updating run step object: {}", modify_run_step_request.ShortDebugString());
                return;
            }

            // update run object
            if(UpdateRunObjectStatus(run_object.thread_id(), run_object.id(), RunObject_RunObjectStatus_requires_action)) {
                LOG_ERROR("Illegal response for update run object: {}", run_object.ShortDebugString());
                return;
            }
            LOG_INFO("OnAgentPause Completed, run_object={}", run_object.ShortDebugString());
        }

        /**
         * 1. update run object to status of `completed`
         * 2. update of run step object with `step_details` of completed tool call results and status of `completed`
         * @param observation
         * @param run_object
         */
        void OnAgentObservation_(const AgentObservation& observation, const RunObject& run_object) {
            LOG_INFO("OnAgentObservation Start, run_object={}", run_object.ShortDebugString());

            const auto last_run_step = RetrieveLastRunStep_(run_object);
            if (!last_run_step) {
                LOG_ERROR("Cannot find last run step for run object: {}", run_object.ShortDebugString());
                return;
            }

            // update status of run object to `in_progress`
            if (UpdateRunObjectStatus(run_object.thread_id(), run_object.id(), RunObject_RunObjectStatus_in_progress)) {
                LOG_ERROR("Cannot update run object. run_object={}", run_object.ShortDebugString());
                return;
            }

            // update run step with tool call outputs
            ModifyRunStepRequest modify_run_step_request;
            modify_run_step_request.set_run_id(run_object.id());
            modify_run_step_request.set_step_id(last_run_step->id());
            modify_run_step_request.set_thread_id(run_object.thread_id());
            modify_run_step_request.set_status(RunStepObject_RunStepStatus_completed);
            auto* step_details = modify_run_step_request.mutable_step_details();
            step_details->CopyFrom(last_run_step->step_details());

            // TODO support code interpreter and file serach
            for(const auto& tool_message: observation.openai().tool_messages()) {
                for(int i=0;i<step_details->tool_calls_size();++i) {
                    if (auto* tool_call = step_details->mutable_tool_calls(i);
                        tool_call->id() == tool_message.tool_call_id()) {
                        tool_call->mutable_function()->set_output(tool_message.content());
                        }
                }
            }
            if (const auto modify_run_stpe_resp = run_service_->ModifyRunStep(modify_run_step_request);
                !modify_run_stpe_resp.has_value()) {
                LOG_ERROR("Illegal response for updating run step object: {}", modify_run_step_request.ShortDebugString());
                return;
            }

            LOG_INFO("OnAgentObservation Done, run_object={}", run_object.ShortDebugString());
        }

        /**
         * if it's successfully finished:
         * 1. update current run step object with status of `completed`
         * 2. create a new run step object with type of `message_creation`.
         * 3. create a message containing result content in the current thread.
         * 4. update run object with status of `completed`.
         *
         * if it's finished with exception
         * 1. update run step object with status of `failed` and correct content of `last_error`.
         * 2. update run object with status of `failed`.
         *
         * @param finish_message
         * @param run_object
         */
        void OnAgentFinish_(const AgentFinish& finish_message, const RunObject& run_object) {
            LOG_INFO("OnAgentFinish Start, run_object={}", run_object.ShortDebugString());

            const auto last_run_step_opt = RetrieveLastRunStep_(run_object);
            if (!last_run_step_opt.has_value()) {
                LOG_ERROR("Cannot find last run step for run object: {}", run_object.ShortDebugString());
                return;
            }

            // TODO needs transaction
            ModifyRunStepRequest modify_run_step_request;
            modify_run_step_request.set_run_id(run_object.id());
            modify_run_step_request.set_step_id(last_run_step_opt->id());
            modify_run_step_request.set_thread_id(run_object.thread_id());

            ModifyRunRequest modify_run_request;
            modify_run_request.set_run_id(run_object.id());
            modify_run_request.set_thread_id(run_object.thread_id());

            if (finish_message.is_failed()) {
                // update last run step object with status of `failed`
                modify_run_step_request.set_failed_at(ChronoUtils::GetCurrentTimeMillis());
                modify_run_step_request.set_status(RunStepObject_RunStepStatus_failed);

                if (finish_message.has_details() && finish_message.details().Is<RunEarlyStopDetails>()) { // find error data from details
                    RunEarlyStopDetails run_early_stop_details;
                    finish_message.details().UnpackTo(&run_early_stop_details);
                    if (run_early_stop_details.has_error()) {
                        modify_run_step_request.mutable_last_error()->CopyFrom(run_early_stop_details.error());
                    }
                }
                if (!modify_run_step_request.has_last_error()) {
                    // fallback to invalid_request_error
                    LOG_WARN("last_error is not set correctly. run_object={}", run_object.ShortDebugString());
                    modify_run_step_request.mutable_last_error()->set_type(invalid_request_error);
                }


                // update run object with status of `failed`
                modify_run_request.set_status(RunObject_RunObjectStatus_failed);
                if (const auto modify_run_resp = run_service_->ModifyRun(modify_run_request); !modify_run_resp.has_value()) {
                    LOG_ERROR("Failed to update run object. modify_run_request={}", modify_run_request.ShortDebugString());
                    return;
                }
            } else if (finish_message.is_cancelled()) {
                modify_run_step_request.set_status(RunStepObject_RunStepStatus_cancelled);
                modify_run_step_request.set_cancelled_at(ChronoUtils::GetCurrentTimeMillis());
                modify_run_request.set_status(RunObject_RunObjectStatus_cancelled);
            } else if (finish_message.is_expired()) {
                modify_run_step_request.set_expired_at(RunStepObject_RunStepStatus_expired);
                modify_run_step_request.set_expired_at(ChronoUtils::GetCurrentTimeMillis());
                modify_run_step_request.set_status(RunStepObject_RunStepStatus_expired);
            } else {
                // update last run step object with status of `completed`
                modify_run_step_request.set_completed_at(ChronoUtils::GetCurrentTimeMillis());
                modify_run_step_request.set_status(RunStepObject_RunStepStatus_completed);
                modify_run_request.set_status(RunObject_RunObjectStatus_completed);

                // create message step
                if (CreateMessageStep_(finish_message.response(), run_object)) {
                    LOG_ERROR("Failed to create message and run step. modify_run_request={}", modify_run_request.ShortDebugString());
                    return;
                }
            }

            // update run step
            if (run_service_->ModifyRunStep(modify_run_step_request)) {
                LOG_ERROR("Failed to update run step object. modify_run_step_request={}", modify_run_step_request.ShortDebugString());
                return;
            }

            // update run object with status of `completed`
            if (run_service_->ModifyRun(modify_run_request)) {
                LOG_ERROR("Failed to update run object. modify_run_request={}", modify_run_request.ShortDebugString());
                return;
            }

            LOG_INFO("OnAgentFinish Done, run_object={}", run_object.ShortDebugString());
        }


        // ReSharper disable once CppMemberFunctionMayBeConst
        std::optional<RunStepObject> RetrieveLastRunStep_(const RunObject& run_object) {
            ListRunStepsRequest list_run_steps_request;
            list_run_steps_request.set_order(asc);
            list_run_steps_request.set_run_id(run_object.id());
            list_run_steps_request.set_thread_id(run_object.thread_id());
            const auto list_run_steps_resp = run_service_->ListRunSteps(list_run_steps_request);
            if (list_run_steps_resp.data_size() > 0) {
                return *list_run_steps_resp.data().rbegin();
            }
            return std::nullopt;
        }


        bool LoadFunctionTools_(const RunObject& run_object, AgentState& state) const {
            std::vector<FunctionTool> function_tools;
            // 1. find tools on assistant
            GetAssistantRequest get_assistant_request;
            get_assistant_request.set_assistant_id(run_object.assistant_id());
            const auto assistant = assistant_service_->RetrieveAssistant(get_assistant_request);

            if (!assistant) {
                LOG_ERROR("Cannot find assistant object for run_object: {}", run_object.ShortDebugString());
                return false;
            }

            for (const auto& assistant_tool: assistant->tools()) {
                if (assistant_tool.type() == function) {
                    function_tools.push_back(assistant_tool.function());
                }
            }

            // 2. find tools on run object
            for (const auto& assistant_tool: run_object.tools()) {
                if (assistant_tool.type() == function) {
                    function_tools.push_back(assistant_tool.function());
                }
            }

            // 3. filter and transfrom to function tool schema
            const auto final_function_tools = std::ranges::unique(function_tools, [](const FunctionTool& a, const FunctionTool& b) {
                return a.name() == b.name();
            });
            LOG_DEBUG("Found {} function tools for run object: {}", final_function_tools.size(), run_object.ShortDebugString());
            for (auto& function_tool: final_function_tools) {
                state.mutable_function_tools()->Add()->CopyFrom(function_tool);
            }
            return true;
        }

        /**
         * this function is called when run object is in static state. It means no messages are generating and function tools are running.
         * @param run_object
         * @param state
         * @return true if state is loaded correctly
         */
        bool LoadAgentStateFromRun_(const RunObject& run_object, AgentState& state) const {
            if (run_object.status() == RunObject_RunObjectStatus_cancelling || run_object.status() == RunObject_RunObjectStatus_in_progress) {
                LOG_ERROR("Cannot handle run object that is canelling");
                return false;
            }

            // user last message as input
            const auto last_user_message = GetLatestUserMessageObject_(run_object.thread_id());
            if (!last_user_message) {
                LOG_ERROR("No user message found for run object: {}", run_object.ShortDebugString());
                return false;
            }
            auto* input_message = state.mutable_input()->mutable_chat()->add_messages();
            input_message->set_role("user");
            input_message->set_content(last_user_message->content().text().value());

            // find steps
            AgentStep* last_step = nullptr;
            AgentContinuation* last_continuation = nullptr;
            AgentPause* last_pause = nullptr;

            auto run_step_objects = ListAllSteps_(run_object.thread_id(), run_object.id());
            auto n = run_step_objects.size();
            for (int i=0;i<n;++i) {
                const auto& step = run_step_objects.at(i);

                if (step.type() == RunStepObject_RunStepType_tool_calls) {
                    if (step.step_details().tool_calls_size() <= 0) {
                        LOG_ERROR("should have tool calls in a run step");
                        return false;
                    }
                    // assert_positive(step.step_details().tool_calls_size(), "should have tool calls in a run step");

                    // create continuation
                    last_step = state.mutable_previous_steps()->Add();
                    last_continuation = last_step->mutable_thought()->mutable_continuation();
                    auto* tool_call_request = last_continuation->mutable_openai()->mutable_tool_call_message();
                    for (const auto& tool_call: step.step_details().tool_calls()) {
                        // TODO support code-interpreter and file-search
                        if (tool_call.type() == function) {
                            auto* call_request = tool_call_request->add_tool_calls();
                            call_request->set_type(ToolCallObjectType::function);
                            call_request->set_id(tool_call.id());
                            call_request->mutable_function()->set_name(tool_call.function().name());
                            call_request->mutable_function()->set_arguments(tool_call.function().arguments());
                        }
                    }

                    if (i-1>=0) { // look backward for message
                        if (const auto& last_run_step = run_step_objects.at(i-1); last_run_step.type() == RunStepObject_RunStepType_message_creation) {
                            if (const auto message_obj = GetMessageObject_(run_object.thread_id(), last_run_step.step_details().message_creation().message_id())) { // update thought text line if last message is found
                                tool_call_request->set_content(message_obj->content().text().value());
                            }
                        }
                    }

                    if (step.status() == RunStepObject_RunStepStatus_completed) {
                        // create observation
                        last_step = state.mutable_previous_steps()->Add();
                        auto* openai_obsercation = last_step->mutable_observation()->mutable_openai();
                        for (const auto& tool_call: step.step_details().tool_calls()) {
                            if (tool_call.type() == function) {
                                auto* tool_messsage = openai_obsercation->add_tool_messages();
                                tool_messsage->set_content(tool_call.function().output());
                                tool_messsage->set_role("tool");
                            }
                        }
                    }

                    if (step.status() == RunStepObject_RunStepStatus_in_progress) {
                        // because only run objects with status of `queued` and `requires_action` are allowed to be added to task scheduler
                        // assert_true(i == n-1, "it should be last step.");
                        // assert_true(run_object.status() == RunObject_RunObjectStatus_requires_action, "run_object should be in status of requires_action");
                        if (i != n-1) {
                            LOG_ERROR("it should be last step.");
                            return false;
                        }
                        if (run_object.status() != RunObject_RunObjectStatus_requires_action) {
                            LOG_ERROR("run_object should be in status of requires_action");
                            return false;
                        }
                        // create pause
                        last_step = state.mutable_previous_steps()->Add();
                        last_pause = last_step->mutable_thought()->mutable_pause();
                        last_pause->mutable_openai()->mutable_tool_call_message()->CopyFrom(*tool_call_request);
                        // TODO support code-interpreter and file-search
                        // add tool messages for completed function tool calls, including those submitted by user
                        for (const auto& tool_call: step.step_details().tool_calls()) {
                            if (tool_call.type() == function && StringUtils::IsNotBlankString(tool_call.function().output())) {
                                auto* tool_messsage = last_pause->mutable_openai()->add_completed();
                                tool_messsage->set_content(tool_call.function().output());
                                tool_messsage->set_role("tool");
                                tool_messsage->set_tool_call_id(tool_call.id());
                            }
                        }
                        LOG_DEBUG("{}/{} completed tool calls in run step. thread_id={}, run_id={}, step_id={}", last_pause->openai().completed_size(), last_pause->openai().tool_call_message().tool_calls_size(), run_object.thread_id(), run_object.id(), step.id());
                    }

                    if (step.status() == RunStepObject_RunStepStatus_cancelled) {
                        // assert_true(i == n-1, "it should be last step");
                        // assert_true(run_object.status() == RunObject_RunObjectStatus_cancelled, "run object should be cancelled");
                        if (i != n-1) {
                            LOG_ERROR("it should be last step.");
                            return false;
                        }
                        if (run_object.status() != RunObject_RunObjectStatus_cancelled) {
                            LOG_ERROR("run_object should be in status of cancelled");
                            return false;
                        }
                        last_step = state.mutable_previous_steps()->Add();
                        auto* finish = last_step->mutable_thought()->mutable_finish();
                        finish->set_is_cancelled(true);
                    }

                    if (step.status() == RunStepObject_RunStepStatus_expired) {
                        // assert_true(i == n-1, "it should be last step");
                        // assert_true(run_object.status() == RunObject_RunObjectStatus_expired, "run object should be expired");
                        if (i != n-1) {
                            LOG_ERROR("it should be last step.");
                            return false;
                        }
                        if (run_object.status() != RunObject_RunObjectStatus_expired) {
                            LOG_ERROR("run_object should be in status of expired");
                            return false;
                        }
                        last_step = state.mutable_previous_steps()->Add();
                        auto* finish = last_step->mutable_thought()->mutable_finish();
                        finish->set_is_expired(true);
                    }

                    if (step.status() == RunStepObject_RunStepStatus_failed) {
                        // assert_true(i == n-1, "it should be last step");
                        // assert_true(run_object.status() == RunObject_RunObjectStatus_failed, "run object should be failed");
                        if (i != n-1) {
                            LOG_ERROR("it should be last step.");
                            return false;
                        }
                        if (run_object.status() != RunObject_RunObjectStatus_failed) {
                            LOG_ERROR("run_object should be in status of failed");
                            return false;
                        }
                        last_step = state.mutable_previous_steps()->Add();
                        auto* finish = last_step->mutable_thought()->mutable_finish();
                        finish->set_is_failed(true);
                        if (step.has_last_error()) {
                            RunEarlyStopDetails run_early_stop_details;
                            run_early_stop_details.mutable_error()->CopyFrom(step.last_error());
                            finish->mutable_details()->PackFrom(run_early_stop_details);
                        }
                    }

                }

                if (step.type() == RunStepObject_RunStepType_message_creation) {
                    if (i == n-1 && run_object.status() == RunObject_RunObjectStatus_completed) { // last message
                        if (const auto message_obj = GetMessageObject_(run_object.thread_id(), step.step_details().message_creation().message_id())) {
                            last_step = state.mutable_previous_steps()->Add();
                            last_step->mutable_thought()->mutable_finish()->set_response(message_obj->content().text().value());
                        }
                    }
                    // other messages are fetched in `tool_calls` branch
                }


            }
            return true;
        }


        [[nodiscard]] std::optional<MessageObject> GetMessageObject_(const std::string& thread_id, const std::string& message_id) const {
            GetMessageRequest get_message_request;
            get_message_request.set_thread_id(thread_id);
            get_message_request.set_message_id(message_id);
            return message_service_->RetrieveMessage(get_message_request);
        }

        [[nodiscard]] std::vector<RunStepObject> ListAllSteps_(const std::string& thread_id, const std::string& run_id) const {
            std::vector<RunStepObject> run_step_objects;
            ListRunStepsRequest list_run_steps_request;
            list_run_steps_request.set_order(asc);
            list_run_steps_request.set_run_id(run_id);
            list_run_steps_request.set_thread_id(thread_id);
            ListRunStepsResponse list_run_steps_resp;
            list_run_steps_resp.set_has_more(true);
            do {
                if (list_run_steps_resp.data_size() > 0) {
                    list_run_steps_request.set_after(list_run_steps_resp.data().rbegin()->id());
                }
                list_run_steps_resp = run_service_->ListRunSteps(list_run_steps_request);
                run_step_objects.insert(run_step_objects.end(), list_run_steps_resp.data().begin(), list_run_steps_resp.data().end());
            } while (list_run_steps_resp.has_more());
            return run_step_objects;
        }

        [[nodiscard]] std::optional<MessageObject> GetLatestUserMessageObject_(const std::string& thread_id) const {
            ListMessagesRequest list_message_request;
            list_message_request.set_thread_id(thread_id);
            list_message_request.set_order(desc);
            ListMessageResponse list_message_response;
            list_message_response.set_has_more(true);
            do {
                if (list_message_response.data_size()>0) {
                    list_message_request.set_after(list_message_response.data().rbegin()->id());
                }
                list_message_response = message_service_->ListMessages(list_message_request);
                for (const auto& msg: list_message_response.data()) {
                    if (msg.role() == user) {
                        return msg;
                    }
                }
            } while (list_message_response.has_more());
            return std::nullopt;
        }





    };
}


#endif //RUNOBJECTHANDLER_HPP