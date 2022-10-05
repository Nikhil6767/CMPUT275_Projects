// Nikhil Nayyar
// CMPUT 379 Assignment 1
// 1614962

#include <sys/times.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <iostream>
#include <unistd.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include <signal.h>

#define MAXLINE     132
#define MAX_NTOKEN  MAXLINE
#define MAXWORD     32

using namespace std;

// struct that contains info for a task
struct program_info {
	int index;
	pid_t pid;
	string cmdline;
	string state;
};

char * string_to_charptr(string str);
void do_cdir(vector<string> &split_input);
void do_run(vector<string> &split_input, string input, vector<program_info> &tasks, int &index, int num_tok);
void do_check(vector<string> &split_input, FILE* fpin);
int split(vector<string> &split_input, string input, char field_separator);
int gsub (char *t, char *omitSet, char *repStr);

int main() {
	// limit the CPU time to 10 minutes
	struct rlimit time_lim;

	time_lim.rlim_cur = 600; //600 seconds = 10 mins
	time_lim.rlim_max = 600;

	setrlimit(RLIMIT_CPU, &time_lim);

	// start timing
	struct tms init_time; 
	clock_t start;
	start = times(&init_time);

	// task parameters
	int index = 0;
	vector<program_info> tasks;

	pid_t pid;
	pid = getpid();

	// main loop
	bool loop = true;
	while (loop) {
		cout << "msh379 " << pid << ": ";

		// get command from user
		string input;
		getline(cin, input);

		size_t found = input.find(" ");
		// handle cmdlines with only 1 argument
		if (found == string::npos){
			//** handle pdir **//
			if (input == "pdir") {
				char s[256];
				// print the current directory
				cout << getcwd(s, 256) << endl;
			}

			//** handle lstasks **// 
			else if (input == "lstasks"){
				// print all accepted tasks that have not been terminated
				for (int i = 0; i < tasks.size(); i++){
					if (tasks[i].state != "Z"){
						cout << tasks[i].index << ": (pid= " << tasks[i].pid << ", cmd= " << tasks[i].cmdline << ")"  << endl;
					}
				}
			}

			//** handle exit **//
			else if (input == "exit"){
				for (int i = 0; i < tasks.size(); i++){
					if (tasks[i].state != "Z"){
						kill(tasks[i].pid, SIGKILL);
						cout << "task " << tasks[i].pid << " terminated" << endl;
					}
				}
				loop = false;
			}

			//** handle quit **//
			else if (input == "quit") {loop = false;}
		}
		// handle cmdlines with multiple arguments
		else{
			// split the input
			vector<string> split_input;
			char cmd_separator = ' ';
			int num_tok = split(split_input, input, cmd_separator);
			
			//** handle cdir **//
			if (split_input[0] == "cdir") {
				do_cdir(split_input);
			}

			//** handle run **//
			else if (split_input[0] == "run"){
				if (index == 31){
					cerr << "Max tasks reached" << endl;
				}
				else{do_run(split_input, input, tasks, index, num_tok);}
			}

			//** handle stop **//
			else if (split_input[0] == "stop"){
				int task_no = stoi(split_input[1]);
				for (int i = 0; i < tasks.size(); i++){
					if (tasks[i].index == task_no){
						kill(tasks[i].pid, SIGSTOP);
						tasks[i].state = "T";
					}
				}
			}

			//** handle continue **//
			else if (split_input[0] == "continue"){
				int task_no = stoi(split_input[1]);
				for (int i = 0; i < tasks.size(); i++){
					if (tasks[i].index == task_no){
						kill(tasks[i].pid, SIGCONT);
						tasks[i].state = "S";
					}
				}
			}

			//** handle terminate **//
			else if (split_input[0] == "terminate"){
				int task_no = stoi(split_input[1]);
				for (int i = 0; i < tasks.size(); i++){
					if (tasks[i].index == task_no){
						kill(tasks[i].pid, SIGKILL);
						tasks[i].state = "Z";
					}
				}
			}

			//** handle check **//
			else if (split_input[0] == "check"){
				FILE *fpin = popen("ps -u $USER -o user,pid,ppid,state,start,cmd --sort start", "r");
				do_check(split_input, fpin);
				int pstatus = pclose(fpin);
			}
		}
	} // end while

	// end timing
	struct tms finish_time; 
	clock_t end;
	end = times(&finish_time);
	long clktck = sysconf(_SC_CLK_TCK);

	// display times
	cout << "real: " << (end-start) / clktck << " sec." << endl;
	cout << "user: " << (finish_time.tms_utime - init_time.tms_utime) / clktck << " sec." << endl;
	cout << "sys: " << (finish_time.tms_stime - init_time.tms_stime) / clktck << " sec." << endl;
	cout << "child user: " << (finish_time.tms_cutime - init_time.tms_cutime) / clktck << " sec." << endl;
	cout << "child sys: " << (finish_time.tms_cstime - init_time.tms_cstime) / clktck << " sec." << endl;

	return 0;
}

char * string_to_charptr(string str){
	string temp = str;
	char* result = new char[temp.length()+1];
	strcpy(result, temp.c_str());

	return result;
}

void do_cdir(vector<string> &split_input){
	// split the path
	vector<string> split_path;
	char path_separator = '/';
	int num_paths = split(split_path, split_input[1], path_separator);
	size_t found = split_path[0].find("$");
	// if environment variable in path
	if (found != string::npos){
		// convert env variable to char * (ie $HOME)
		char * sub_path = string_to_charptr(split_path[0]);

		char a[]= "$";
		char b[] = "";
		// remove $
		int num_subs = gsub(sub_path, a, b);

		// // get environment var value
		char *pPath;
		pPath = getenv(sub_path);
		string env_path = string(pPath);
		delete[] sub_path;

		// create the path the user wants us to change to by adding back in /
		string add_path = "/";
		for (int i = 1; i < num_paths; i++){
			env_path += add_path + split_path[i];
		}

		const char* path = env_path.c_str();

		if (chdir(path) == 0){
			cout << "cdir: done (pathname= " << path << ")" << endl;
		}
		else {cout << "cdir: failed (pathname= " << path << ")" << endl;}
	}

	// no environment variable in path
	else {
		const char* path = split_input[1].c_str();

		if (chdir(path) == 0){
			cout << "cdir: done (pathname= " << path << ")" << endl;
		}
		else {cout << "cdir: failed (pathname= " << path << ")" << endl;}
	}
}

void do_run(vector<string> &split_input, string input, vector<program_info> &tasks, int &index, int num_tok){
	int status;
	pid_t pid_task;
	pid_task = fork();

	char *pgrm;
	char *arg1, *arg2, *arg3, *arg4;

	if (pid_task < 0) {cout << "fork failed" << endl;}
	if (pid_task == 0) {
		switch (num_tok) {
			case 2: // run a program with 0 arguments
				pgrm = string_to_charptr(split_input[1]);
				status = execlp(pgrm, pgrm, (char *) NULL);
				if (status < 0){
					exit(1); // stop child process if excelp failed
				}
				delete[] pgrm;
				break;
			case 3: // run a program with 1 argument
				pgrm = string_to_charptr(split_input[1]);
				arg1 = string_to_charptr(split_input[2]);
				status = execlp(pgrm, pgrm, arg1, (char *) NULL);
				if (status < 0){
					exit(0); // stop child process if excelp failed
				}
				delete[] pgrm;
				delete[] arg1;
				break;
			case 4: // run a program with 2 arguments
				pgrm = string_to_charptr(split_input[1]);
				arg1 = string_to_charptr(split_input[2]);
				arg2 = string_to_charptr(split_input[3]);
				status = execlp(pgrm, pgrm, arg1, arg2, (char *) NULL);
				if (status < 0){
					exit(0); // stop child process if excelp failed
				}
				delete[] pgrm;
				delete[] arg1;
				delete[] arg2;
				break;
			case 5: // run a program with 3 arguments
				pgrm = string_to_charptr(split_input[1]);
				arg1 = string_to_charptr(split_input[2]);
				arg2 = string_to_charptr(split_input[3]);
				arg3 = string_to_charptr(split_input[4]);
				status = execlp(pgrm, pgrm, arg1, arg2, (char *) NULL);
				if (status < 0){
					exit(0); // stop child process if excelp failed
				}
				delete[] pgrm;
				delete[] arg1;
				delete[] arg2;
				delete[] arg3;
				break;
			case 6: // run a program with 4 arguments
				pgrm = string_to_charptr(split_input[1]);
				arg1 = string_to_charptr(split_input[2]);
				arg2 = string_to_charptr(split_input[3]);
				arg3 = string_to_charptr(split_input[4]);
				arg4 = string_to_charptr(split_input[5]);
				status = execlp(pgrm, pgrm, arg1, arg2, arg3, arg4, (char *) NULL);
				if (status < 0){
					exit(0); // stop child process if excelp failed
				}
				delete[] pgrm;
				delete[] arg1;
				delete[] arg2;
				delete[] arg3;
				delete[] arg4;
				break;
		}
	}

	else if (pid_task > 0){
		// store index and cmdline used
		program_info new_task;
		new_task.index = index;
		for (int i = 1; i < num_tok; i++){
			if (i == num_tok-1) {new_task.cmdline += split_input[i];}
			else {new_task.cmdline += split_input[i] + " ";}
		}
		// get the pid of the task and store it
		new_task.pid = pid_task;
		sleep(1);
		/*
		use waitpid and in the event of a failed excelp,
		dont add task to vector since the task failed
		*/
		pid_t child_status = waitpid(pid_task, &status, WNOHANG);

		if (WIFEXITED(status)){ // check if the excelp ran sucessfuly, if not we dont accept the task
			cout << "task creation failed with exit status " << WEXITSTATUS(status) << endl;
		}
		else {
			tasks.push_back(new_task);
			index++; // increase index by one if we have accepted a task
		}
	}

}

void do_check(vector<string> &split_input, FILE*fpin){
	char buffer[256];
	string line;
	vector<vector<string>> all_pgrms;
	// read the lines
	// acknowledge c++ reference https://www.cplusplus.com/reference/cstdio/FILE/
	while (! feof (fpin)){
		if (fgets (buffer, 256, fpin) == NULL) break;
		line = string(buffer);
		
		// split the ps line into tokens
		vector<string> ps;
		string column;
		int num_cols = 0;
		for (auto iter : line){
			if (num_cols == 5){
				column+=iter;
			}
			else{
				if (iter != ' '){
					column+= iter;
				}
				else { 
					if (column != ""){
						ps.push_back(column);
						column = "";
						num_cols++;
					}
				}
			}
		}
		ps.push_back(column);
		all_pgrms.push_back(ps);
	}// end while

	// display the ps headers
	for (int k = 0; k < all_pgrms[0].size(); k++){
		cout << all_pgrms[0][k] << " ";
	}
	cout << endl;
	string target_pid = split_input[1];

	for (int i = 0; i < all_pgrms.size(); i++){
		// check if the target_pid is a process
		if (all_pgrms[i][1] == target_pid){ 
			// check and report if the process is terminated
			if (all_pgrms[i][3] == "Z") { 
				cout << "target_pid= " << target_pid << " terminated" << endl; 
				}
			else{
				// display information on the process
				for (int j = 0; j < all_pgrms[i].size(); j++){
					cout << all_pgrms[i][j] << " ";
				}
			}
		}
	}
	for (int m = 0; m < all_pgrms.size(); m++){
		// check for descendants of target_pid process
		if (all_pgrms[m][2] == target_pid){
			for (int n = 0; n < all_pgrms[m].size(); n++){
				cout << all_pgrms[m][n] << " ";
			}
		}
	}
	cout << endl;
}
int split(vector<string> &split_input, string input, char field_separator) {
	string token;
	int num_tok = 1;
	// loop over the string
	for (auto iter : input) {
		// split at specified field separator
		if (iter == field_separator){
			split_input.push_back(token);
			token = "";
			num_tok++;
		}
		else { token += iter; }
	}
	// add the last token
	split_input.push_back(token);

	return num_tok;
}
	
int gsub (char *t, char *omitSet, char *repStr)
{
    int   i,j, ocSeen, match, nmatch;
    char  outStr[MAXLINE];

    enum {MBEGIN= 1, MALL= 2} task;
    task= (omitSet[0] == '^')? MBEGIN : MALL;
    memset (outStr, 0, sizeof(outStr));

    nmatch= 0; ocSeen= 0;	       // Ordinary character not seen yet
    for (i= 0; i < strlen(t); i++)
    {
	if ( (task == MBEGIN) && (ocSeen == 1)) {
	    outStr[strlen(outStr)]= t[i]; continue;
	}	    

	match= 0;		// no match found yet for character t[i]
    	for (j= 0; j < strlen(omitSet); j++)
	{
	    if (t[i] == omitSet[j])
	       { match= 1; nmatch++; strcat(outStr, repStr); break; }
	}
	if (match == 0) { ocSeen= 1; outStr[strlen(outStr)]= t[i]; }
    }
    memset (t, 0, sizeof(t));
    strcpy (t, outStr);
    return(nmatch);
}