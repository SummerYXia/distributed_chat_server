#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include <string.h>
#include <string>
#include <arpa/inet.h>
using namespace std;

// global
const int BUFF_SIZE = 1000;

int main(int argc, char *argv[])
{
	if (argc != 2) {
		cerr << "Wudao Ling (wudao) @UPenn\n";
		exit(1);
	}

	// create a new socket(UDP)
	int sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock < 0){
		cerr << "cannot open socket\r\n";
		exit(2);
	}
    // set port for reuse
	const int REUSE = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &REUSE, sizeof(REUSE));

	// parse command and connect to server
	struct sockaddr_in dest;
	bzero(&dest, sizeof(dest));
	dest.sin_family = AF_INET;

	char* token = strtok(argv[1],":"); // now token is IP
	inet_pton(AF_INET, token , &dest.sin_addr);
	token = strtok(NULL,":"); // now token is port
	dest.sin_port = htons(atoi(token));

	// source address
	struct sockaddr_in src;
	socklen_t src_size = sizeof(src);

	while (true){
		// use select for I/O waiting, reference: https://www.gnu.org/software/libc/manual/html_node/Waiting-for-I_002fO.html
		fd_set read_set;

		FD_ZERO(&read_set);
		FD_SET(STDIN_FILENO, &read_set); // keyboard
		FD_SET(sock, &read_set); // socket

		// block until I/O is ready
		int readable = select(sock+1, &read_set, NULL, NULL, NULL);

		char buff[BUFF_SIZE];
		int read_len;

		if (FD_ISSET(STDIN_FILENO, &read_set)) {
			// keyboard input
			read_len = read(STDIN_FILENO, buff, BUFF_SIZE);
			buff[read_len-1] = 0; // remove \n at the end
			sendto(sock, buff, strlen(buff), 0, (struct sockaddr*) &dest, sizeof(dest));

			char* token = strtok(buff, " ");
			if (strcasecmp(token, "/quit") == 0) {
				break;
			}
		} else {
			// response from server
			read_len = recvfrom(sock, buff, sizeof(buff), 0, (struct sockaddr*) &src, &src_size);
			cout << buff << endl;
		}
		// clear buffer by setting zeros
		memset(buff, 0, BUFF_SIZE);
	}

	// close socket
	close(sock);
	return 0;
}  
