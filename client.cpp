/*
Nikhil Nayyar
CMPUT 275 Assignment P2
1614962
*/
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <cstring>  
#define MAX_SIZE 1024
// Add more libraries, macros, functions, and global variables if needed

using namespace std;

int create_and_open_fifo(const char * pname, int mode) {
    // creating a fifo special file in the current working directory
    // with read-write permissions for communication with the plotter
    // both proecsses must open the fifo before they can perform
    // read and write operations on it
    if (mkfifo(pname, 0666) == -1) {
        cout << "Unable to make a fifo. Ensure that this pipe does not exist already!" << endl;
        exit(-1);
    }

    // opening the fifo for read-only or write-only access
    // a file descriptor that refers to the open file description is
    // returned
    int fd = open(pname, mode);

    if (fd == -1) {
        cout << "Error: failed on opening named pipe." << endl;
        exit(-1);
    }
    return fd;
}

int main(int argc, char const *argv[]) {
    const char *inpipe = "inpipe";
    const char *outpipe = "outpipe";

    int in = create_and_open_fifo(inpipe, O_RDONLY);
    cout << "inpipe opened..." << endl;
    int out = create_and_open_fifo(outpipe, O_WRONLY);
    cout << "outpipe opened..." << endl;
    // Your code starts here
    // Here is what you need to do:
    // 1. Establish a connection with the server
    int port_num = atoi(argv[1]); // get the port number   
    const char * server_ip = argv[2]; // get server ip address

    struct sockaddr_in my_addr, peer_addr;
    // zero out the structor variable because it has an unused part
    memset(&my_addr, '\0', sizeof my_addr);
    // Declare socket descriptor
    int socket_desc;

    char outbound[MAX_SIZE] = {};
    char inbound[MAX_SIZE] = {};
    char end_msg[MAX_SIZE] = {};
    // the acknowledgement message from client
    char acknow[] = {'A'};
    int bytes_written;

    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1) {
        std::cerr << "Listening socket creation failed!\n";
        return 1;
    }
    // Prepare sockaddr_in structure variable
    peer_addr.sin_family = AF_INET;                       
    peer_addr.sin_port = htons(port_num);      
    inet_aton(server_ip, &(peer_addr.sin_addr));   

    if (connect(socket_desc, (struct sockaddr *) &peer_addr, sizeof peer_addr) == -1) {
        std::cerr << "Cannot connect to the host!\n";
        close(socket_desc);
        return 1;
    }
    // cout << "Connection established with " << inet_ntoa(peer_addr.sin_addr) << ":" << ntohs(peer_addr.sin_port) << "\n";

    char buffer1[MAX_SIZE];
    char buffer2[MAX_SIZE];
    int bytesread, bytesread2, rec_size;
    while (true) {
        // 2. Read coordinates of start and end points from inpipe (blocks until they are selected)
        // int bytesread = read(in, buffer, MAX_SIZE);
        // If 'Q' is read instead of the coordinates then go to Step 7
        
        // read the points from plotter
        bytesread = read(in, buffer1, MAX_SIZE);
        // remove the newline from first point
        size_t len = strlen(buffer1);
        char * b1 = (char *)malloc(len-1);
        memcpy(b1, buffer1, len-1);
        // if plotter is closed Q is sent to pipe and we end connection
        // cout << "this is buf1 " << b1 << endl;
        if (strcmp("Q", b1) == 0) {
            // cout << "terminate client? " << endl;
            send(socket_desc, b1, strlen(b1) + 1, 0);
            break;
        }
        
        // otherwise continue reading next point
        else {
            // 3. Write to the first point to socket
            send(socket_desc, b1, strlen(b1) + 1, 0);
            bytesread2 = read(in, buffer2, MAX_SIZE);
        }

        // remove newline from second point
        size_t len2 = strlen(buffer2);
        char * b2 = (char *)malloc(len2-4);
        memcpy(b2, buffer2, len2-4);
        // 3. Write to the second point to socket
        send(socket_desc, b2, strlen(b2) + 1, 0);
        // cout << b1 << " " << b2 << endl;
        // 4. Read coordinates of waypoints one at a time (blocks until server writes them)
    
        // get the total num of waypoints (blocking call)
        rec_size = recv(socket_desc, inbound, MAX_SIZE, 0);
        // convert num waypoints to an int
        string str_inbound(inbound);
        string str_num_wayp = str_inbound.substr(str_inbound.find(' ')+1);
        int num_wayp = stoi(str_num_wayp);
        // cout << "a " << num_wayp << endl;
        if (num_wayp > 0) {
            // send the acknowledgement to server
            send(socket_desc, acknow, strlen(acknow)+1, 0);
            // 5. Write these coordinates to outpipe
            for (int i=0; i<(num_wayp-2); i++) {
                // blocking call to recieve a waypoint
                rec_size = recv(socket_desc, inbound, MAX_SIZE, 0);
                // cout << "cli rec " << inbound << endl;
                // write the point to outpipe
                bytes_written = write(out, inbound, rec_size-1);
                // send the acknowledgement to server
                send(socket_desc, acknow, strlen(acknow)+1, 0);
                // cout << "cli sending " << acknow << endl;
            }
            // Write the E at the end
            rec_size = recv(socket_desc, end_msg, MAX_SIZE, 0);
            // cout << "cli rec " << end_msg << endl;
            bytes_written = write(out, end_msg, rec_size-1);
        }
        else{
            // Write the E at the end (N 0 Case)
            rec_size = recv(socket_desc, end_msg, MAX_SIZE, 0);
            // cout << "n 0 cli rec " << end_msg << endl;
            bytes_written = write(out, end_msg, rec_size-1);
        }
        // 6. Go to Step 2
        // clear buffer 1 and 2 and go back to step 2 at begining of while loop
        memset(buffer1, 0, sizeof buffer1);
        memset(buffer2, 0, sizeof buffer2);
    }

    // 7. Close the socket and pipes
    close(socket_desc);
    // Your code ends here
    close(in);
    close(out);
    // commented out unlinking from code because make clean handles this
    // unlink(inpipe);
    // unlink(outpipe);
    return 0;
}
