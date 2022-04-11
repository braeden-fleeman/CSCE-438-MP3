#include <vector>


struct Server_Entry {
    int server_ID = -1;
    int port = -1;
    bool isActive = false;
};

std::vector<Server_Entry> master_table;
std::vector<Server_Entry> slave_table;
std::vector<Server_Entry> synchronizer_table;


// TODO: Main function
// TODO: Anything else here
int getServer(int client_id) {
    int serverID = (client_id % 3) + 1;
    if(master_table.at(serverID).isActive) {
        return master_table.at(serverID).port;
    }

    return slave_table.at(serverID).port;
}
int getFollowerSyncer(int client_id) {
    int serverID = (client_id % 3) + 1;
    return synchronizer_table.at(serverID).port;
}