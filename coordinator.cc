#include <vector>
#include <ctime>
#include <chrono>

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
    int port = -1;
    bool isActive = false;
};

std::vector<Server_Entry> master_table;
std::vector<Server_Entry> slave_table;
std::vector<Server_Entry> synchronizer_table;
std::map<std::string, google::protobuf::Timestamp> last_heartbeats;

// TODO: Main function
int main(int argc, char** argv) {
    return 0;
}
// TODO: Anything else heres

class CoordServiceImpl final : public Coordinator_Service::Service {
    Status HeartBeat(ServerContext* context, const Message* request, Reply* response) override {
        // Get Info from server
        std::string msg = request->msg();
        google::protobuf::Timestamp temptime = request->timestamp();
        std::string username = request->username();

        // Store time
        if (last_heartbeats.find(username) == last_heartbeats.end()) {
            last_heartbeats.insert({ username, temptime });
        }
        else {
            // Add timestamp check
            last_heartbeats.at(username) = temptime;
        }
        return Status::OK;
    }
};

int getServer(int client_id) {
    int serverID = (client_id % 3) + 1;
    if (master_table.at(serverID).isActive) {
        return master_table.at(serverID).port;
    }

    return slave_table.at(serverID).port;
}
int getFollowerSyncer(int client_id) {
    int serverID = (client_id % 3) + 1;
    return synchronizer_table.at(serverID).port;
}

// helper function
void check_heartbeats() {
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