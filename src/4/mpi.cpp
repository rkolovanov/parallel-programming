#include <iostream>
#include <mpi.h>

int main(int argc, char** argv)
{
  int processNumber, processRank;
  MPI_Comm workers;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &processNumber);
  MPI_Comm_rank(MPI_COMM_WORLD, &processRank);

  srand(time(nullptr) + static_cast<time_t>(processRank) * 1000);

  const int isWorker = (processRank != 0) ? rand() % 2 : 1;
  double data = 1.0;
  double sum = 0.0;

  MPI_Comm_split(MPI_COMM_WORLD, (isWorker) ? isWorker : MPI_UNDEFINED, processRank, &workers);

  if (isWorker)
  {
	double startTime = MPI_Wtime();
	MPI_Allreduce(&data, &sum, 1, MPI_DOUBLE, MPI_SUM, workers);
	double elapsedTime = MPI_Wtime() - startTime;

	double maxTime;
	MPI_Reduce(&elapsedTime, &maxTime, 1, MPI_DOUBLE, MPI_MAX, 0, workers);

	if (processRank == 0)
	{
	  std::cout << "Elapsed time: " << maxTime << "\n";
	}
  }

  MPI_Finalize();

  return 0;
}
