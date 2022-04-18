#include <vector>
#include <ctime>
#include <chrono>
#include <thread>
#include <mutex>

std::mutex mtx; // incoming file
std::mutex o_mtx; // outgoing file

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <vector>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>

#include "sns.grpc.pb.h"

using csce438::Synchronizer_Service;
using csce438::ListReply;
using csce438::Update;
using csce438::Message;
using csce438::Reply;
using csce438::Request;
using csce438::ListPorts;
using csce438::SNSService;
using google::protobuf::Duration;
using google::protobuf::Timestamp;
using google::protobuf::util::TimeUtil;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ClientContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using csce438::Coordinator_Service;

// Coord stub
std::unique_ptr<Coordinator_Service::Stub> coord_stub;

// FS stub
std::unique_ptr<Synchronizer_Service::Stub> sync_stub;

std::string coord_ip = "0.0.0.0";
std::string coord_port = "6009";
std::string sync_port = "4000";
std::string sync_id = "-1";

/*
 * Splits a string into a vector based on the character given
 */
std::vector<std::string> split(std::string line, char separator) {
    std::vector<std::string> result;
    while (line.size()) {
        size_t found = line.find_first_of(separator);
        if (found != std::string::npos) {
            std::string part = line.substr(0, found);
            result.push_back(part);
            line = line.substr(found + 1);
        }
        else {
            result.push_back(line);
            break;
        }
    }
    return result;
}

class SyncServiceImpl final : public Synchronizer_Service::Service {
    Status SyncUpdate(ServerContext* context, const Update* request, Reply* reply) {
        // Incoming update, write to local file
        // master_1_incoming.txt
        // slave_1_incoming.txt
        std::string msg = request->msg();

        // Handle master incoming file
        std::string filename = "./master_" + sync_id + "/incoming.txt";
        // Lock
        mtx.lock();
        std::ofstream m_file(filename, std::ios::app | std::ios::out | std::ios::in);
        if (!m_file.is_open()) {
            std::cout << "Error opening file: " << filename << std::endl;
        }

        m_file << msg << std::endl;
        m_file.close();

        // Unlock
        mtx.unlock();


        // Handle slave incoming file
        filename = "./slave_" + sync_id + "/incoming.txt";
        mtx.lock();
        std::ofstream s_file(filename, std::ios::app | std::ios::out | std::ios::in);
        if (!s_file.is_open()) {
            std::cout << "Error opening file: " << filename << std::endl;
        }
        s_file << msg << std::endl;
        s_file.close();

        // Unlock
        mtx.unlock();
    }
};

void RunServer(std::string port_no) {
    std::string server_address = "0.0.0.0:" + sync_port;
    SyncServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Synchronizer listening on " << server_address << std::endl;

    // Send heartbeat to initialize in coordinator
    std::string login_info = coord_ip + ":" + coord_port;
    auto coord_stub = std::unique_ptr<Coordinator_Service::Stub>(Coordinator_Service::NewStub(
        grpc::CreateChannel(
            login_info, grpc::InsecureChannelCredentials())));
    ClientContext context;
    Message message;
    message.set_username("sync_" + sync_id);
    message.set_msg(sync_port);
    TimeUtil time_util;
    Reply reply;
    Timestamp* ts = new Timestamp(time_util.GetCurrentTime());
    message.set_allocated_timestamp(ts);
    Status status = coord_stub->HeartBeat(&context, message, &reply);

    // Startup outgoing file manager
    std::thread outgoing_manager(outgoingUpdater);
    outgoing_mangaer.detach();

    // Server handles incoming updates
    server->Wait();
}

void handleMasterOutgoing() {
    // Handle master outgoing file
    std::string filename = "./master_" + sync_id + "/outgoing.txt";

    o_mtx.lock();
    std::ifstream m_out_file(filename, std::ios::app | std::ios::out | std::ios::in);

    std::string line;
    while (getline(m_out_file, line)) {
        std::vector<std::string> vect = split(line, ',');
        std::string cmd = vect.at(0);
        std::string from = vect.at(1);
        if (cmd == "LOGIN") {
            // Get port list from coord
            std::string login_info = coord_ip + ":" + coord_port;
            auto coord_stub = std::unique_ptr<Coordinator_Service::Stub>(Coordinator_Service::NewStub(
                grpc::CreateChannel(
                    login_info, grpc::InsecureChannelCredentials())));
            ClientContext context;
            Request request;
            ListPorts port_list;
            coord_stub->GetAllSynchronizers(&context, request, &port_list);

            // Forward to all FSs
            for (int i = 0; i < port_list.ports_size(); i++) {
                std::string port = port_list.ports(i);
                std::string login_info = "0.0.0.0:" + port;
                auto sync_stub = std::unique_ptr<Synchronizer_Service::Stub>(Synchronizer_Service::NewStub(
                    grpc::CreateChannel(
                        login_info, grpc::InsecureChannelCredentials())));
                ClientContext context;
                Update update;
                update.set_msg(line);
                Reply reply;

                sync_stub->SyncUpdate(&context, update, &reply);
            }
        }
        else if (cmd == "FOLLOW" || cmd == "TIMELINE") {
            std::string user_to = vect.at(2);
            std::string login_info = coord_ip + ":" + coord_port;
            auto coord_stub = std::unique_ptr<Coordinator_Service::Stub>(Coordinator_Service::NewStub(
                grpc::CreateChannel(
                    login_info, grpc::InsecureChannelCredentials())));
            ClientContext context;
            Request request;
            request.set_username(user_to);
            Reply reply;
            coord_stub->HandleSynchronizer(&context, request, &reply);
            std::string port = reply.msg();
            std::string login_info = "0.0.0.0:" + port;
            auto sync_stub = std::unique_ptr<Synchronizer_Service::Stub>(Synchronizer_Service::NewStub(
                grpc::CreateChannel(
                    login_info, grpc::InsecureChannelCredentials())));
            ClientContext sync_context;
            Update update;
            update.set_msg(line);
            Reply sync_reply;

            sync_stub->SyncUpdate(&sync_context, update, &sync_reply);
        }

        o_mtx.lock();
    }
    // Clear outgoing file
    m_out_file.close();
}

void handleSlaveOutgoing() {
    // Handle master outgoing file
    std::string filename = "./slave_" + sync_id + "/outgoing.txt";

    o_mtx.lock();
    std::ifstream m_out_file(filename, std::ios::app | std::ios::out | std::ios::in);

    std::string line;
    while (getline(m_out_file, line)) {
        std::vector<std::string> vect = split(line, ',');
        std::string cmd = vect.at(0);
        std::string from = vect.at(1);
        if (cmd == "LOGIN") {
            // Get port list from coord
            std::string login_info = coord_ip + ":" + coord_port;
            auto coord_stub = std::unique_ptr<Coordinator_Service::Stub>(Coordinator_Service::NewStub(
                grpc::CreateChannel(
                    login_info, grpc::InsecureChannelCredentials())));
            ClientContext context;
            Request request;
            ListPorts port_list;
            coord_stub->GetAllSynchronizers(&context, request, &port_list);

            // Forward to all FSs
            for (int i = 0; i < port_list.ports_size(); i++) {
                std::string port = port_list.ports(i);
                std::string login_info = "0.0.0.0:" + port;
                auto sync_stub = std::unique_ptr<Synchronizer_Service::Stub>(Synchronizer_Service::NewStub(
                    grpc::CreateChannel(
                        login_info, grpc::InsecureChannelCredentials())));
                ClientContext context;
                Update update;
                update.set_msg(line);
                Reply reply;

                sync_stub->SyncUpdate(&context, update, &reply);
            }
        }
        else if (cmd == "FOLLOW" || cmd == "TIMELINE") {
            std::string user_to = vect.at(2);
            std::string login_info = coord_ip + ":" + coord_port;
            auto coord_stub = std::unique_ptr<Coordinator_Service::Stub>(Coordinator_Service::NewStub(
                grpc::CreateChannel(
                    login_info, grpc::InsecureChannelCredentials())));
            ClientContext context;
            Request request;
            request.set_username(user_to);
            Reply reply;
            coord_stub->HandleSynchronizer(&context, request, &reply);
            std::string port = reply.msg();
            std::string login_info = "0.0.0.0:" + port;
            auto sync_stub = std::unique_ptr<Synchronizer_Service::Stub>(Synchronizer_Service::NewStub(
                grpc::CreateChannel(
                    login_info, grpc::InsecureChannelCredentials())));
            ClientContext sync_context;
            Update update;
            update.set_msg(line);
            Reply sync_reply;

            sync_stub->SyncUpdate(&sync_context, update, &sync_reply);
        }

        o_mtx.lock();
    }
    m_out_file.close();
}

void outgoingUpdater() {
    while (true) {
        handleMasterOutgoing();
        handleSlaveOutgoing();
        // Clear files
        o_mtx.lock();
        std::ofstream m_file("./master_" + sync_id + "/outgoing.txt", std::ios::trunc | std::ios::out);
        m_file.close();
        std::ofstream s_file "./slave_" + sync_id + "/outgoing.txt", std::ios::trunc | std::ios::out);
        s_file.close();
        o_mtx.unlock();
        sleep(10);

    }
}

int main(int argc, char** argv) {
    int opt = 0;
    while ((opt = getopt(argc, argv, "h:p:s:u:")) != -1) {
        switch (opt) {
        case 'h':
            coord_ip = optarg;break;
        case 'p':
            coord_port = optarg;break;
        case 's':
            sync_port = optarg;break;
        case 'u':
            sync_id = optarg;break;
        default:
            std::cerr << "Invalid Command Line Argument\n";
        }
    }

    RunServer(sync_port);

    return 0;
}