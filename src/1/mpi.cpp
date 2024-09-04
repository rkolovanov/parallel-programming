#include <iostream>
#include <mpi.h>

int main(int argc, char** argv) {
  const int dataSize = 100000000;
  int processNumber, processRank;
  MPI_Status status;

  srand(time(nullptr));

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &processNumber);
  MPI_Comm_rank(MPI_COMM_WORLD, &processRank);

  int dataBlockSize, remainDataSize;
  if (processNumber != 1) {
	dataBlockSize = dataSize / (processNumber - 1);
	remainDataSize = dataSize % (processNumber - 1);
  }
  else {
	dataBlockSize = 0;
	remainDataSize = dataSize;
  }

  if (processRank == 0) {
	int count = 0;
	int* data = new int[dataSize];

	for (int i = 0; i < dataSize; ++i) {
	  data[i] = rand() % 11 - 5;
	}

	double startTime = MPI_Wtime();

	for (int i = 1; i < processNumber; ++i) {
	  MPI_Send(data + (i - 1) * dataBlockSize, dataBlockSize, MPI_INT, i, 0, MPI_COMM_WORLD);
	}

	for (int i = dataSize - 1; dataSize - 1 - remainDataSize < i; --i) {
	  if (data[i] == 0) {
		++count;
	  }
	}

	for (int result = 0, i = 1; i < processNumber; ++i) {
	  MPI_Recv(&result, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &status);
	  count += result;
	}

	double deltaTime = MPI_Wtime() - startTime;

	delete[] data;

	std::cout << "Number of '0' in array: " << count << "\n";
	std::cout << "Elapsed time: " << deltaTime << "\n";
  }
  else {
	int count = 0;
	int* data = new int[dataBlockSize];

	MPI_Recv(data, dataBlockSize, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);

	for (int i = 0; i < dataBlockSize; ++i) {
	  if (data[i] == 0) {
		++count;
	  }
	}

	MPI_Send(&count, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
  }

  MPI_Finalize();
  return 0;
}
