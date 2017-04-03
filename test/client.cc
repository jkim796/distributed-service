#include <memory>
#include <stdlib.h> 

#include <grpc++/grpc++.h>

#include "../src/store.grpc.pb.h"

#include "product_queries_util.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::ClientAsyncResponseReader;
using grpc::CompletionQueue;
using store::ProductQuery;
using store::ProductReply;
using store::ProductInfo;
using store::StoreService;

class StoreClient {
public:
	StoreClient(std::shared_ptr<Channel> channel)
		: stub_(StoreService::NewStub(channel)) {}
	bool getProducts(const ProductSpec&, ProductQueryResult&);

private:
	std::unique_ptr<StoreService::Stub> stub_;
};

/* asynchronous version */
bool StoreClient::getProducts(const ProductSpec& product_spec, ProductQueryResult& query_result) {
	ProductQuery query;
	ProductReply reply;
	ClientContext context;
	CompletionQueue cq;
	Status status;

	query.set_product_name(product_spec.name_);
	std::unique_ptr<ClientAsyncResponseReader<ProductReply> > rpc(
		stub_->AsyncgetProducts(&context, query, &cq)
		);
	rpc->Finish(&reply, &status, (void *) 1);
	void *got_tag;
	bool ok = false;

	GPR_ASSERT(cq.Next(&got_tag ,&ok));
	GPR_ASSERT(got_tag == (void *) 1);
	GPR_ASSERT(ok);

	if (!status.ok()) {
		std::cout << status.error_code() << ": " << status.error_message()
			  << std::endl;
		return false;
	}

	for (const auto result : reply.products()) {
		ProductQueryResult::Bid bid;
		bid.price_ = result.price();
		bid.vendor_id_ = result.vendor_id();
		query_result.bids_.push_back(bid);
	}
	return true;
}

bool run_client(const std::string& server_addr, const std::string& product_name, ProductQueryResult& pq_result) {
	StoreClient store_client(grpc::CreateChannel(server_addr, grpc::InsecureChannelCredentials()));
	ProductSpec spec(product_name);
	//return store_client.getProducts(product_name, pq_result);
	return store_client.getProducts(spec, pq_result);
}
