/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <ctime>

#include <thread>
#include <sys/types.h>
#include <sys/stat.h>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>

#include "sns.grpc.pb.h"

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using google::protobuf::util::TimeUtil;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ClientContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using csce438::Message;
using csce438::ListReply;
using csce438::Request;
using csce438::Reply;
using csce438::Update;
using csce438::SNSService;
using csce438::Coordinator_Service;

struct Client {
  std::string username;
  bool connected = true;
  int following_file_size = 0;
  std::vector<Client*> client_followers;
  std::vector<Client*> client_following;
  ServerReaderWriter<Message, Message>* stream = 0;
  bool operator==(const Client& c1) const {
    return (username == c1.username);
  }
};

//Vector that stores every client that has been created
std::vector<Client> client_db;

// Server Type & ID
std::string server_type;
std::string server_id;

// Stub to slave
std::unique_ptr<SNSService::Stub> stub_;

//Helper function used to find a Client object given its username
int find_user(std::string username) {
  int index = 0;
  for (Client c : client_db) {
    if (c.username == username)
      return index;
    index++;
  }
  return -1;
}

void heartbeat_handler(std::string coord_ip, std::string coord_port, std::string port_no) {
  // Connect to the coordinator
  std::string login_info = coord_ip + ":" + coord_port;
  auto coord_stub = std::unique_ptr<Coordinator_Service::Stub>(Coordinator_Service::NewStub(
    grpc::CreateChannel(
      login_info, grpc::InsecureChannelCredentials())));

  // Build message
  Message message;
  message.set_username(server_type + "_" + server_id);
  message.set_msg(port_no);
  TimeUtil time_util;
  while (true) {
    Reply reply;
    ClientContext context;
    // Contact coordinator every ten seconds (Call heartbeat function)
    Timestamp* ts = new Timestamp(time_util.GetCurrentTime());
    message.set_allocated_timestamp(ts);
    Status status = coord_stub->HeartBeat(&context, message, &reply);
    sleep(10);
  }
}

void ClusterUpdates() {}


// TODO: Slave/Master Communication
// TODO: Update files

class SNSServiceImpl final : public SNSService::Service {

  Status List(ServerContext* context, const Request* request, ListReply* list_reply) override {
    Client user = client_db[find_user(request->username())];
    int index = 0;
    for (Client c : client_db) {
      list_reply->add_all_users(c.username);
    }
    std::vector<Client*>::const_iterator it;
    for (it = user.client_followers.begin(); it != user.client_followers.end(); it++) {
      list_reply->add_followers((*it)->username);
    }
    return Status::OK;
  }

  Status Follow(ServerContext* context, const Request* request, Reply* reply) override {
    std::string username1 = request->username();
    std::string username2 = request->arguments(0);
    int join_index = find_user(username2);
    if (join_index < 0 || username1 == username2)
      reply->set_msg("unkown user name");
    else {
      Client* user1 = &client_db[find_user(username1)];
      Client* user2 = &client_db[join_index];
      if (std::find(user1->client_following.begin(), user1->client_following.end(), user2) != user1->client_following.end()) {
        reply->set_msg("you have already joined");
        return Status::OK;
      }
      user1->client_following.push_back(user2);
      user2->client_followers.push_back(user1);
      reply->set_msg("Follow Successful");

    }

    if (server_type == "master") {
      Request m_request;
      m_request.set_username(username1);
      m_request.add_arguments(username2);
      ClientContext m_context;
      Reply m_reply;
      stub_->Follow(&m_context, m_request, &m_reply);
    }
    return Status::OK;
  }

  // Deprecated Usage
  Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override {
    std::string username1 = request->username();
    std::string username2 = request->arguments(0);
    int leave_index = find_user(username2);
    if (leave_index < 0 || username1 == username2)
      reply->set_msg("unknown follower username");
    else {
      Client* user1 = &client_db[find_user(username1)];
      Client* user2 = &client_db[leave_index];
      if (std::find(user1->client_following.begin(), user1->client_following.end(), user2) == user1->client_following.end()) {
        reply->set_msg("you are not follower");
        return Status::OK;
      }
      user1->client_following.erase(find(user1->client_following.begin(), user1->client_following.end(), user2));
      user2->client_followers.erase(find(user2->client_followers.begin(), user2->client_followers.end(), user1));
      reply->set_msg("UnFollow Successful");
    }
    return Status::OK;
  }

  Status Login(ServerContext* context, const Request* request, Reply* reply) override {
    Client c;
    std::string username = request->username();
    int user_index = find_user(username);
    if (user_index < 0) {
      c.username = username;
      client_db.push_back(c);
      reply->set_msg("Login Successful!");
    }
    else {
      Client* user = &client_db[user_index];
      if (user->connected) {
        reply->set_msg("Invalid Username");
      }
      else {
        std::string msg = "Welcome Back " + user->username;
        reply->set_msg(msg);
        user->connected = true;
      }
    }

    if (server_type == "master") {
      Request m_request;
      m_request.set_username(username);
      ClientContext m_context;
      Reply m_reply;
      stub_->Login(&m_context, m_request, &m_reply);
    }
    return Status::OK;
  }

  Status Timeline(ServerContext* context,
    ServerReaderWriter<Message, Message>* stream) override {
    Message message;
    Client* c;
    while (stream->Read(&message)) {
      std::string username = message.username();
      int user_index = find_user(username);
      c = &client_db[user_index];

      //Write the current message to "username.txt"
      std::string filename = "./" + server_type + "_" + server_id + "/" + username + ".txt";
      std::ofstream user_file(filename, std::ios::app | std::ios::out | std::ios::in);
      google::protobuf::Timestamp temptime = message.timestamp();
      std::string time = google::protobuf::util::TimeUtil::ToString(temptime);
      std::string fileinput = time + " :: " + message.username() + ":" + message.msg() + "\n";
      //"Set Stream" is the default message from the client to initialize the stream
      if (message.msg() != "Set Stream")
        user_file << fileinput;
      //If message = "Set Stream", print the first 20 chats from the people you follow
      else {
        if (c->stream == 0)
          c->stream = stream;
        std::string line;
        std::vector<std::string> newest_twenty;
        std::ifstream in("./" + server_type + "_" + server_id + "/" + username + "following.txt");
        int count = 0;
        //Read the last up-to-20 lines (newest 20 messages) from userfollowing.txt
        while (getline(in, line)) {
          if (c->following_file_size > 20) {
            if (count < c->following_file_size - 20) {
              count++;
              continue;
            }
          }
          newest_twenty.push_back(line);
        }
        Message new_msg;
        //Send the newest messages to the client to be displayed
        for (int i = 0; i < newest_twenty.size(); i++) {
          new_msg.set_msg(newest_twenty[i]);
          stream->Write(new_msg);
        }
        continue;
      }
      //Send the message to each follower's stream
      std::vector<Client*>::const_iterator it;
      for (it = c->client_followers.begin(); it != c->client_followers.end(); it++) {
        Client* temp_client = *it;
        if (temp_client->stream != 0 && temp_client->connected)
          temp_client->stream->Write(message);
        //For each of the current user's followers, put the message in their following.txt file
        std::string temp_username = temp_client->username;
        std::string temp_file = "./" + server_type + "_" + server_id + "/" + temp_username + "following.txt";
        std::ofstream following_file(temp_file, std::ios::app | std::ios::out | std::ios::in);
        following_file << fileinput;
        temp_client->following_file_size++;
        std::ofstream user_file("./" + server_type + "_" + server_id + "/" + temp_username + ".txt", std::ios::app | std::ios::out | std::ios::in);
        user_file << fileinput;
      }

      // Update slave
      if (server_type == "master") {
        ClientContext m_context;
        Reply m_reply;
        Update m_update;
        m_update.set_username(username);
        m_update.set_msg(fileinput);
        stub_->ServerUpdate(&m_context, m_update, &m_reply);
      }
    }
    //If the client disconnected from Chat Mode, set connected to false
    c->connected = false;
    return Status::OK;
  }

  // To update slave server
  Status ServerUpdate(ServerContext* context, const Update* request, Reply* reply) override {
    // Get user
    Client* c;
    std::string username = request->username();
    int user_index = find_user(username);
    c = &client_db[user_index];

    //Write the current message to "username.txt"
    std::string filename = "./" + server_type + "_" + server_id + "/" + username + ".txt";
    std::ofstream user_file(filename, std::ios::app | std::ios::out | std::ios::in);
    std::string fileinput = request->msg();
    //"Set Stream" is the default message from the client to initialize the stream
    user_file << fileinput;

    //Send the message to each follower's stream
    std::vector<Client*>::const_iterator it;
    for (it = c->client_followers.begin(); it != c->client_followers.end(); it++) {
      Client* temp_client = *it;
      //For each of the current user's followers, put the message in their following.txt file
      std::string temp_username = temp_client->username;
      std::string temp_file = "./" + server_type + "_" + server_id + "/" + temp_username + "following.txt";
      std::ofstream following_file(temp_file, std::ios::app | std::ios::out | std::ios::in);
      following_file << fileinput;
      temp_client->following_file_size++;
      std::ofstream user_file("./" + server_type + "_" + server_id + "/" + temp_username + ".txt", std::ios::app | std::ios::out | std::ios::in);
      user_file << fileinput;
    }

    return Status::OK;
  }
};


void RunServer(std::string port_no, std::string coord_ip, std::string coord_port) {

  std::string server_address = "0.0.0.0:" + port_no;
  SNSServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());

  // Dispatch thread to message coordinator with heartbeats
  std::thread server_heartbeat(heartbeat_handler, coord_ip, coord_port, port_no);
  server_heartbeat.detach();

  std::cout << "Server listening on " << server_address << std::endl;

  server->Wait();
}

int main(int argc, char** argv) {

  std::string coord_ip = "0.0.0.0";
  std::string coord_port = "6009";
  std::string server_port = "3010";
  server_id = "-1";
  server_type = "master";

  int opt = 0;
  while ((opt = getopt(argc, argv, "h:p:s:u:t:")) != -1) {
    switch (opt) {
    case 'h':
      coord_ip = optarg;
      break;
    case 'p':
      coord_port = optarg;
      break;
    case 's':
      server_port = optarg;
      break;
    case 'u':
      server_id = optarg;
      break;
    case 't':
      server_type = optarg;
      break;
    default:
      std::cerr << "Invalid Command Line Argument\n";
    }
  }

  if (server_type == "master") {
    // Get Slave port
    std::string login_info = coord_ip + ":" + coord_port;
    auto coord_stub = std::unique_ptr<Coordinator_Service::Stub>(Coordinator_Service::NewStub(
      grpc::CreateChannel(
        login_info, grpc::InsecureChannelCredentials())));

    Request request;
    request.set_username(server_id);

    Reply reply;
    ClientContext context;
    coord_stub->HandleSlave(&context, request, &reply);

    // Connect to slave
    login_info = coord_ip + ":" + reply.msg();
    stub_ = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(
      grpc::CreateChannel(
        login_info, grpc::InsecureChannelCredentials())));
  }
  std::string path = server_type + "_" + server_id;
  int check = mkdir(path.c_str(), 0777);

  RunServer(server_port, coord_ip, coord_port);

  return 0;
}
