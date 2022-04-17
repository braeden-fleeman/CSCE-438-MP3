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

struct Server_Entry
{
    int server_ID = -1;
    std::string port = "";
    bool isActive = false;
};

std::map<int, Server_Entry> master_table;
std::map<int, Server_Entry> slave_table;
std::map<int, Server_Entry> synchronizer_table;
std::map<std::string, google::protobuf::Timestamp> last_heartbeats;


int getServer(int client_id);
int getSlaveServer(int master_id);
void check_heartbeats();


// TODO: Anything else heres

class CoordServiceImpl final : public Coordinator_Service::Service {
    Status HeartBeat(ServerContext* context, const Message* request, Reply* response) override {
        // Get Info from server
        std::string msg = request->msg();
        google::protobuf::Timestamp temptime = request->timestamp();
        // username = server_id
        std::string username = request->username();

        // Store time
        if (last_heartbeats.find(username) == last_heartbeats.end()) {
            std::cout << "creating table for port: " << request->msg() << std::endl;
            // Put server in heartbeat table
            last_heartbeats.insert({ username, temptime });

            // Build the server
            Server_Entry server;
            int underscorePos = request->username().find("_");
            std::string server_type = request->username().substr(0, underscorePos);
            int serverIndex = stoi(request->username().substr(underscorePos + 1));
            server.isActive = true;
            server.server_ID = serverIndex;
            server.port = request->msg();
            if (server_type == "master") {
                master_table.insert({ serverIndex, server });
            }
            else {
                slave_table.insert({ serverIndex, server });
            }
        }
        else {
            // Add timestamp check
            last_heartbeats.at(username) = temptime;
        }
        return Status::OK;
    }

    Status HandleClient(ServerContext* context, const Request* request, Reply* reply) override {
        std::string username = request->username();

        try {
            int userID = stoi(username);
            int server_port = getServer(userID);
            reply->set_msg(std::to_string(server_port));

        }
        catch (const std::exception& e) {
            std::cout << e.what() << std::endl;
            Status stat = Status(grpc::StatusCode::UNKNOWN, "Failure in HandleClient");
            return stat;
        }

        return Status::OK;
    }

    Status HandleSlave(ServerContext* context, const Request* request, Reply* reply) override {
        std::string username = request->username();

        try {
            int serverIndex = stoi(request->username());
            reply->set_msg(std::to_string(getSlaveServer(serverIndex)));
        }
        catch (const std::exception& e) {
            std::cout << e.what() << std::endl;
            Status stat = Status(grpc::StatusCode::UNKNOWN, "Failure in HandleClient");
            return stat;
        }

        return Status::OK;
    }

    Status HandleSynchronizer(ServerContext* context, const Request* request, Reply* reply) override {
        std::string username = request->username();

        try {
            int serverIndex = stoi(request->username());
            reply->set_msg(std::to_string(getSynchronizer(serverIndex)));
        }
        catch (const std::exception& e) {
            std::cout << e.what() << std::endl;
            Status stat = Status(grpc::StatusCode::UNKNOWN, "Failure in HandleClient");
            return stat;
        }

        return Status::OK;
    }

};

// Returns port of server 
int getServer(int client_id) {
    int serverID = (client_id % 3);
    if (master_table.at(serverID).isActive) {
        std::cout << "master table" << std::endl;
        return stoi(master_table.at(serverID).port);
    }
    // Master is inactive. Route to slave from here on.
    std::cout << "slave table" << std::endl;
    return stoi(slave_table.at(serverID).port);
}
int getFollowerSyncer(int client_id) {
    int serverID = (client_id % 3) + 1;
    return stoi(synchronizer_table.at(serverID).port);
}

int getSlaveServer(int master_id) {
    return stoi(slave_table.at(master_id).port);
}

// helper function
void check_heartbeats() {
    // Every 5 seconds, check table: if not valid timestamp then declare server dead
    while (true) {
        sleep(5);
        for (std::map<std::string, google::protobuf::Timestamp>::iterator it = last_heartbeats.begin(); it != last_heartbeats.end(); ++it) {
            auto lastHB = google::protobuf::util::TimeUtil::TimestampToSeconds(it->second);
            auto currentTime = google::protobuf::util::TimeUtil::TimestampToSeconds(google::protobuf::util::TimeUtil::GetCurrentTime());
            if (currentTime - lastHB >= 20) {
                // set server to inactive
                int underscorePos = it->first.find("_");
                std::string server_type = it->first.substr(0, underscorePos);
                int serverIndex = stoi(it->first.substr(underscorePos + 1));
                if (server_type == "master") {
                    master_table.at(serverIndex).isActive = false;
                }
                // else {
                //     slave_table.at(serverIndex).isActive = false;
                // }
            }
        }
    }
}


/*
DEFAULT PORTS:
M1: 3010, S1: 3011
M2: 3012, S2: 3013
M3: 3014. S3: 3015

C: 6009
*/

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