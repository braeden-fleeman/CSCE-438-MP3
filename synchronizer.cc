#include <vector>
#include <ctime>
#include <chrono>
#include <thread>


#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>

#include "sns.grpc.pb.h"

using csce438::Coordinator_Service;
using csce438::ListReply;
using csce438::Message;
using csce438::Reply;
using csce438::Request;
using csce438::SNSService;
using google::protobuf::Duration;
using google::protobuf::Timestamp;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;



void RunServer(std::string port_no) {
    std::string server_address = "0.0.0.0:" + port_no;
    CoordServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Coordinator listening on " << server_address << std::endl;

    // Dispatch heartbeat handler
    std::thread heartbeat_check(check_heartbeats);
    heartbeat_check.detach();

    server->Wait();
}


int main(int argc, char** argv) {

    std::string port = "6009";
    int opt = 0;
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
        case 'p':
            port = optarg;break;
        default:
            std::cerr << "Invalid Command Line Argument\n";
        }
    }

    RunServer(port);

    return 0;
}