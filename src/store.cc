#include "threadpool.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <grpc++/grpc++.h>
#include "store.grpc.pb.h"
#include "vendor.grpc.pb.h"

using grpc::Channel;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerCompletionQueue;
using grpc::ClientAsyncResponseReader;
using grpc::CompletionQueue;
using grpc::ClientContext;
using store::ProductInfo;
using store::ProductQuery;
using store::ProductReply;
using store::StoreService;
using vendor::VendorService;
using vendor::BidQuery;
using vendor::BidReply;


void forwarder(void *context);

class VendorClient {
private:
	std::unique_ptr<VendorService::Stub> stub_;

public:
	VendorClient(std::shared_ptr<Channel> channel)
		: stub_(VendorService::NewStub(channel)) {}
	
	bool getProductBid(const std::string &product_name, ProductInfo *pinfo) {
		BidQuery query;
		BidReply reply;
		ClientContext context;
		CompletionQueue cq;
		Status status;

		query.set_product_name(product_name);
		std::unique_ptr<ClientAsyncResponseReader<BidReply> > rpc(
			stub_->AsyncgetProductBid(&context, query, &cq)
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

		pinfo->set_price(reply.price());
		pinfo->set_vendor_id(reply.vendor_id());

		return true;
	}
};


class StoreServiceImpl final {
private:
	std::unique_ptr<ServerCompletionQueue> cq_;
	StoreService::AsyncService service_;
	std::unique_ptr<Server> server_;
	std::vector<std::string> &ip_addresses_;
	threadpool *tp;

	class CallData {
	private:
		StoreService::AsyncService *service_;
		ServerCompletionQueue *cq_;
		ServerContext ctx_;
		ProductQuery request_;
		ProductReply reply_;
		ServerAsyncResponseWriter<ProductReply> responder_;
		enum CallStatus {CREATE, PROCESS, FINISH};
		CallStatus status_;
		std::vector<std::string> &ip_addresses_;

	public:
		CallData(StoreService::AsyncService *service, ServerCompletionQueue *cq, std::vector<std::string> &ip_addresses)
			: service_(service), cq_(cq), responder_(&ctx_), status_(CREATE), ip_addresses_(ip_addresses) {
			Proceed();
		}

		void Proceed() {
			if (status_ == CREATE) {
				status_ = PROCESS;
				std::cout << "Calldata proceed create\n";
				service_->RequestgetProducts(&ctx_, &request_, &responder_, cq_, cq_, this);
			} else if (status_ == PROCESS) {
				std::cout << "Calldata proceed process\n";
				new CallData(service_, cq_, ip_addresses_);

				std::vector<std::string>::iterator it;
				/* send a request for each vendor */
				for (it = ip_addresses_.begin(); it != ip_addresses_.end(); it++) {
					VendorClient vendor_client(grpc::CreateChannel(*it, grpc::InsecureChannelCredentials()));
					ProductInfo *pinfo;
					pinfo = reply_.add_products();
					vendor_client.getProductBid(request_.product_name(), pinfo); /* actual RPC call */
				}
				status_ = FINISH;
				responder_.Finish(reply_, Status::OK, this);
			} else {
				GPR_ASSERT(status_ == FINISH);
				delete this;
			}
		}
	};

public:
	StoreServiceImpl(std::vector<std::string> &ip_addresses, int nthreads)
		: ip_addresses_(ip_addresses) {
		tp = new threadpool(nthreads, threadwork);
	}
	
	~StoreServiceImpl() {
		server_->Shutdown();
		cq_->Shutdown();
	}

	void HandleRpcs() {
		new CallData(&service_, cq_.get(), ip_addresses_);
		void *tag;
		bool ok;
		while (true) {
			std::cout << "request sent\n";
			GPR_ASSERT(cq_->Next(&tag, &ok));
			GPR_ASSERT(ok);
			std::cout << "Request received\n";
			//static_cast<CallData *>(tag)->Proceed();
			tp->addRequest(tag); /* producer */
		}
	}

	static void threadwork(void *tag) {
		static_cast<CallData *>(tag)->Proceed();
	}

	void Run(const std::string &port) {
		std::string server_address("localhost:" + port);
		ServerBuilder builder;

		builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
		builder.RegisterService(&service_);

		cq_ = builder.AddCompletionQueue();
		server_ = builder.BuildAndStart();
		std::cout << "Server listening on " << server_address << std::endl;

		//new CallData(&service_, cq_.get(), ip_addresses_);
		HandleRpcs();
	}
};

int main(int argc, char *argv[])
{
	std::string filename = "./vendor_addresses.txt";
	std::string port;
	int nthreads;
	
	if (argc == 3) {
		//filename = std::string(argv[1]);
		port = std::string(argv[1]);
		nthreads = std::stoi(argv[2]);
	}

	std::vector<std::string> ip_addresses;
	std::ifstream myfile(filename);
	
	if (myfile.is_open()) {
		std::string ip_addr;
		while (getline(myfile, ip_addr))
			ip_addresses.push_back(ip_addr);
		myfile.close();
	} else {
		std::cerr << "Failed to open file " << filename << std::endl;
		return EXIT_FAILURE;
	}
	
	StoreServiceImpl store(ip_addresses, nthreads);
	store.Run(port);
	// store.create_threadpool(nthreads);
	
	return 0;
}
