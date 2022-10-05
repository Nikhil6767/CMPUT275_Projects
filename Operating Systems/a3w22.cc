/*
Nikhil Nayyar
nnayyar1 1614962
CMPUT 379 Assignment 2
*/
#include <iostream>
#include <cstring>
#include <string>
#include <fstream>
#include <vector>
#include <poll.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <algorithm>
#include <signal.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>

using namespace std;

#define MAXIP 1000
#define MAX_NSW 7

bool got_usr1 = false;
bool timer_started = false;

// switch information for the HELLO pkt
struct switch_info{
	int pkt_name;
	int port_1;
	int port_2;
	int IPlow;
	int IPhigh;
};

// switch information for the forwarding table
struct pkt_actn {
	int srcIP_lo;
	int srcIP_hi;
	int destIP_lo;
	int destIP_hi;
	string action;
	int actval;
	int pktCount;
};

// Messaging declarations addapted from CMPUT 379: Experiments with sending and receiving formatted messages
typedef enum { HELLO, HELLO_ACK, ASK, ADD, RELAY, DONE} KIND;
typedef struct { char  d[6][132]; } MSG_STR;
typedef struct { int   d[6]; }          MSG_INT;
typedef union { MSG_STR  mStr; MSG_INT mInt;} MSG;
typedef struct { KIND kind; MSG msg; } FRAME;
MSG composeMINT (int a, int b, int c, int d, int e);

void sendFrame (int fd, KIND kind, MSG *msg);

// general functions
int open_fifo_write(string fifo_name);
int open_fifo_read(string fifo_name);
char * string_to_charptr(string str);
int split(vector<string> &split_input, string input, char field_separator);

// functions for packet switch 
void print_info_psw(vector<pkt_actn> forward_table, vector<int> &pkt_status);
void handle_recv_psw(int pkt_num, int pkt_send_fd, vector<int> &pkt_status, FRAME frame, vector<pkt_actn> &forward_table, int port1, int port2,  struct pollfd *pollfds);
void do_psw(char *argv[]);

// functions for master switch
void print_info(vector<switch_info> info_table, int num_switches, vector<int> &pkt_status);
void handle_recv(int pkt_num, int pkt_send_fd, vector<switch_info> &info_table, vector<int> &pkt_status, FRAME frame);
void do_master(int num_switches, char *argv[]);

/* New Asn3 Functions adapted from sockMsg from eclass*/
typedef struct sockaddr  SA;
int serverListen (int portNo, int nClient);
int clientConnect (const char *serverName, int portNo);

// signal handler
void user1(int code){
	if (code == SIGUSR1) {
		got_usr1 = true;
	}
}
void catch_sigalrm(int code){
	if (code == SIGALRM) {
		cout << "** Delay period ended" << endl;
		timer_started = false;
	}
}

int main(int argc, char *argv[]){
	// check to envoke master switch or packet switch, or exit and tell user proper usage. Adapted from CMPUT 379 Experiments with
	// sending and receiving formatted messages 
	if (argc <= 1) {
		cout << "For master switch usage: "<< argv[0] << " master nSwitch portNumber" << endl;
		cout << "For packet switch usage: "<< argv[0] << " pswi dataFile (null|pswj) (null|pswk) IPlow-IPhigh serverAddress portNumber" << endl;
		exit(0);
	}
	
	if (strcmp(argv[1], "master") == 0 && argc == 4 && stoi(argv[2]) <= MAX_NSW) {
		int num_switches = stoi(argv[2]);
		do_master(num_switches, argv);
	}

	else if (strstr(argv[1], "psw") != NULL && argc == 8) {do_psw(argv);}

	else{
	cout << "For master switch usage: "<< argv[0] << " master nSwitch portNumber" << endl;
	cout << "For packet switch usage: "<< argv[0] << " pswi dataFile (null|pswj) (null|pswk) IPlow-IPhigh serverAddress portNumber" << endl;
	exit(0);
	}
	
	return 0;
}

//** MASTER SWITCH **//
void do_master(int num_switches, char *argv[]) {

	signal(SIGUSR1, user1);
	vector<switch_info> info_table;
	int fifos[num_switches+1][num_switches+1];
	vector<int> pkt_status {0, 0, 0, 0};

	// SET UP SOCKET CONNECTIONS
	int sfd, portNum = atoi(argv[3]);
	sfd = serverListen(portNum, num_switches);

	// set poll info on our socket file descriptors and STDIN
	struct pollfd pollfds[num_switches+1];
	
	pollfds[1].fd = sfd;
	pollfds[1].events = POLLIN;
	pollfds[1].revents = 0;

	pollfds[0].fd = STDIN_FILENO;
	pollfds[0].events = POLLIN;
	pollfds[0].revents = 0;

	struct sockaddr_in  from;
	socklen_t           fromlen;
	int done[num_switches + 1];
	

	bool loop = true;
	bool data_read = false;
	int num_desc = 2;
		
	// start master loop
	while (loop) {
		int rval = poll(pollfds, num_desc, 0); // num_switches + 1 to include stdin
		// check for failed poll call
		if (rval < 0) {cout << "Warning, polling error (possible from sigusr1)" << endl;}

		// accept connections
		if(  (pollfds[1].revents & POLLIN) ) {//(num_desc < num_switches + 1) &&
			// cout << "num desc is " << num_desc << endl;
			pollfds[num_desc].fd = accept(pollfds[1].fd, (SA *) &from, &fromlen);
			pollfds[num_desc].events = POLLIN;
			pollfds[num_desc].revents = 0;
			// cout << "accepting a socket with fd " << pollfds[num_desc].fd << endl;
			done[num_desc-1] = 0;
			num_desc++;
		}

		// poll for user input
		if (pollfds[0].revents & POLLIN)  {
			// if no packet was started master should not try to get info so we exit prgm
			if (!data_read){cout << "No packet switch has sent data" << endl; exit(0);}
			pollfds[0].revents = 0;
			char buf[4];
			int len;
			len = read(pollfds[0].fd, buf, 4);
			if (strcmp(buf, "exit") == 0) {print_info(info_table, num_switches, pkt_status); loop = false;}
			if (strcmp(buf, "info") == 0) {print_info(info_table, num_switches, pkt_status);}
		}

		// poll for pkt switch
		for (int i = 2; i <= num_switches+2; i++) {
			// cout << "i is " << i << endl;
			// cout << "fd is " << pollfds[i].fd << endl;
			// cout << "done is " << done[i] << endl;
			if ((done[i-1] == 0) && (pollfds[i].revents & POLLIN)) { //(done[i-1] == 0) && 
				pollfds[i].revents = 0;
				data_read = true;
				FRAME frame;
				int len;
				len = read(pollfds[i].fd, &frame, sizeof(frame));
				if (len==0){
					cout << "lost connection to psw" << i-1 << endl;
					done[i-1] = 1;
				
				}
				else{
					if (frame.kind == DONE) {done[i] = 1;}
					handle_recv(i-1, pollfds[i].fd, info_table, pkt_status, frame);
				}
				
			}
		}
		// check for sigusr1
		if (got_usr1){
			print_info(info_table, num_switches, pkt_status);
			got_usr1 = false;
		}
		
	} // end while

	for (int i = 1; i <= num_switches+1; i++){
		close(pollfds[i].fd);
	}
	
}

//** TOR SWITCH **//
void do_psw(char *argv[]) {
	string num(1, argv[1][3]);
	vector<int> pkt_status {0, 0, 0, 0, 0, 0, 0};
	vector<pkt_actn> forward_table;
	int n1_fd_send, n1_fd_recv, n2_fd_send, n2_fd_recv;

	char * hostname = argv[6];
	int portNum = atoi(argv[7]);

	// CONNECT SOCKETS
	// cout << "trying to connect..." << endl;
	int pkt_send_fd = clientConnect(hostname, portNum);
	// cout << "connected to fd " << pkt_send_fd  << endl;

	// set poll info
	struct pollfd pollfds[7]; // stdin, master to pkt, pkt to master, then add 2 for each neigbour
	// set poll info for stdin
	pollfds[0].fd = STDIN_FILENO;
	pollfds[0].events = POLLIN;
	
	// set poll info for master socket
	pollfds[1].fd = pkt_send_fd;
	pollfds[1].events = POLLIN;
	pollfds[1].revents = 0;

	//** get the switch information we need for HELLO **//
	int pkt_num = argv[1][3] - '0';

	// check for null neighbours
	int port1, port2;
	if (strcmp(argv[3], "null") == 0) {port1 = -1;}
	else {
		port1 = argv[3][3] - '0';
		// open fifos for neighbour
		string fifo_n1_send = "fifo-" + num +"-"; fifo_n1_send += argv[3][3];
		n1_fd_send = open_fifo_write(string_to_charptr(fifo_n1_send));
		if (n1_fd_send < 0) {cout << "The fifo " << fifo_n1_send << " has not been made" << endl; exit(0);}

		string fifo_n1_recv = "fifo-";
		fifo_n1_recv += argv[3][3]; fifo_n1_recv += "-"; fifo_n1_recv += num;
		n1_fd_recv = open_fifo_read(string_to_charptr(fifo_n1_recv));
		// set poll info for neighbour fifos
		pollfds[3].fd = n1_fd_send;
		pollfds[3].events = POLLOUT;
		pollfds[3].revents = 0;

		pollfds[4].fd = n1_fd_recv;
		pollfds[4].events = POLLIN;
		pollfds[4].revents = 0;

	}
	if (strcmp(argv[4], "null") == 0) {port2 = -1;}
	else {
		port2 = argv[4][3]-'0';
		// open fifos for neighbour
		string fifo_n2_send = "fifo-" + num + "-"; fifo_n2_send += argv[4][3];
		n2_fd_send = open_fifo_write(string_to_charptr(fifo_n2_send));

		string fifo_n2_recv = "fifo-"; fifo_n2_recv += argv[4][3]; fifo_n2_recv += "-" + num;
		n2_fd_recv = open_fifo_read(string_to_charptr(fifo_n2_send));

		// set poll info for neighbour fifos
		pollfds[5].fd = n2_fd_send;
		pollfds[5].events = POLLOUT;
		pollfds[5].revents = 0;

		pollfds[6].fd = n2_fd_recv;
		pollfds[6].events = POLLIN;
		pollfds[6].revents = 0;
		
	}
	// get the IPlow and IPhigh from command line
	vector<string> split_IP;
	split(split_IP, argv[5], '-');

	// compose the message
	MSG hello_pkt;
	memset( (char *) &hello_pkt, 0, sizeof(hello_pkt) );
	hello_pkt = composeMINT(pkt_num, port1, port2, stoi(split_IP[0]), stoi(split_IP[1]));

	//compose the frame
	FRAME frame;
	memset( (char *) &frame, 0, sizeof(frame) );
	frame.kind = HELLO;
	frame.msg = hello_pkt;

	// send Hello
	write(pkt_send_fd, &frame, sizeof(frame));
	cout << "Transmitted (src= psw" << pkt_num << " dest= master) [HELLO]:" << endl;
	cout << "(port0= master, port1= " << port1 << ", port2= " << port2 << ", port3= " << split_IP[0]
	<< "-" << split_IP[1] << ")" << endl;

	// store Hello
	pkt_status[4] += 1;

	// install the intial rule for the forwarding table
	struct pkt_actn fwd_table_info;
	fwd_table_info.srcIP_lo = 0;
	fwd_table_info.srcIP_hi = MAXIP;
	fwd_table_info.destIP_lo = stoi(split_IP[0]);
	fwd_table_info.destIP_hi = stoi(split_IP[1]);
	fwd_table_info.action = "FORWARD";
	fwd_table_info.actval = 3;
	fwd_table_info.pktCount = 0;

	forward_table.push_back(fwd_table_info);
	// open data file for reading
	string filename = argv[2];
	string line;
	ifstream data(filename);
	if (!data.is_open()) {
		cout << "File could not be opened" << endl;
		exit(0);
	}

	signal(SIGUSR1, user1);
	signal(SIGALRM, catch_sigalrm);
	bool cont_read = true;
	bool loop = true;
	bool match = false;
	bool wait = false;
	// start packet loop
	while (loop) {
		// read a line from the file
		if (cont_read && !timer_started && !wait) {
			if(getline(data, line)){
				vector<string> split_lines;
				split(split_lines, line, ' ');
				// check for packets that should be handled by this switch
				if (split_lines[0] == argv[1]){
					if (split_lines[1] == "delay"){
						cout << "** Entering a delay period of " << split_lines[2] << " msec" << endl;
						struct itimerval timer;
						timer.it_value.tv_sec = stoi(split_lines[2]) / 1000;
						timer.it_value.tv_usec = (stoi(split_lines[2]) % 1000) *1000;
						timer.it_interval.tv_sec = 0;
						timer.it_interval.tv_usec = 0;
						setitimer(ITIMER_REAL, &timer, NULL);
						timer_started = true;
					}
					else{
						// admit the packet
						pkt_status[0] += 1;
						match = false;

						for (int i = 0; i < forward_table.size(); i++){
							// check if the packet matches a rule in the forwarding table
							if (forward_table[i].srcIP_lo <= stoi(split_lines[2]) &&  stoi(split_lines[2]) <= forward_table[i].srcIP_hi && 
								forward_table[i].destIP_lo <= stoi(split_lines[2]) && stoi(split_lines[2]) <= forward_table[i].destIP_hi) {

							
									if (forward_table[i].actval == 1){
										cout <<"monk1 \n";
										MSG msg;
										sendFrame(pollfds[4].fd, RELAY, &msg);
										cout << "Transmitted (src= psw" << num << " dest= psw" << port1 << " [RELAYOUT]: header= (srcIP= " <<
										split_lines[1] << ", destIP= " << split_lines[2] << ")" << endl;
										//increase relay out
										pkt_status[6] += 1;
									}
									if (forward_table[i].actval == 2){
										cout <<"monk2 \n";
										MSG msg;
										sendFrame(pollfds[6].fd, RELAY, &msg);
										cout << "Transmitted (src= psw" << num << " dest= psw" << port2 << " [RELAYOUT]: header= (srcIP= " <<
										split_lines[1] << ", destIP= " << split_lines[2] << ")" << endl;
										//increase relay out
										pkt_status[6] += 1;
									}
								

								// increase the count for a matching rule
								int count = forward_table[i].pktCount;
								forward_table[i].pktCount = count + 1;
								match = true;
								break;
							}
							// ask master swtich what to do
						}
						if(!match) {
							cout << "Transmitted (src= psw" << pkt_num << ", dest= master) [ASK]: header= (srcIP= " << split_lines[1] 
							<< ", destIP= " << split_lines[2] << ")" << endl;
							// compose the message
							MSG ask;
							// send the src and dest ip in question
							ask = composeMINT(stoi(split_lines[1]), stoi(split_lines[2]), port1, port2, 0);
							//compose the frame
							sendFrame(pollfds[1].fd, ASK, &ask);
							// store the ask
							pkt_status[5] += 1;
							wait = true;
						}
						
					}
				}
			}
			// reached eof
			else { cont_read = false; }
		}

		int rval = poll(pollfds, MAX_NSW, -1);
		// check for failed poll call
		if (rval < 0) {cout << "Polling failed" << endl; exit(0);}

		// poll for user input
		if (pollfds[0].revents & POLLIN)  {
			pollfds[0].revents = 0;
			char buf[4];
			int len;
			len = read(pollfds[0].fd, buf, 4);
			// close the socket if the user exits a pkt switch
			if (strcmp(buf, "exit") == 0) {print_info_psw(forward_table, pkt_status); close(pkt_send_fd);
				close(n2_fd_recv); close(n2_fd_send); close(n1_fd_recv); close(n1_fd_send); loop = false;}
			if (strcmp(buf, "info") == 0) {print_info_psw(forward_table, pkt_status);}
		}
		// poll master socket
		if (pollfds[1].revents & POLLIN) {
			pollfds[1].revents = 0;
			FRAME frame;
			int len;
			len = read(pollfds[1].fd, &frame, sizeof(frame));
			// if we polled pollfds[i], i-1 contains the corresponding output fifo
			handle_recv_psw(pkt_num, pollfds[1].fd, pkt_status, frame, forward_table, port1, port2, pollfds);
			wait = false;
		}
		// poll from switches
		for (int i = 2; i <= MAX_NSW; i++) {
			// cout << "i is " << i << endl;
			// cout << "fd is " << pollfds[i].fd << endl;
			if (pollfds[i].revents & POLLIN) { 
				pollfds[i].revents = 0;
				FRAME frame;
				int len;
				len = read(pollfds[i].fd, &frame, sizeof(frame));
				// if we polled pollfds[i], i-1 contains the corresponding output fifo
				handle_recv_psw(pkt_num, i, pkt_status, frame, forward_table, port1, port2, pollfds);
			}
		}
		// check for sigusr1
		if (got_usr1){
			print_info_psw(forward_table, pkt_status);
			got_usr1 = false;
		}


	} // end while
}

void print_info(vector<switch_info> info_table, int num_switches, vector<int> &pkt_status) {
	cout << "Switch information:" << endl;
	for (int i = 0; i < num_switches; i++) {
		cout << "[" << "psw" + to_string(info_table[i].pkt_name) << "]" << " port1= " << info_table[i].port_1 << ", port2= " 
		<< info_table[i].port_2 << ", port3= " << info_table[i].IPlow << "-" << info_table[i].IPhigh << endl;
	}

	cout << "Packet Stats:" << endl;
	cout << "\t Recieved: HELLO:" << pkt_status[0] << ", ASK:" << pkt_status[1] << endl;
	cout << "\t Transmitted: HELLO_ACK:" << pkt_status[2] << ", ADD:" << pkt_status[3] << endl;
}

void handle_recv(int pkt_num, int pkt_send_fd, vector<switch_info> &info_table, vector<int> &pkt_status, FRAME frame) {
	switch (frame.kind) {
	case HELLO: {
		cout << "Recieved (src= psw" << pkt_num << ", dest= master) " "[HELLO]:" << endl;
		// store a hello
		pkt_status[0] += 1;
		// store switch info
		struct switch_info recv_info;
		recv_info.pkt_name = frame.msg.mInt.d[0];//"psw" + to_string(frame.msg.mInt.d[0]);
		recv_info.port_1 = frame.msg.mInt.d[1];
		recv_info.port_2 = frame.msg.mInt.d[2];
		recv_info.IPlow = frame.msg.mInt.d[3];
		recv_info.IPhigh = frame.msg.mInt.d[4];
		info_table.push_back(recv_info);

		cout << "(port0= master, port1= " << recv_info.port_1 << ", port2= " << recv_info.port_2 << ", port3= " 
		<< recv_info.IPlow << "-" << recv_info.IPhigh << ")" << endl;

		// send HELLO_ACK back to pkt switch
		FRAME ack;
		MSG msg;
		ack.kind = HELLO_ACK;
		ack.msg = msg;
		write(pkt_send_fd, &ack, sizeof(ack));
		cout << "Transmitted (src= master, dest= psw"<< pkt_num << ") [HELLO_ACK]:" << endl;
		// store a hello ack
		pkt_status[2] += 1; 
		break; }

	case ASK: {
		int srcIP, destIP, port1, port2;
		srcIP = frame.msg.mInt.d[0];
		destIP = frame.msg.mInt.d[1];
		port1 = frame.msg.mInt.d[2];
		port2 = frame.msg.mInt.d[3];

		cout << "Recieved (src= psw" << pkt_num << ", dest= master) [ASK]: header= (srcIP= " << srcIP <<
		", destIP= " << destIP << ")" << endl;

		bool sent = false;
		// loop over switches
		for (int i = 0; i < info_table.size(); i++) {
			if (info_table[i].IPlow <= destIP && destIP <= info_table[i].IPhigh) {
				// check if the found switch is a neigbour
				if (info_table[i].pkt_name == port1){
					MSG msg;
					// send message as 3 ints, first being 1 for forward, 2 for drop, second being the port to forward to
					// third for the destIPlo, fourth destiphi
					msg = composeMINT(1, port1, info_table[i].IPlow, info_table[i].IPhigh , 0);
					sendFrame(pkt_send_fd, ADD, &msg);
					sent = true;

					cout << "(srcIP= 0-1000" << ", destIP= " << destIP << ", action= FORWARD:1, pktCount= 0" << endl;
				}
				else if (info_table[i].pkt_name == port2){
					MSG msg;
					msg = composeMINT(1, port2, info_table[i].IPlow, info_table[i].IPhigh , 0);
					sendFrame(pkt_send_fd, ADD, &msg);
					sent = true;

					cout << "(srcIP= 0-1000" << ", destIP= " << destIP << ", action= FORWARD:2, pktCount= 0" << endl;
				}
			}
		}
		// drop to port 0
		if (!sent) {
			MSG msg;
			msg = composeMINT(2, 0, destIP, destIP, 0); 
			sendFrame(pkt_send_fd, ADD, &msg);

			cout << "(srcIP= 0-1000" << ", destIP= " << destIP << ", action= DROP:0, pktCount= 0" << endl;
		}
		// store a ask
		pkt_status[1] += 1; 

		// store a add
		pkt_status[3] += 1; 

		cout << "Transmitted (src= master, dest= psw"<< pkt_num << ") [ADD]:" << endl;
		break; }
	}
	
}

void print_info_psw(vector<pkt_actn> forward_table, vector<int> &pkt_status) {
	// cout << forward_table.size() << endl;
	cout << "Forwarding Table" << endl;
	for (int i = 0; i < forward_table.size(); i++) {
		cout << "[" << i << "] (srcIP= " << forward_table[i].srcIP_lo << "-" << forward_table[i].srcIP_hi 
		<< ", destIP= " << forward_table[i].destIP_lo << "-" << forward_table[i].destIP_hi << ", action= "
		<< forward_table[i].action << ":" << forward_table[i].actval << ", pktCount = " << forward_table[i].pktCount
		<< ")" << endl;
	}

	cout << "Packet Status" << endl;
	cout << "\t Recieved: ADMIT:" << pkt_status[0] << ", HELLO_ACK:" << pkt_status[1] << ", ADD:" << pkt_status[2] <<
	", RELAYIN:" << pkt_status[3] << endl;
	cout << "\t Transmitted: HELLO:" << pkt_status[4] << ", ASK:" << pkt_status[5] << ", RELAYOUT:" << pkt_status[6] << endl;

}

void handle_recv_psw(int pkt_num, int pkt_send_fd, vector<int> &pkt_status, FRAME frame, vector<pkt_actn> &forward_table, int port1, int port2, struct pollfd *pollfds) {
	switch (frame.kind) {
	case HELLO_ACK: {
		// store a hello ack
		cout << "Recieved (src= master, dest= psw"<< pkt_num << ") [HELLO_ACK]:" << endl;
		pkt_status[1] += 1;
		break; }
	case ADD: {
		cout << "Recieved (src= master, dest= psw"<< pkt_num << ") [ADD]:" << endl;
		// store an add
		pkt_status[2] += 1;
		// store the information
		struct pkt_actn new_entry;
		new_entry.srcIP_lo = 0;
		new_entry.srcIP_hi = MAXIP;
		new_entry.destIP_lo = frame.msg.mInt.d[2];
		new_entry.destIP_hi = frame.msg.mInt.d[3];
		// check if the packet is to be forwarded or dropped
		if (frame.msg.mInt.d[0] == 1){
			new_entry.action = "Forward";
		}
		if (frame.msg.mInt.d[0] == 2){
			new_entry.action = "Drop";
		}
		new_entry.actval = frame.msg.mInt.d[1];
		new_entry.pktCount = 1;

		bool pre_exist = false;
		for (int i = 0; i < forward_table.size(); i++){
			if (forward_table[i].destIP_lo == new_entry.destIP_lo && forward_table[i].destIP_hi == new_entry.destIP_hi){
				forward_table[i].pktCount += 1;
				pre_exist = true;
			}
		}
		if (!pre_exist){
			forward_table.push_back(new_entry);
		}
		// if we need to forward, send the packet to another switch
		if (new_entry.action == "Forward") {
			// write relay to  switch_to_relay
			if (new_entry.actval == port1){
				MSG msg;
				sendFrame(pollfds[4].fd, RELAY, &msg);
				cout << "Transmitted (src= psw" << pkt_num << " dest= psw" << port1 << " [RELAYOUT]: header= (srcIP= " <<
				new_entry.destIP_lo << ", destIP= " << new_entry.destIP_hi << ")" << endl;
			}
			if (new_entry.actval == port2){
				MSG msg;
				sendFrame(pollfds[6].fd, RELAY, &msg);
				cout << "Transmitted (src= psw" << pkt_num << " dest= psw" << port2 << " [RELAYOUT]: header= (srcIP= " <<
				new_entry.destIP_lo << ", destIP= " << new_entry.destIP_hi << ")" << endl;
			}
			//increase relay out
			pkt_status[6] += 1;
			
		}

		
		break; }
	case RELAY: {
		if (pkt_send_fd == 4){cout << "Recieved (src= psw" << port1 << ", dest= psw" << pkt_num << ") [RELAYIN]:" << endl;}
		else {cout << "Recieved (src= psw" << port2 << ", dest= psw" << pkt_num << ") [RELAYIN]:" << endl;}
		
		// update the forwarding table
		for (int i = 0; i < forward_table.size(); i++){
			if (forward_table[i].action == "FORWARD" && forward_table[i].actval == 3){
				forward_table[i].pktCount += 1;
			}
		}
		// increase relay in
		pkt_status[3] += 1;
		break; }
	}
}

int open_fifo_read(string fifo_name) {
	int fd = open(string_to_charptr(fifo_name), 2, O_RDONLY, O_NONBLOCK);
	if (fd < 0) {cout << "The fifo " << fifo_name << " has not been made" << endl; exit(0);}
	return fd;
}

int open_fifo_write(string fifo_name) {
	int fd = open(string_to_charptr(fifo_name), 2, O_WRONLY, O_NONBLOCK);
	if (fd < 0) {cout << "The fifo " << fifo_name << " has not been made" << endl; exit(0);}
	return fd;
}

MSG composeMINT (int a, int b, int c, int d, int e)
{
	MSG  msg;
	memset( (char *) &msg, 0, sizeof(msg) );
	msg.mInt.d[0]= a; msg.mInt.d[1]= b; msg.mInt.d[2]= c; msg.mInt.d[3]= d; msg.mInt.d[4]= e;
	return msg;
}    

void sendFrame (int fd, KIND kind, MSG *msg)
{
	FRAME  frame;
	memset( (char *) &frame, 0, sizeof(frame) );
	frame.kind= kind;
	frame.msg=  *msg;
	write (fd, (char *) &frame, sizeof(frame));
}

int split(vector<string> &split_input, string input, char field_separator){
	string column;
	int num_cols = 0;
	// loop over lines from file
	for (auto iter : input){
		if (iter != field_separator){
			column+= iter;
		}
		else {
			// add items between multiple spaces 
			if (column != ""){
				split_input.push_back(column);
				column = "";
				num_cols++;
			}
		}
	}
	split_input.push_back(column);

	return num_cols;
}

char * string_to_charptr(string str){
	string temp = str;
	char* result = new char[temp.length()+1];
	strcpy(result, temp.c_str());

	return result;
}

/* New Asn3 Functions */
int serverListen (int portNo, int nClient) {
    int                 sfd;
    struct sockaddr_in  sin;

    memset ((char *) &sin, 0, sizeof(sin));

    // create a managing socket
    //
    if ( (sfd= socket (AF_INET, SOCK_STREAM, 0)) < 0) {
        cout << "serverListen: failed to create a socket \n";
    	exit(1);
    }
    // bind the managing socket to a name
    //
    sin.sin_family= AF_INET;
    sin.sin_addr.s_addr= htonl(INADDR_ANY);
    sin.sin_port= htons(portNo);

    if (bind (sfd, (SA *) &sin, sizeof(sin)) < 0) {
        cout << "serverListen: bind failed \n";
    	exit(1);
    }
    // indicate how many connection requests can be queued

    listen (sfd, nClient);
    return sfd;
}

int clientConnect (const char *serverName, int portNo)
{
	int                 sfd;
	struct sockaddr_in  server;
	struct hostent      *hp;                    // host entity

	// lookup the specified host
	//
	hp= gethostbyname(serverName);
	if (hp == (struct hostent *) NULL) {
	    cout << "clientConnect: failed gethostbyname '%s'\n", serverName;
		// exit(1);
	}

    // put the host's address, and type into a socket structure
    //
    memset ((char *) &server, 0, sizeof server);
    memcpy ((char *) &server.sin_addr, hp->h_addr, hp->h_length);
    server.sin_family= AF_INET;
    server.sin_port= htons(portNo);

    // create a socket, and initiate a connection
    if ( (sfd= socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        cout << "clientConnect: failed to create a socket \n";
    	exit(1);
	}

    if (connect(sfd, (SA *) &server, sizeof(server)) < 0) {
		cout << "clientConnect: failed to connect" << endl;
		exit(1);
	}
    return sfd;
}