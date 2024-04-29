//
// Created by RobinQu on 2024/4/19.
//

#ifndef THREADCONTROLLER_HPP
#define THREADCONTROLLER_HPP

#include "BaseController.hpp"

namespace INSTINCT_ASSISTANT_NS::v2 {
    class ThreadController final: public BaseController {
    public:
        explicit ThreadController(const AssistantFacade &facade)
            : BaseController(facade) {
        }

        void Mount(HttpLibServer &server) override {
            server.PostRoute<ThreadObject, ThreadObject>("/v1/threads", [&](const ThreadObject& req, const HttpLibSession& session) {
                if (const auto resp = facade_.thread->CreateThread(req); resp.has_value()) {
                    session.Respond(resp.value());
                } else {
                    session.Respond("Thread is not retrieved after createion", 500);
                }
            });

            server.GetRoute<GetThreadRequest, ThreadObject>("/v1/threads/:thread_id", [&](const GetThreadRequest& req, const HttpLibSession& session) {
                if (const auto resp = facade_.thread->RetrieveThread(req); resp.has_value()) {
                    session.Respond(resp.value());
                } else {
                    session.Respond(fmt::format("Thread is not found with thread_id {}", req.thread_id()), 404);
                }
            });

            server.PostRoute<ModifyThreadRequest, ThreadObject>("/v1/threads/:thread_id", [&](const ModifyThreadRequest& req, const HttpLibSession& session) {
                if (const auto resp = facade_.thread->ModifyThread(req); resp.has_value()) {
                    session.Respond(resp.value());
                } else {
                    session.Respond("Thread is not retrieved after createion", 500);
                }
            });

            server.DeleteRoute<DeleteThreadRequest, DeleteThreadResponse>("/v1/threads/:thread_id", [&](const DeleteThreadRequest& req, const HttpLibSession& session) {
                const auto resp = facade_.thread->DeleteThread(req);
                session.Respond(resp);
            });

        }
    };
}



#endif //THREADCONTROLLER_HPP
