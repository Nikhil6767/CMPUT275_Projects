/*
Nikhil Nayyar
CMPUT 275 Assignment P2
1614962
*/
#include <iostream>
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>
#include <string.h>
#include <list>
#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "wdigraph.h"
#include "dijkstra.h"
#include <netdb.h>

#define MAX_SIZE 1024
#define LISTEN_BACKLOG 50

struct Point {
    long long lat, lon;
};

// returns the manhattan distance between two points
long long manhattan(const Point& pt1, const Point& pt2) {
  long long dLat = pt1.lat - pt2.lat, dLon = pt1.lon - pt2.lon;
  return abs(dLat) + abs(dLon);
}

// finds the point that is closest to a given point, pt
int findClosest(const Point& pt, const unordered_map<int, Point>& points) {
  pair<int, Point> best = *points.begin();

  for (const auto& check : points) {
    if (manhattan(pt, check.second) < manhattan(pt, best.second)) {
      best = check;
    }
  }
  return best.first;
}

// reads graph description from the input file and builts a graph instance
void readGraph(const string& filename, WDigraph& g, unordered_map<int, Point>& points) {
  ifstream fin(filename);
  string line;

  while (getline(fin, line)) {
    // split the string around the commas, there will be 4 substrings either way
    string p[4];
    int at = 0;
    for (auto c : line) {
      if (c == ',') {
        // starting a new string
        ++at;
      }
      else {
        // appending a character to the string we are building
        p[at] += c;
      }
    }

    if (at != 3) {
      // empty line
      break;
    }

    if (p[0] == "V") {
      // adding a new vertex
      int id = stoi(p[1]);
      assert(id == stoll(p[1])); // sanity check: asserts if some id is not 32-bit
      points[id].lat = static_cast<long long>(stod(p[2])*100000);
      points[id].lon = static_cast<long long>(stod(p[3])*100000);
      g.addVertex(id);
    }
    else {
      // adding a new directed edge
      int u = stoi(p[1]), v = stoi(p[2]);
      g.addEdge(u, v, manhattan(points[u], points[v]));
    }
  }
}

// Keep in mind that in Part I, your program must handle 1 request
// but in Part 2 you must serve the next request as soon as you are
// done handling the previous one
int main(int argc, char* argv[]) {
	WDigraph graph;
	unordered_map<int, Point> points;
	// build the graph
	readGraph("edmonton-roads-2.0.1.txt", graph, points);
	// In Part 2, client and server communicate using a pair of sockets
	// modify the part below so that the route request is read from a socket
	// (instead of stdin) and the route information is written to a socket
	// extract the server's IPv4 address and port number from argument vector
	int port_num = atoi(argv[1]); // the server port number
	struct sockaddr_in my_addr, peer_addr;
	// zero out the structor variable because it has an unused part
	memset(&my_addr, '\0', sizeof my_addr);
	// declare variables for socket descriptors 
	int lstn_socket_desc, conn_socket_desc;
	// declare buffers to read sockets
	char echobuffer1[MAX_SIZE] = {};
	char echobuffer2[MAX_SIZE] = {};

	lstn_socket_desc = socket(AF_INET, SOCK_STREAM, 0);
	if (lstn_socket_desc == -1) {
		std::cerr << "Listening socket creation failed!\n";
		return 1;
	}
	// prepare sockaddr_in structure variable
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(port_num);			
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(lstn_socket_desc, (struct sockaddr *) &my_addr, sizeof my_addr) == -1) {
		std::cerr << "Binding failed!\n";
		close(lstn_socket_desc);
		return 1;
	}
	// std::cout << "Binding was successful\n";

	if (listen(lstn_socket_desc, LISTEN_BACKLOG) == -1) {
		std::cerr << "Cannot listen to the specified socket!\n";
		close(lstn_socket_desc);
		return 1;
	}
	socklen_t peer_addr_size = sizeof my_addr;
	bool connection = true;
	while (connection) {
		// extract the first connection request from the queue of pending connection requests
		// return a new connection socket descriptor which is not in the listening state
		conn_socket_desc = accept(lstn_socket_desc, (struct sockaddr *) &peer_addr, &peer_addr_size);
		if (conn_socket_desc == -1){
			std::cerr << "Connection socket creation failed!\n";
			// continue;
			return 1;
		}
		// cout << "Connection request accepted from " << inet_ntoa(peer_addr.sin_addr) << ":" << ntohs(peer_addr.sin_port) << "\n";
		while (true) {
			// blocking call - blocks until a message arrives 
			// (unless O_NONBLOCK is set on the socket's file descriptor)
			int rec_size, rec_size2, rec_ack;; 
			rec_size = recv(conn_socket_desc, echobuffer1, MAX_SIZE, 0);
			// if Q sent from pipe to client to server then close server
			if (strcmp("Q", echobuffer1) == 0) {
				// std::cout << "Connection will be closed\n";
				connection = false;
			break;
			}
			// get the second point
			rec_size2 = recv(conn_socket_desc, echobuffer2, MAX_SIZE, 0);
			// cout << echobuffer1 << " " << echobuffer2 << endl;
			// convert starting point to a string to separate lat and lon
			string buffer1(echobuffer1);
			string str_start_lat = buffer1.substr(0, buffer1.find(' '));
			string str_start_lon = buffer1.substr(buffer1.find(' ')+1);
			// convert points for shortest path computation
			long long start_lat = static_cast<long long>(stod(str_start_lat)*100000);
			long long start_lon = static_cast<long long>(stod(str_start_lon)*100000);
			// convert end point to a string to get lat and lon
			string buffer2(echobuffer2);
			string str_end_lat = buffer2.substr(0, buffer2.find(' '));
			string str_end_lon = buffer2.substr(buffer2.find(' ')+1);
			// convert points for shortest path computation
			long long end_lat = static_cast<long long>(stod(str_end_lat)*100000);
			long long end_lon = static_cast<long long>(stod(str_end_lon)*100000);

			if (rec_size == -1) {
				std::cout << "Timeout occurred... still waiting!\n";
				continue;
			}
			// std::cout << "Message received\n";
			// Create the starting and end point structs using data from sockets
			Point sPoint, ePoint;
			sPoint.lat = start_lat;
			sPoint.lon = start_lon;
			ePoint.lat = end_lat;
			ePoint.lon = end_lon;
			// get the points closest to the two points we read
			int start = findClosest(sPoint, points), end = findClosest(ePoint, points);

			unordered_map<int, PIL> tree;
			dijkstra(graph, start, tree);
			// if no path is found send N 0 to the client
			if (tree.find(end) == tree.end()) {
				string no_path = "N 0";
				// send N 0 to client
				send(conn_socket_desc, no_path.c_str(), no_path.length() + 1, 0);
				// send the E message to client
				string end_message = "E\n";
				send(conn_socket_desc, end_message.c_str(), end_message.length()+1, 0);
				// cout << "ser seding " << end_message << endl;
			}
			else {
			// read off the path by stepping back through the search tree
				list<int> path;
				while (end != start) {
					path.push_front(end);
					end = tree[end].first;
				}
				path.push_front(start);
				// send the number of waypoints to the client
				int path_size = path.size();
				string str_path_size = to_string(path_size);
				string num_waypoints = "N " + str_path_size;
				send(conn_socket_desc, num_waypoints.c_str(), num_waypoints.length() + 1, 0);
				// blocking call as we wait for acknowledgement from the client
				rec_ack = recv(conn_socket_desc, echobuffer1, MAX_SIZE, 0);
				int count = 0;
				for (int v : path) {
					// dont send the start and end points to client, but send all the waypoints on
					// the path
					if (count > 0 && count < path.size()-1) {  
						// Divide lat and lon by 100 000 and convert to string so it can be sent back to client
						// and then written to plotter
						double temp_lat = points[v].lat, temp_lon = points[v].lon;
						temp_lat = temp_lat / 100000;
						temp_lon = temp_lon / 100000;

						string str_lat = to_string(temp_lat);
						string str_lon = to_string(temp_lon);
						string waypoint_msg = str_lat + " " + str_lon + "\n";

						send(conn_socket_desc, waypoint_msg.c_str(), waypoint_msg.length()+1, 0);
						// cout << "ser seding " << waypoint_msg << endl;
						// blocking call as we wait for acknowledgement from the client
						rec_ack = recv(conn_socket_desc, echobuffer1, MAX_SIZE, 0);
						// cout << "ser recving " << echobuffer1 << endl;
					}
					count++;
				}
				// send E message after sending all waypoints
				string end_message = "E\n";
				send(conn_socket_desc, end_message.c_str(), end_message.length()+1, 0);
				// cout << "ser seding " << end_message << endl;
			}
		}
	}

	// close socket descriptors
	close(lstn_socket_desc);
	close(conn_socket_desc);

	return 0;
}
