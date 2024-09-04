#include <iostream>
#include <mpi.h>


int main(int argc, char** argv) {
	int processNumber, processRank;
	MPI_Status status;
	MPI_Comm matrixComm;

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &processNumber);
	MPI_Comm_rank(MPI_COMM_WORLD, &processRank);

	if (processNumber % 2 == 1) {
		if (processRank == 0) {
			std::cout << "The number of processes must be even.\n";
		}

		MPI_Finalize();
		return 0;
	}

	int dimensions[2] = { processNumber / 2 , 2 };
	int periods[2] = { 0, 0 };

	MPI_Cart_create(MPI_COMM_WORLD, 2, dimensions, periods, 1, &matrixComm);

	MPI_Comm rowComm;
	int subdimensions[2] = { true, false };

	MPI_Cart_sub(matrixComm, subdimensions, &rowComm);

	double data = 0.0;

	if (processRank == 0) {
		data = 5;
	} else if (processRank == 1) {
		data = 2.5;
	}

	double startTime = MPI_Wtime();
	MPI_Bcast(&data, 1, MPI_DOUBLE, 0, rowComm);
	double elapsedTime = MPI_Wtime() - startTime;

	std::cout << "Process: " << processRank << ", data: " << data << "\n";

	double maxTime;
	MPI_Reduce(&elapsedTime, &maxTime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	if (processRank == 0) {
		std::cout << "Elapsed time: " << maxTime << " seconds\n";
	}

	MPI_Finalize();
	return 0;
}
