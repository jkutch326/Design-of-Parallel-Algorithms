#include "game.h"
#include "utilities.h"
// Standard Includes for MPI, C and OS calls
#include <mpi.h>

// C++ standard I/O and library includes
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>

// C++ standard library using statements
using std::cerr;
using std::cout;
using std::endl;
using std::ifstream;
using std::ios;
using std::ofstream;
using std::string;
using std::vector;

const int CHUNK_SIZE = 12;

struct game_results {
	game_state game_board; 		// initial game board that converts char buffer to game's grid
	move solution[IDIM*JDIM]; 	// move history that yields a solution to the puzzle
	bool found = false; 		// solution found
	int size = 0;				// number of steps used in solution
	int solved_by;				// processor that worked on the puzzle
};

void search(unsigned char buf[], game_results results[], int batch_size, int rank) {
	for (int i = 0; i < batch_size; ++i) {
		results[i].game_board.Init(&buf[i*IDIM*JDIM]);
		//results[i].start_board.Init(buf[i]);
		// Search for a solution to the puzzle
		results[i].found = depthFirstSearch(results[i].game_board,
											results[i].size,
											results[i].solution);
		results[i].solved_by = rank;
	}
}
class Server {
	public:
	vector<string> puzzles; // will essentially act as the bag in the bag of tasks
	int NUM_GAMES;
	ifstream input;
	ofstream output;
	Server(int argc, char *argv[]) {
		// Check to make sure the server can run
		this->input.open(argv[1]);	// Input case filename
		this->output.open(argv[2]); // Output case filename
		if (argc != 3) {
			cerr << "two arguments please!" << endl;
			MPI_Abort(MPI_COMM_WORLD, -1);
		}
		this->input >> this->NUM_GAMES; // get the number of games from first line of the input file
		// initialize a vector with all of the puzzles saved as a string
		this->puzzles.reserve(this->NUM_GAMES);
		for (int i = 0; i < this->NUM_GAMES; ++i) { // for each game in file...
			string input_string;
			this->input >> input_string; // reads line by line
			// each puzzle given by IDIM*JDIM characters per line
			if (input_string.size() != IDIM*JDIM) {
				cerr << "something wrong in input file format!" << endl;
				MPI_Abort(MPI_COMM_WORLD, -1);
			}
			this->puzzles.push_back(input_string);
		}
	}

	void record_output(vector<game_results> &games) {
	// called at the end of solve_puzzles() to write all intelligent puzzles to the output file
		int num_sol = games.size();
		for(int k = 0; k < num_sol; ++k) {
			this->output << "found solution = " << endl;
			games[k].game_board.Print(this->output);
			for (int i = 0; i < games[k].size; ++i) {
				games[k].game_board.makeMove(games[k].solution[i]);
				this->output << "-->" << endl;
				games[k].game_board.Print(this->output);
			}
			this->output << "solved" << endl;
		}
		cout << "found " << num_sol << " solutions" << endl;
	}

	void puzzle_copy(unsigned char buffer[], int batch_size) {
	// copies batch_size puzzle strings into the buffer reference from Server::puzzles
		for(int j = 0; j < batch_size; ++j) {
			// assumes unsigned chars in C string are each 1 byte
			std::memcpy(&buffer[j*IDIM*JDIM], this->puzzles[0].c_str(), IDIM*JDIM);
			this->puzzles.erase(this->puzzles.begin());
		}
	}

	void solve_locally(vector<game_results> &solved, int batch_size) {
		game_results results[batch_size];
		unsigned char server_buf[IDIM*JDIM*batch_size];
		this->puzzle_copy(server_buf, batch_size);
		search(server_buf, results, batch_size, 0);
		for(int i=0; i < batch_size; ++i) {
			if (results[i].found)
				solved.push_back(results[i]);
		}
	}

	void solve_puzzles(int num_proc) {
		vector<game_results> solved_puzzles;
		int SERVER_CHUNK = 0.25*CHUNK_SIZE;
		bool chunk_size_flag = false;
		int num_puzzles = this->NUM_GAMES;
		// unsigned char buf[num_proc-1][IDIM*JDIM*CHUNK_SIZE];
		MPI_Request send_req[num_proc-1];
		// MPI_Status recv_req[num_proc-1];
		int stop_proc_tag = 5;
		int more_work_tag = 10;
		int ret_result_tag = 100;
		int test_flag;
		unsigned char buf[IDIM*JDIM*CHUNK_SIZE];
		for(int i = 0; i < num_proc-1; ++i) {
			this->puzzle_copy(buf, CHUNK_SIZE);
			MPI_Send(buf, IDIM*JDIM*CHUNK_SIZE, MPI_UNSIGNED_CHAR, i+1, 1, MPI_COMM_WORLD);
			//MPI_Wait(&send_req[i], MPI_STATUS_IGNORE);
			num_puzzles -= CHUNK_SIZE;
		}
		// unsigned char buf[CHUNK_SIZE] = new unsigned char[IDIM*JDIM]; // new buffer for this iteration's game
		while (num_puzzles > 0) {
		// ########################### SERVER DOING TASKS ############################
			if(chunk_size_flag) {
				SERVER_CHUNK = this->puzzles.size();
				this->solve_locally(solved_puzzles, SERVER_CHUNK);
				num_puzzles -= SERVER_CHUNK;
				break;
			}
		// ################## SERVER CHECKING FOR COMPLETED TASKS FROM CLIENTS ####################
			if(num_puzzles < CHUNK_SIZE)
				chunk_size_flag = true;
			//MPI_Iprobe(i+1, ret_result_tag, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE);
			game_results recv_buffer[CHUNK_SIZE];
			MPI_Request recv_request;
			MPI_Status recv_status;
			MPI_Irecv(&recv_buffer, CHUNK_SIZE*sizeof(game_results), MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &recv_request);
			MPI_Request_get_status(recv_request, &test_flag, &recv_status);
			int idle_proc = recv_status.MPI_SOURCE;
			if(test_flag) {
				for(int k = 0; k < CHUNK_SIZE; ++k) {
					if(recv_buffer[k].found)
						solved_puzzles.push_back(recv_buffer[k]);
				}
			}
			else
				MPI_Wait(&recv_request, MPI_STATUS_IGNORE);
			// MPI_Irecv(recv_buffer, CHUNK_SIZE*sizeof(game_results), MPI_BYTE, i+1, MPI_ANY_TAG, MPI_COMM_WORLD, &recv_req[i]);
			// MPI_Wait(&recv_req[i], MPI_STATUS_IGNORE);
			if(!chunk_size_flag) {
				unsigned char new_buf[IDIM*JDIM*CHUNK_SIZE];
				this->puzzle_copy(new_buf, CHUNK_SIZE);
				MPI_Send(new_buf, IDIM*JDIM*CHUNK_SIZE, MPI_UNSIGNED_CHAR, idle_proc, more_work_tag, MPI_COMM_WORLD); // for Isend: &send_req[idle_proc-1]
				//MPI_Wait(&send_req[i], MPI_STATUS_IGNORE);
				num_puzzles -= CHUNK_SIZE;
				if(num_puzzles < CHUNK_SIZE)
					chunk_size_flag = true;
			}
			else {
				MPI_Send(buf, IDIM*JDIM*CHUNK_SIZE, MPI_UNSIGNED_CHAR, idle_proc, stop_proc_tag, MPI_COMM_WORLD);
			}
		}
		// DELETE buf here
		this->record_output(solved_puzzles);
	}
};


// Put the code for the client here
void Client(int rank){
	// send buf[] to the client processor and use game_board.Init() on the Client to
	// initialize game board. The result that will need to be sent back will either be
	// the moves required to solve the game or an indication that the game was not solvable.
	bool exit_flag = false;
	bool msg_flag = false;
	int stop_proc_tag = 5;
	int more_work_tag = 10;
	int ret_result_tag = 100;
	MPI_Request send_req;
	MPI_Request recv_req;
	MPI_Status recv_status;
	unsigned char buf[IDIM*JDIM*CHUNK_SIZE]; // receive buffer
	MPI_Recv(buf, IDIM*JDIM*CHUNK_SIZE, MPI_UNSIGNED_CHAR, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &recv_status);

	while(!exit_flag){
		game_results results[CHUNK_SIZE];
		search(buf, results, CHUNK_SIZE, rank);
		MPI_Send(results, CHUNK_SIZE*sizeof(game_results), MPI_BYTE, 0, ret_result_tag, MPI_COMM_WORLD);
		// if MPI receive value is specific status, break
		//MPI_Wait(&send_req, MPI_STATUS_IGNORE);
		MPI_Recv(buf, IDIM*JDIM*CHUNK_SIZE, MPI_UNSIGNED_CHAR, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &recv_status);
		//MPI_Iprobe(0, stop_proc_tag, MPI_COMM_WORLD, &exit_flag, MPI_STATUS_IGNORE);
		if(recv_status.MPI_TAG == stop_proc_tag)
			exit_flag = true;
	}
}

int main(int argc, char *argv[]) {
	// This is a utility routine that installs an alarm to kill off this
	// process if it runs to long.  This will prevent jobs from hanging
	// on the queue keeping others from getting their work done.
	chopsigs_();
	// All MPI programs must call this function
	MPI_Init(&argc, &argv);
	int myId;
	int numProcessors;
	/* Get the number of processors and my processor identification */
	MPI_Comm_size(MPI_COMM_WORLD, &numProcessors);
	MPI_Comm_rank(MPI_COMM_WORLD, &myId);
	if (myId == 0) {// Processor 0 runs the server code
		get_timer(); // zero the timer
		Server server_process(argc, argv);
		server_process.solve_puzzles(numProcessors);
		// Measure the running time of the server
		cout << "execution time = " << get_timer() << " seconds." << endl;
	}
	else {// all other processors run the client code.
		Client(myId);
	}
	// All MPI programs must call this before exiting
	MPI_Finalize();
}
