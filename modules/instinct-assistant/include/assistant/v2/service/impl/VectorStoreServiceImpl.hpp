//
// Created by RobinQu on 2024/5/27.
//

#ifndef VECTORSTORESERVICEIMPL_HPP
#define VECTORSTORESERVICEIMPL_HPP

#include "../IVectorStoreService.hpp"
#include "assistant/v2/data_mapper/VectorStoreDataMapper.hpp"
#include "assistant/v2/data_mapper/VectorStoreFileBatchDataMapper.hpp"
#include "assistant/v2/data_mapper/VectorStoreFileDataMapper.hpp"
#include "assistant/v2/task_handler/FileObjectTaskHandler.hpp"
#include "assistant/v2/tool/RetrieverOperator.hpp"
#include "task_scheduler/ThreadPoolTaskScheduler.hpp"

namespace INSTINCT_ASSISTANT_NS::v2 {

    class VectorStoreServiceImpl final: public IVectorStoreService {
        VectorStoreFileDataMapperPtr vector_store_file_data_mapper_;
        VectorStoreDataMapperPtr vector_store_data_mapper_;
        VectorStoreFileBatchDataMapperPtr vector_store_file_batch_data_mapper_;
        CommonTaskSchedulerPtr task_scheduler_;
        RetrieverOperatorPtr retriever_operator_;

    public:
        VectorStoreServiceImpl(
            VectorStoreFileDataMapperPtr vector_store_file_data_mapper,
            VectorStoreDataMapperPtr vector_store_data_mapper,
            VectorStoreFileBatchDataMapperPtr vector_store_file_batch_data_mapper,
            CommonTaskSchedulerPtr task_scheduler,
            RetrieverOperatorPtr retriever_operator)
            : vector_store_file_data_mapper_(std::move(vector_store_file_data_mapper)),
              vector_store_data_mapper_(std::move(vector_store_data_mapper)),
              vector_store_file_batch_data_mapper_(std::move(vector_store_file_batch_data_mapper)),
              task_scheduler_(std::move(task_scheduler)),
              retriever_operator_(std::move(retriever_operator)) {
        }

        ListVectorStoresResponse ListVectorStores(const ListVectorStoresRequest &req) override {
            trace_span span {"ListVectorStores"};
            return vector_store_data_mapper_->ListVectorStores(req);
        }

        std::optional<VectorStoreObject> CreateVectorStore(const CreateVectorStoreRequest &req) override {
            trace_span span {"CreateVectorStore"};
            const auto pk = vector_store_data_mapper_->InsertVectorStore(req);
            assert_true(vector_store_data_mapper_->InsertVectorStore(req), "should have object inserted");
            const auto vector_store_object = vector_store_data_mapper_->GetVectorStore(pk.value());
            assert_true(vector_store_object, "should have found created VectorStore");
            assert_true(retriever_operator_->ProvisionRetriever(vector_store_object.value()), "db instance should be created");
            if(req.file_ids_size()>0) {
                vector_store_file_data_mapper_->InsertManyVectorStoreFiles(vector_store_object->id(), req.file_ids());
                if(task_scheduler_) {
                    // trigger background jobs for file objects
                    for(const auto files = vector_store_file_data_mapper_->ListVectorStoreFiles(vector_store_object->id(), req.file_ids()); const auto& file: files) {
                        task_scheduler_->Enqueue({
                            .task_id = vector_store_object->id(),
                            .category = FileObjectTaskHandler::CATEGORY,
                            .payload = ProtobufUtils::Serialize(file)
                        });
                    }
                }
            }
            return vector_store_object;
        }

        std::optional<VectorStoreObject> GetVectorStore(const GetVectorStoreRequest &req) override {
            trace_span span {"GetVectorStore"};
            auto vs = vector_store_data_mapper_->GetVectorStore(req.vector_store_id());
            if (!vs) {
                return std::nullopt;
            }
            // TODO need transaction
            const auto counts = vector_store_file_data_mapper_->CountVectorStoreFiles(req.vector_store_id());
            vs->mutable_file_counts()->CopyFrom(counts);
            return vs;
        }

        std::optional<VectorStoreObject> ModifyVectorStore(const ModifyVectorStoreRequest &req) override {
            trace_span span {"ModifyVectorStore"};
            assert_true(vector_store_data_mapper_->UpdateVectorStore(req) == 1, "should have vector object updated");
            GetVectorStoreRequest get_vector_store_request;
            get_vector_store_request.set_vector_store_id(req.vector_store_id());
            return GetVectorStore(get_vector_store_request);
        }

        DeleteVectorStoreResponse DeleteVectorStore(const DeleteVectorStoreRequest &req) override {
            trace_span span {"DeleteVectorStore"};
            // TODO need transaction
            const auto vector_store_object = vector_store_data_mapper_->GetVectorStore(req.vector_store_id());
            assert_true(vector_store_object, fmt::format("should have found VectorStoreObject with request {}", req.ShortDebugString()));
            const auto count = vector_store_file_data_mapper_->CountVectorStoreFiles(req.vector_store_id());
            const bool is_removable = count.in_progress() == 0;
            DeleteVectorStoreResponse response;
            response.set_id(req.vector_store_id());
            if (is_removable) {
                const auto deleted_count = vector_store_file_data_mapper_->DeleteVectorStoreFiles(req.vector_store_id());
                LOG_DEBUG("Cascade delete {} files in VectorStore {}", deleted_count, req.vector_store_id());
                assert_true(vector_store_data_mapper_->DeleteVectorStore(req) == 1, "should have VectorStore deleted");
                response.set_deleted(retriever_operator_->CleanupRetriever(vector_store_object.value()));
            } else {
                response.set_deleted(false);
            }
            return response;
        }

        ListVectorStoreFilesResponse ListVectorStoreFiles(const ListVectorStoreFilesRequest &req) override {
            trace_span span {"ListVectorStoreFiles"};
            return vector_store_file_data_mapper_->ListVectorStoreFiles(req);
        }

        std::optional<VectorStoreFileObject> CreateVectorStoreFile(const CreateVectorStoreFileRequest &req) override {
            trace_span span {"CreateVectorStoreFile"};
            assert_not_blank(req.file_id(), "should provide file_id");
            assert_not_blank(req.vector_store_id(), "should provide vector_store_id");
            assert_true(vector_store_file_data_mapper_->InsertVectorStoreFile(req), "should have vector store file created");
            GetVectorStoreFileRequest get_request;
            get_request.set_vector_store_id(req.vector_store_id());
            get_request.set_file_id(req.file_id());
            const auto file_object = GetVectorStoreFile(get_request);
            if (task_scheduler_) {
                task_scheduler_->Enqueue({
                    .task_id = file_object->id(),
                    .category = FileObjectTaskHandler::CATEGORY,
                    .payload = ProtobufUtils::Serialize(file_object.value())
                });
            }
            return file_object;
        }

        std::optional<VectorStoreFileObject> GetVectorStoreFile(const GetVectorStoreFileRequest &req) override {
            trace_span span {"GetVectorStoreFile"};
            assert_not_blank(req.vector_store_id(), "should have provide vector_store_id");
            assert_not_blank(req.file_id(), "should have provide file_id");
            return vector_store_file_data_mapper_->GetVectorStoreFile(req.vector_store_id(), req.file_id());
        }

        DeleteVectorStoreFileResponse DeleteVectorStoreFile(const DeleteVectorStoreFileRequest &req) override {
            trace_span span {"DeleteVectorStoreFile"};
            assert_not_blank(req.vector_store_id(), "should have provide vector_store_id");
            assert_not_blank(req.file_id(), "should have provide file_id");
            const auto count = vector_store_file_data_mapper_->DeleteVectorStoreFile(req.vector_store_id(), req.file_id());
            DeleteVectorStoreFileResponse response;
            response.set_id(req.file_id());
            response.set_object("vector_store.file.deleted");
            response.set_deleted(count == 1);
            const auto vector_store_object = vector_store_data_mapper_->GetVectorStore(req.vector_store_id());
            const auto retriever = retriever_operator_->GetStatefulRetriever(vector_store_object.value());
            SearchQuery filter;
            auto* file_id_term = filter.mutable_term();
            file_id_term->set_name(VECTOR_STORE_FILE_ID_KEY);
            file_id_term->mutable_term()->set_string_value(req.file_id());
            retriever->Remove(filter);
            return response;
        }

        std::optional<VectorStoreFileObject> ModifyVectorStoreFile(const ModifyVectorStoreFileRequest &req) override {
            trace_span span {"ModifyVectorStoreFile"};
            assert_not_blank(req.file_id(), "should provide file_id");
            assert_not_blank(req.vector_store_id(), "should provide vector_store_id");
            assert_true(vector_store_file_data_mapper_->GetVectorStoreFile(req.vector_store_id(), req.file_id()), "should have found VectorStoreFileObject before update");
            assert_true(vector_store_file_data_mapper_->UpdateVectorStoreFile(req) == 1, "should have VectorStoreFile updated");
            return vector_store_file_data_mapper_->GetVectorStoreFile(req.vector_store_id(), req.file_id());
        }

        std::optional<VectorStoreFileBatchObject>
        CreateVectorStoreFileBatche(const CreateVectorStoreFileBatchRequest &req) override {
            trace_span span {"CreateVectorStoreFileBatchRequest"};
            assert_true(req.file_ids_size()>0, "should provide at least one file_id");
            assert_not_blank(req.vector_store_id(), "should provide valid vector_store_id");
            const auto pk = vector_store_file_batch_data_mapper_->InsertVectorStoreFileBatch(req);
            assert_true(pk, "should have VectorStoreFileBatch inserted");
            // create VectorStoreFileObject
            vector_store_file_data_mapper_->InsertManyVectorStoreFiles(req.vector_store_id(), req.file_ids());

            // trigger file object jobs
            if (task_scheduler_) {
                for(const auto files = vector_store_file_data_mapper_->ListVectorStoreFiles(req.vector_store_id(), req.file_ids()); const auto& file: files) {
                    task_scheduler_->Enqueue({
                        .task_id = req.vector_store_id(),
                        .category = FileObjectTaskHandler::CATEGORY,
                        .payload = ProtobufUtils::Serialize(file)
                    });
                }
            }

            return vector_store_file_batch_data_mapper_->GetVectorStoreFileBatch(req.vector_store_id(), pk.value());
        }

        std::optional<VectorStoreFileBatchObject> GetVectorStoreFileBatch(const GetVectorStoreFileBatchRequest &req) override {
            trace_span span {"GetVectorStoreFileBatch"};
            assert_not_blank(req.vector_store_id(), "should provide valid vector_store_id");
            assert_not_blank(req.batch_id(), "should provide valid batch_id");
            return vector_store_file_batch_data_mapper_->GetVectorStoreFileBatch(req.vector_store_id(), req.batch_id());
        }

        std::optional<VectorStoreFileBatchObject>
        CancelVectorStoreFileBatch(const CancelVectorStoreFileBatchRequest &req) override {
            trace_span span {"CancelVectorStoreFileBatchRequest"};
            assert_not_blank(req.batch_id(), "should have non-blank");
            assert_true(vector_store_file_batch_data_mapper_->UpdateVectorStoreFileBatch(req.vector_store_id(), req.batch_id(), VectorStoreFileBatchObject_VectorStoreFileBatchStatus_cancelled) == 1, "should have VectorStoreFileBatch updated");
            return vector_store_file_batch_data_mapper_->GetVectorStoreFileBatch(req.vector_store_id(), req.batch_id());
        }

        ListFilesInVectorStoreBatchResponse
        ListFilesInVectorStoreBatch(const ListFilesInVectorStoreBatchRequest &req) override {
            trace_span span {"ListFilesInVectorStoreBatch"};
            assert_not_blank(req.vector_store_id(), "should provide valid vector_store_id");
            assert_not_blank(req.batch_id(), "should provide valid batch_id");
            return vector_store_file_data_mapper_->ListVectorStoreFiles(req);
        }
    };
}


#endif //VECTORSTORESERVICEIMPL_HPP
