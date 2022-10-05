// Nikhil Nayyar nnayyar1
// CMPUT 379 Asn 4

/* 
// Declarations
*/
#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <pthread.h>
#include <time.h>
using namespace std;

#define NRES_TYPES 10
#define NJOBS 25

struct resource_info {
	string name;
	int value;
};

struct job_info {
	string jobName;
	int busyTime;
	int idleTime;
	int num_res;
	vector<resource_info> job_resources;
};

vector<resource_info> all_resources;
vector<job_info> all_jobs;

int split(vector<string> &split_input, string input, char field_separator);
void read_file(char *argv[], vector<resource_info> &all_resources, vector<job_info> &all_jobs);
extern "C" void *athread(void *arg);
/* 
// Main
*/
int main(int argc, char *argv[]) {
	// check for valid cmd line arguments
	if (argc != 4) {
		cout << "Usage: " << argv[0] << " inputFile monitorTime NITER" << endl;
		exit(1);
	}

	// read in the file
	
	read_file(argv, all_resources, all_jobs);

	/* debug printing 
	for (int i = 0; i < all_resources.size(); i++){cout << all_resources[i].name << " " << all_resources[i].value << endl;}

	for (int i = 0; i < all_jobs.size(); i++){cout << all_jobs[i].jobName << " " << all_jobs[i].busyTime << " " << 
		all_jobs[i].job_resources[0].name << " " << all_jobs[i].num_res << endl;}
	*/

	pthread_t ntid;
	pthread_t tids[NJOBS+1];
	int rval;
	
	for (int i = 1; i <= 1; i++){
		
		rval = pthread_create(&ntid, NULL, athread, (void *) i);
	}
	
	rval = pthread_join(ntid, NULL);
	return 0;
}


/* 
// Functions
*/
void *athread(void *arg) {
	int  threadNum =  (intptr_t) arg; 

	cout << "im a thread " << threadNum << endl;
	cout << all_resources[0].name << endl;

	pthread_exit(NULL);
}

void read_file(char *argv[], vector<resource_info> &all_resources, vector<job_info> &all_jobs){
	// open data file for reading
	string filename = argv[1];
	string line;
	ifstream data(filename);

	// check for file error
	if (!data.is_open()) {
		cout << "File could not be opened" << endl;
		exit(1);
	}

	// read the file
	while (getline(data, line)) {
		// split the line at white space(s)
		vector<string> split_lines;
		int num_cols = split(split_lines, line, ' ');
		if (split_lines[0] == "resources") {	
			for (int i = 1; i <= num_cols; i++) {
				// split the name and value
				vector<string> split_resource;
				split(split_resource, split_lines[i], ':');

				// add the resource to our vector containing all resources
				struct resource_info resource;
				resource.name = split_resource[0];
				resource.value = stoi(split_resource[1]);
				all_resources.push_back(resource);
			}
		}
		else if (split_lines[0] == "job") {
			// get job information
			struct job_info job;
			job.jobName = split_lines[1];
			job.busyTime = stoi(split_lines[2]);
			job.idleTime = stoi(split_lines[3]);
			job.num_res = num_cols - 3;
			vector<resource_info> job_resources;

			// get the resources the job uses
			for (int i = 4; i <= num_cols; i++){
				// split the name and value
				vector<string> split_resource;
				split(split_resource, split_lines[i], ':');

				// add the resource to our vector containing all resources
				struct resource_info resource;
				resource.name = split_resource[0];
				resource.value = stoi(split_resource[1]);
				job_resources.push_back(resource);
			}

			job.job_resources = job_resources;
			all_jobs.push_back(job);
		}
	}
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