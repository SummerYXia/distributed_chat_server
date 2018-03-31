#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string.h>
#include <string>
#include <arpa/inet.h>
#include <vector>
#include <time.h>
#include <map>
using namespace std;

// constant
// ordering
const int UNORDERED = 0;
const int FIFO = 1;
const int TOTAL = 2;
// message type for total ordering
const int INITIAL = 0;
const int PROPOSE = 1;
const int DELIVER = 2;
// size
const int BUFF_SIZE = 1000;
const int GROUP_SIZE = 10;
const int NAME_SIZE = 50;

// class
struct Client {
	sockaddr_in addr;
	int group;
	string nickname;

	Client(sockaddr_in addr_){
		addr = addr_;
		group = 0;
	}
};

struct Message {
	int seq;
	int node;
	bool deliver;

	Message(int _seq, int _node){
		seq = _seq;
		node = _node;
		deliver = false;
	}
};
struct Compare {
	bool operator()(const Message& m1, const Message& m2) const {
		// message seq, node id as tie breaker
		if (m1.seq < m2.seq || (m1.seq == m2.seq && m1.node < m2.node)) {
			return true;
		} else {
			return false;
		}
	}
};

// global
bool DEBUG = false;
int ORDER = UNORDERED;
int SELF = 0;
vector<sockaddr_in> SERVERS;
vector<Client> CLIENTS;
// fifo
vector<int> LOCAL_SEQ; // local sequence number for group g (for this server)
vector<vector <int> > RECV_SEQ; // sequence number of the most recent message for group g received from N
vector<vector< map <int, string> > > FIFO_HOLDBACK; // holdback queue that contains messages for group g received from N
// total
vector<int> PROPOSED; // highest proposed sequence number for group g
vector<int> AGREED; // highest agreed sequence number for group g
vector< map<Message, string, Compare> >  TOTAL_HOLDBACK; // holdback queue that contains messages received for group g
map< string, vector< Message > > PROPOSALS;


// declare function
sockaddr_in parse_addr(char* address);
sockaddr_in read_config(string config_file, int index);
void init_global();
int chat_server(sockaddr_in self);
bool is_client(sockaddr_in src, int* index);
bool is_server(sockaddr_in src, int* index);
void handle_client(int client_index, char* buff, int sock);
char* prepare_debug();
char* prepare_msg(char* buff, Client* client);
void send_to_clients(int sock, char* message, int group);
void forward_to_servers(int sock, char* message, bool self);
void handle_fifo(int sock, char* message, int group, int sender, int seq);
void handle_total(int sock, char* message, int group, int sender, int seq, int node, int type);


int main(int argc, char *argv[])
{
	if (argc < 2) {
		cerr << "Wudao Ling (wudao) @UPenn\n";
		exit(1);
	}

	int c;
	// getopt() for command parsing
	while((c=getopt(argc,argv,"o:v"))!=-1){
		switch(c){
		case 'o': //order
			if (strcasecmp(optarg, "unordered") == 0){
				ORDER = UNORDERED;
			} else if (strcasecmp(optarg, "fifo") == 0) {
				ORDER = FIFO;
			} else if (strcasecmp(optarg, "total") == 0) {
				ORDER = TOTAL;
			} else {
				cerr << "Ordering supported are unordered, fifo and total\n";
				exit(1);
			}
			break;
		case 'v': //debug mode
			DEBUG = true;
			break;
		default:
			cerr <<"Syntax: "<< argv[0] << " [-o order] [-v] <config file> <index>\n";
			exit(1);
		}
	}

	// get config file
	if (optind == argc) {
		cerr <<"Syntax: "<< argv[0] << " [-o order] [-v] <config file> <index>\n";
		exit(1);
	}
	string config_file = argv[optind];

	// get index
	optind++;
	if (optind == argc) {
		cerr <<"Syntax: "<< argv[0] << " [-o order] [-v] <config file> <index>\n";
		exit(1);
	}
	SELF = atoi(argv[optind]);

	// read config file
	sockaddr_in self = read_config(config_file, SELF);

	// init global variable for ordering
	init_global();

	chat_server(self);
}  

sockaddr_in parse_addr(char* address){
	// init a sock address
	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;

	// IP
	char* token = strtok(address, ":");
	inet_pton(AF_INET, token, &addr.sin_addr);

    // port
	token = strtok(NULL, ":");
	addr.sin_port = htons(atoi(token));

	return addr;
}

sockaddr_in read_config(string config_file, int index){
	// open the file for reading
	ifstream config(config_file);

	// self address for binding
	sockaddr_in self;

	int i = 0;
	string line;
	while (getline(config, line)){
		char addresses[line.length()+1];
		strcpy(addresses, line.c_str());

		// a line may contains 1 or 2 address
		// if not using proxy, forward and bind must be the same
		char* forward = strtok(addresses, ",");
		char* bind = strtok(NULL, ",");

		SERVERS.push_back(parse_addr(forward));
		if (i==index-1){
			if (bind==NULL){
				self = parse_addr(forward);
			} else {
				self = parse_addr(bind);
			}
		}
		i++;
	}
	return self;
}

void init_global(){
	if (ORDER==UNORDERED) {
		return;
	} else if (ORDER==FIFO) {
		for (int i = 0; i < GROUP_SIZE; i++) {
			LOCAL_SEQ.push_back(0);
			vector <int> v2;
			RECV_SEQ.push_back(v2);
			vector< map< int, string > > v1;
			FIFO_HOLDBACK.push_back(v1);

			for (int j = 0; j < SERVERS.size(); j++) {
				RECV_SEQ[i].push_back(0);
				map< int, string > m;
				FIFO_HOLDBACK[i].push_back(m);
			}
		}
	} else if (ORDER==TOTAL) {
		for (int i = 0; i < GROUP_SIZE; i++){
			PROPOSED.push_back(0);
			AGREED.push_back(0);
			map<Message, string, Compare> m;
			TOTAL_HOLDBACK.push_back(m);
		}
	}
}

int chat_server(sockaddr_in self){
	// create a new socket(UDP) and bind
	int sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock < 0){
		cerr << "cannot open socket\r\n";
		exit(2);
	}
    // set port for reuse
	const int REUSE = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &REUSE, sizeof(REUSE));

	self.sin_addr.s_addr = htons(INADDR_ANY);
	bind(sock, (struct sockaddr*) &self, sizeof(self));

	// source address
	struct sockaddr_in src;
	socklen_t src_size = sizeof(src);

	while(true){
		char buff[BUFF_SIZE];
		int read_len = recvfrom(sock, buff, sizeof(buff), 0, (struct sockaddr*) &src, &src_size);
		buff[read_len] = 0; // clear buff

		int index = 0; // either index of servers or clients
		if (is_server(src, &index)){ // forwarded from other servers
			if (DEBUG) {
				char* prefix = prepare_debug();
				cout << prefix << " Server " << index << " forward \"" << buff << "\"" << endl;
				delete[] prefix;
			}

			int type = atoi(strtok(buff, "|"));
			int node = atoi(strtok(NULL, "|"));
			int seq = atoi(strtok(NULL, "|"));
			int group = atoi(strtok(NULL, "|"));
			char* message = strtok(NULL, "|");

			if (ORDER==UNORDERED){
				send_to_clients(sock, message, group);
			} else if (ORDER==FIFO){
				handle_fifo(sock, message, group, index, seq);
			} else if (ORDER==TOTAL){
				handle_total(sock, message, group, index, seq, node, type);
			}


		} else {
			// check whether the client is known or new
			if (!is_client(src, &index)){ // new client
				Client new_client(src);
				CLIENTS.push_back(new_client);
				index = CLIENTS.size();
			}

			if (DEBUG) {
				char* prefix = prepare_debug();
				cout << prefix << " Client " << index << " posts \"" << buff << "\""<< endl;
				delete[] prefix;
			}
			handle_client(index, buff, sock);
		}
	}
	return 0;


}

bool is_client(sockaddr_in src, int* index){
	for (int i = 0; i<CLIENTS.size(); i++){
		// compare both IP and port
		if (strcmp(inet_ntoa(CLIENTS[i].addr.sin_addr),inet_ntoa(src.sin_addr)) == 0  && CLIENTS[i].addr.sin_port == src.sin_port) {
			*index = i+1;
			return true;
		}
	}
	return false;
}

bool is_server(sockaddr_in src, int* index){
	for (int i = 0; i<SERVERS.size(); i++){
		// compare both IP and port
		if (strcmp(inet_ntoa(SERVERS[i].sin_addr),inet_ntoa(src.sin_addr)) == 0  && SERVERS[i].sin_port == src.sin_port) {
			*index = i+1;
			return true;
		}
	}
	return false;
}

void handle_client(int client_index, char* buff, int sock){
	Client* client = &CLIENTS[client_index-1];

	// handle commands
	if (buff[0]=='/'){
		char* token = strtok(buff, " ");
		char response[BUFF_SIZE];

		if (strcasecmp(token, "/join")==0) {
			if (client->group==0) { // not in any group
				token = strtok(NULL, " ");
				int group = atoi(token);

				if (group <= GROUP_SIZE) {
					client->group = group;
					sprintf(response, "+OK You are now in chat room #%d", group);
					sendto(sock, response, strlen(response), 0, (struct sockaddr*) &client->addr, sizeof(client->addr));
				} else {
					sprintf(response, "-ERR There are only %d chat room you could join", GROUP_SIZE);
					sendto(sock, response, strlen(response), 0, (struct sockaddr*) &client->addr, sizeof(client->addr));
				}
			} else { // already in a group
				sprintf(response, "-ERR You are already in room #%d", client->group);
				sendto(sock, response, strlen(response), 0, (struct sockaddr*) &client->addr, sizeof(client->addr));
			}

		} else if (strcasecmp(token, "/nick")==0){
			token = strtok(NULL, " ");
			char nickname[NAME_SIZE];
			strcpy(nickname, token);

	        client->nickname = nickname;
	        sprintf(response, "+OK Nickname set to \"%s\"", nickname);
	        sendto(sock, response, strlen(response), 0, (struct sockaddr*) &client->addr, sizeof(client->addr));

		} else if (strcasecmp(token, "/part")==0){
			if (client->group==0){
				sprintf(response, "-ERR You didn't join any chat room yet");
				sendto(sock, response, strlen(response), 0, (struct sockaddr*) &client->addr, sizeof(client->addr));
			} else {
				sprintf(response, "+OK You have left chat room #%d", client->group);
				client->group = 0;
				sendto(sock, response, strlen(response), 0, (struct sockaddr*) &client->addr, sizeof(client->addr));
			}

		} else if (strcasecmp(token, "/quit")==0){
			CLIENTS.erase(CLIENTS.begin() + client_index-1);

		} else {
			sprintf(response, "-ERR Unknown command for client");
			sendto(sock, response, strlen(response), 0, (struct sockaddr*) &client->addr, sizeof(client->addr));
		}

	// handle messages
	} else {
		if (client->group==0){ // not in any group, can't send to clients or forward to servers
			char response[BUFF_SIZE];
			sprintf(response, "-ERR You didn't join any chat room yet");
			sendto(sock, response, strlen(response), 0, (struct sockaddr*) &client->addr, sizeof(client->addr));
		} else {
			char forward_message[BUFF_SIZE];
			char* message = prepare_msg(buff, client);
			if (ORDER == UNORDERED) {
				// delivers local message as soon as receive
				send_to_clients(sock, message, client->group);
				// forward to other servers
				sprintf(forward_message, "%d|%d|%d|%d|%s", 0, SELF, 0, client->group, message);
				forward_to_servers(sock, forward_message, false);
			} else if (ORDER == FIFO){
				// delivers local message as soon as receive
				send_to_clients(sock, message, client->group);

				// prepare forward message
				LOCAL_SEQ[client->group-1]++;
				sprintf(forward_message, "%d|%d|%d|%d|%s", 0, SELF, LOCAL_SEQ[client->group-1], client->group, message);
				forward_to_servers(sock, forward_message, false);
			}
			else if (ORDER == TOTAL){
				sprintf(forward_message, "%d|%d|%d|%d|%s", INITIAL, SELF, 0, client->group, message);
				forward_to_servers(sock, forward_message, true);
			}
			delete[] message;
		}
	}
}

char* prepare_debug(){
	char *prefix = new char[NAME_SIZE];

	// time
    time_t rawtime;
    struct tm* timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    strftime (prefix, NAME_SIZE, "%X", timeinfo);

	// server index
	char server[4];
	sprintf(server, "S%02d", SELF);

	strcat(prefix, " ");
	strcat(prefix, server);
	return prefix;
}

char* prepare_msg(char* buff, Client* client){
	char *message = new char[BUFF_SIZE];
	if (client->nickname.empty()){
		// use IP and port as nickname
		char nickname[NAME_SIZE];
		sprintf(nickname, "%s:%d", inet_ntoa(client->addr.sin_addr), client->addr.sin_port);
		sprintf(message, "<%s> %s", nickname, buff);
	} else {
		sprintf(message, "<%s> %s", client->nickname.c_str(), buff);
	}
	return message;
}

void send_to_clients(int sock, char* message, int group){
	for (int i = 0; i < CLIENTS.size(); i++){
		if (CLIENTS[i].group == group){
			sendto(sock, message, strlen(message), 0, (struct sockaddr*) &CLIENTS[i].addr, sizeof(CLIENTS[i].addr));
			if (DEBUG){
				char* prefix = prepare_debug();
				cout << prefix << " Server sends \"" << message << "\" to client "<< i+1 << " at char room #" << CLIENTS[i].group << endl;
				delete[] prefix;
			}
		}
	}
}

void forward_to_servers(int sock, char* message, bool self){
	for (int i = 0; i < SERVERS.size(); i++){
		if (!self && i==SELF-1) continue; // if not include self
		sendto(sock, message, strlen(message), 0, (struct sockaddr*) &SERVERS[i], sizeof(SERVERS[i]));
	}
}

void handle_fifo(int sock, char* message, int group, int sender, int seq){
	// for index
	int gp = group-1;
	int sd = sender-1;

	// put into holdback queue
	FIFO_HOLDBACK[gp][sd][seq] = message;

    // continue deliver next message if sequence is right
	int next_seq = RECV_SEQ[gp][sd] + 1;
	while(FIFO_HOLDBACK[gp][sd].find(next_seq) != FIFO_HOLDBACK[gp][sd].end()){
		const char* next_msg = FIFO_HOLDBACK[gp][sd][next_seq].c_str();
		char msg[BUFF_SIZE];
		strcpy(msg, next_msg);
    	send_to_clients(sock, msg, group);

    	// delete message in queue and update received sequence
    	FIFO_HOLDBACK[gp][sd].erase(next_seq);
    	next_seq = (++RECV_SEQ[gp][sd])+1;
    }
}

void handle_total(int sock, char* message, int group, int sender, int seq, int node, int type){
	char msg[BUFF_SIZE];
	string content(message);
	int gp = group-1; // for index

	if (type==INITIAL){
		// update PROPOSE and build new message
		PROPOSED[gp] = max(PROPOSED[gp], AGREED[gp])+1;
		Message m(PROPOSED[gp], SELF);

		// save in local holdback queue
		TOTAL_HOLDBACK[gp][m] = content;

		// respond to sender
		sprintf(msg, "%d|%d|%d|%d|%s", PROPOSE, SELF, PROPOSED[gp], group, message);
		sendto(sock, msg, strlen(msg), 0, (struct sockaddr*) &SERVERS[sender-1], sizeof(SERVERS[sender-1]));

	} else if (type==PROPOSE){ // only the sender
		// save propose
		if (PROPOSALS.find(content)==PROPOSALS.end()){
			vector<Message> v;
			PROPOSALS[content] = v;
		}
		Message m(seq, node);
		PROPOSALS[content].push_back(m);

		// if receive all(including self), choose max as seq
		if (PROPOSALS[content].size()==SERVERS.size()){
			int max_seq = 0;
			int max_node = 0;
			for (int i = 0; i<PROPOSALS[content].size(); i++){
				if (PROPOSALS[content][i].seq > max_seq || (PROPOSALS[content][i].seq == max_seq && PROPOSALS[content][i].node > max_node)){
					max_seq = PROPOSALS[content][i].seq;
					max_node = PROPOSALS[content][i].node;
				}
			}

			// multi-cast
			sprintf(msg, "%d|%d|%d|%d|%s", DELIVER, max_node, max_seq, group, message);
			forward_to_servers(sock, msg, true);

			// erase proposals about this message
		    PROPOSALS.erase(content);
		}

	} else if (type==DELIVER){
		// update Message in holdback queue
		Message m(seq, node);
		m.deliver = true;
		for (auto it: TOTAL_HOLDBACK[gp]){
			if (it.second.compare(content)==0){
				TOTAL_HOLDBACK[gp].erase(it.first);
				TOTAL_HOLDBACK[gp][m] = content;
			}
		}

		// update agree
		AGREED[gp] = max(AGREED[gp], seq);

		// deliver according to holdback queue's sequence
		while(TOTAL_HOLDBACK[gp].begin()->first.deliver){
			strcpy(msg, TOTAL_HOLDBACK[gp].begin()->second.c_str());
			send_to_clients(sock, msg, group);
			TOTAL_HOLDBACK[gp].erase(TOTAL_HOLDBACK[gp].begin());
		}
	}
}

