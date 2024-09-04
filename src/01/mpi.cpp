#include <iostream>
#include <thread>
#include <mpi.h>

void fillArrayWithData(char* data, int count)
{
  for (int i = 0; i < count; ++i)
  {
	data[i] = i % 256;
  }
}

int main(int argc, char** argv) {
  const int messageMaxLength = 10000000, lengthStep = 10000;
  int processNumber, processRank;
  MPI_Status status;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &processNumber);
  MPI_Comm_rank(MPI_COMM_WORLD, &processRank);

  for (int length = 1; length <= messageMaxLength; length += lengthStep) {
	char* buffer = new char[length];

	if (processRank % 2 == 0) {
	  fillArrayWithData(buffer, length);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	double startTime = MPI_Wtime();

	if (processRank % 2 == 0) {
	  if (processRank < processNumber - processNumber % 2) {
		MPI_Send(buffer, length, MPI_CHAR, processRank + 1, 0, MPI_COMM_WORLD);
	  }
	}
	else {
	  MPI_Recv(buffer, length, MPI_CHAR, processRank - 1, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
	}

	double deltaTime = MPI_Wtime() - startTime;
	delete[] buffer;

	double maxTime;
	MPI_Reduce(&deltaTime, &maxTime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	if (processRank == 0) {
	  printf("%.7f, ", maxTime);
	  std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	MPI_Barrier(MPI_COMM_WORLD);
  }

  MPI_Finalize();

  return 0;
}
