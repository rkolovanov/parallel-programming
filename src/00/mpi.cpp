#include <iostream>
#include <mpi.h>


int main(int argc, char** argv) {
	int processNumber, processRank, recievedRank;
	MPI_Status status;

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &processNumber);
	MPI_Comm_rank(MPI_COMM_WORLD, &processRank);

	if (processRank == 0) {
		std::cout << "Hello from process " << processRank << "\n";

		for (int i = 1; i < processNumber; i++) {
			MPI_Recv(&recievedRank, 1, MPI_INT, i, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			std::cout << "Hello from process " << recievedRank << "\n";
		}

	} else {
		MPI_Send(&processRank, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
	}

	MPI_Finalize();

	return 0;
}
