#include <iostream>
#include <mpi.h>

int main(int argc, char** argv) {
  int processNumber, processRank;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &processNumber);
  MPI_Comm_rank(MPI_COMM_WORLD, &processRank);

  srand(time(nullptr) + static_cast<time_t>(1000) * processRank);

  int* sendBuffer = new int[processNumber + 5];
  int* receiveBuffer = new int[processNumber + 5];

  for (int i = 0; i < processNumber + 5; ++i) {
	sendBuffer[i] = rand() % 11;
  }

  double startTime = MPI_Wtime();

  MPI_Reduce(sendBuffer, receiveBuffer, processNumber + 5, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

  double elapsedTime = MPI_Wtime() - startTime;

  if (processRank == 0) {
	std::cout << "Elapsed time: " << elapsedTime << "\n";
  }

  delete[] sendBuffer;
  delete[] receiveBuffer;

  MPI_Finalize();

  return 0;
}
