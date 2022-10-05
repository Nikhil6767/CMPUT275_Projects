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
typedef enum { HELLO, HELLO_ACK, ASK, ADD, RELAY} KIND;
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
void handle_recv_psw(int pkt_send_fd, vector<int> &pkt_status, FRAME frame, vector<pkt_actn> &forward_table, int port1, int port2,  struct pollfd *pollfds);

void do_psw(char *argv[]);

// functions for master switch
void print_info(vector<switch_info> info_table, int num_switches, vector<int> &pkt_status);
void handle_recv(int pkt_send_fd, vector<switch_info> &info_table, vector<int> &pkt_status, FRAME frame);

void do_master(int num_swithces);

// signal handler
void user1(int code){
	if (code == SIGUSR1) {
		got_usr1 = true;
	}
}
void catch_sigalrm(int code){
	if (code == SIGALRM) {
		cout << "**Delay period ended" << endl;
		timer_started = false;
	}
}
int main(int argc, char *argv[]){
	// check to envoke master switch or packet switch, or exit and tell user proper usage. Adapted from CMPUT 379 Experiments with
	// sending and receiving formatted messages 
	if (strcmp(argv[1], "master") == 0 && argc == 3 && stoi(argv[2]) <= MAX_NSW) {
		int num_switches = stoi(argv[2]);
		do_master(num_switches);
	}

	else if (strstr(argv[1], "psw") != NULL && argc == 6) {do_psw(argv);}

	else{
	cout << "For master switch usage: "<< argv[0] << " master nSwitch" << endl;
	cout << "For packet switch usage: "<< argv[0] << " pswi dataFile (null|pswj) (null|pswk) IPlow-IPhigh" << endl;
	exit(0);
	}
	
	return 0;
}

//** MASTER SWITCH **//
void do_master(int num_switches) {

	signal(SIGUSR1, user1);
	vector<switch_info> info_table;
	int fifos[num_switches+1][num_switches+1];
	vector<int> pkt_status {0, 0, 0, 0};

	// store the file descriptors for fifos from pkt switches to master
	for (int i = 1; i < num_switches + 1; i++) {
		string fifo_recv = "fifo-";
		fifo_recv += to_string(i) + "-0";
		fifos[i][0] = open_fifo_read(string_to_charptr(fifo_recv));

	}

	// store the file descriptors for fifos from master to pkt switches
	for (int i = 1; i < num_switches + 1; i++) {
		string fifo_send = "fifo-0-";
		fifo_send += to_string(i);
		fifos[0][i] = open_fifo_write(string_to_charptr(fifo_send));
	}

	// set poll info on our fifo file descriptors
	struct pollfd pollfds[num_switches*2+1];
	for (int i = 1; i < num_switches*2+1; i++) {
		// these are fifos recieving data from pkt switches
		if (i <= num_switches) {
			pollfds[i].fd = fifos[i][0];
			pollfds[i].events = POLLIN;
			pollfds[i].revents = 0;
		}
		// these are fifos sending data to pkt switches
		else {
			pollfds[i].fd = fifos[0][i-num_switches];
			pollfds[i].events = POLLOUT;
			pollfds[i].revents = 0;
		}
	}

	// set poll info for stdin
	pollfds[0].fd = STDIN_FILENO;
	pollfds[0].events = POLLIN;

	bool loop = true;
	bool data_read = false;
	// start master loop
	while (loop) {
		int rval = poll(pollfds, num_switches+1, -1); // num_switches + 1 to include stdin
		// check for failed poll call
		if (rval < 0) {cout << "Warning, polling error (possible from sigusr1)" << endl;}

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
		for (int i = 1; i < num_switches+1; i++) {
			// cout << "i is " << i << endl;
			// cout << "fd is " << pollfds[i].fd << endl;
			if (pollfds[i].revents & POLLIN) { 
				pollfds[i].revents = 0;
				data_read = true;
				FRAME frame;
				int len;
				len = read(pollfds[i].fd, &frame, sizeof(frame));
				handle_recv(pollfds[num_switches+i].fd, info_table, pkt_status, frame);
			}
		}
		// check for sigusr1
		if (got_usr1){
			print_info(info_table, num_switches, pkt_status);
			got_usr1 = false;
		}
		
	} // end while
}

//** TOR SWITCH **//
void do_psw(char *argv[]) {
	string num(1, argv[1][3]);
	vector<int> pkt_status {0, 0, 0, 0, 0, 0, 0};
	vector<pkt_actn> forward_table;

	// open the fifo this pkt switch is going to use to send to master
	string fifo_send = "fifo-"; fifo_send += argv[1][3]; fifo_send += "-0";
	int pkt_send_fd = open_fifo_write(string_to_charptr(fifo_send));

	// open fifo from master to pkt switch
	string fifo_recv = "fifo-0-" + num;
	int pkt_recv_fd = open_fifo_read(string_to_charptr(fifo_recv));

	// set poll info
	struct pollfd pollfds[7]; // stdin, master to pkt, pkt to master, then add 2 for each neigbour
	// set poll info for stdin
	pollfds[0].fd = STDIN_FILENO;
	pollfds[0].events = POLLIN;
	// poll info for pkt to master
	pollfds[1].fd = pkt_send_fd;
	pollfds[1].events = POLLOUT;
	pollfds[1].revents = 0;
	// poll info for master to pkt
	pollfds[2].fd = pkt_recv_fd;
	pollfds[2].events = POLLIN;
	pollfds[2].revents = 0;

	//** get the switch information we need for HELLO **//
	int pkt_num = argv[1][3] - '0';

	// check for null neighbours
	int port1, port2;
	if (strcmp(argv[3], "null") == 0) {port1 = -1;}
	else {
		port1 = argv[3][3] - '0';
		// open fifos for neighbour
		string fifo_n1_send = "fifo-" + num +"-"; fifo_n1_send += argv[3][3];
		int n1_fd_send = open_fifo_write(string_to_charptr(fifo_n1_send));
		if (n1_fd_send < 0) {cout << "The fifo " << fifo_n1_send << " has not been made" << endl; exit(0);}

		string fifo_n1_recv = "fifo-";
		fifo_n1_recv += argv[3][3]; fifo_n1_recv += "-"; fifo_n1_recv += num;
		int n1_fd_recv = open_fifo_read(string_to_charptr(fifo_n1_recv));
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
		int n2_fd_send = open_fifo_write(string_to_charptr(fifo_n2_send));

		string fifo_n2_recv = "fifo-"; fifo_n2_recv += argv[4][3]; fifo_n2_recv += "-" + num;
		int n2_fd_recv = open_fifo_read(string_to_charptr(fifo_n2_send));

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
	frame.kind= HELLO;
	frame.msg= hello_pkt;

	// send Hello
	write(pkt_send_fd, &frame, sizeof(frame));
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
	// start packet loop
	while (loop) {
		// read a line from the file
		if (cont_read && !timer_started) {
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

						for (int i = 0; i < forward_table.size(); i++){
							// check if the packet matches a rule in the forwarding table
							if (forward_table[i].srcIP_lo <= stoi(split_lines[1]) &&  stoi(split_lines[1]) <= forward_table[i].srcIP_hi && 
								forward_table[i].destIP_lo <= stoi(split_lines[2]) && stoi(split_lines[2]) <= forward_table[i].destIP_hi) {
								// increase the count for a matching rule
								int count = forward_table[i].pktCount;
								forward_table[i].pktCount = count + 1;
							}
							// ask master swtich what to do
							else {
								// compose the message
								MSG ask;
								// send the src and dest ip in question
								ask = composeMINT(stoi(split_lines[1]), stoi(split_lines[2]), port1, port2, 0);
								//compose the frame
								sendFrame(pollfds[1].fd, ASK, &ask);
								// store the ask
								pkt_status[5] += 1;					
							}
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
			if (strcmp(buf, "exit") == 0) {print_info_psw(forward_table, pkt_status); loop = false;}
			if (strcmp(buf, "info") == 0) {print_info_psw(forward_table, pkt_status);}
		}
		// poll from switches
		for (int i = 1; i < MAX_NSW; i++) {
			// cout << "i is " << i << endl;
			// cout << "fd is " << pollfds[i].fd << endl;
			if (pollfds[i].revents & POLLIN) { 
				pollfds[i].revents = 0;
				FRAME frame;
				int len;
				len = read(pollfds[i].fd, &frame, sizeof(frame));
				// if we polled pollfds[i], i-1 contains the corresponding output fifo
				handle_recv_psw(pollfds[i-1].fd, pkt_status, frame, forward_table, port1, port2, pollfds);
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

void handle_recv(int pkt_send_fd, vector<switch_info> &info_table, vector<int> &pkt_status, FRAME frame) {
	switch (frame.kind) {
	case HELLO: {
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

		// send HELLO_ACK back to pkt switch
		FRAME ack;
		MSG msg;
		ack.kind = HELLO_ACK;
		ack.msg = msg;
		write(pkt_send_fd, &ack, sizeof(ack));
		// store a hello ack
		pkt_status[2] += 1; 
		break; }

	case ASK: {
		int srcIP, destIP, port1, port2;
		srcIP = frame.msg.mInt.d[0];
		destIP = frame.msg.mInt.d[1];
		port1 = frame.msg.mInt.d[2];
		port2 = frame.msg.mInt.d[3];
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
				}
				else if (info_table[i].pkt_name == port2){
					MSG msg;
					msg = composeMINT(1, port2, info_table[i].IPlow, info_table[i].IPhigh , 0);
					sendFrame(pkt_send_fd, ADD, &msg);
					sent = true;
				}
			}
		}
		// drop to port 0
		if (!sent) {
			MSG msg;
			msg = composeMINT(2, 0, destIP, destIP, 0); 
			sendFrame(pkt_send_fd, ADD, &msg);
		}
		// store a ask
		pkt_status[1] += 1; 

		// store a add
		pkt_status[3] += 1; 
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

void handle_recv_psw(int pkt_send_fd, vector<int> &pkt_status, FRAME frame, vector<pkt_actn> &forward_table, int port1, int port2, struct pollfd *pollfds) {
	switch (frame.kind) {
	case HELLO_ACK: {
		// store a hello ack
		pkt_status[1] += 1;
		break; }
	case ADD: {
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
				sendFrame(pollfds[3].fd, RELAY, &msg);
			}
			if (new_entry.actval == port2){
				MSG msg;
				sendFrame(pollfds[5].fd, RELAY, &msg);
			}
			//increase relay out
			pkt_status[6] += 1;
		}

		
		break; }
	case RELAY: {
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

		