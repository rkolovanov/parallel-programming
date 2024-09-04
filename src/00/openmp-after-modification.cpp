#include <iostream>
#include <string>
#include <omp.h>

namespace
{
  constexpr int THREADS = 1;
  bool FLAG[THREADS];
}

inline void printHello()
{
  const int threadId = omp_get_thread_num();
  std::cout << "Hello from process " + std::to_string(threadId) + "\n";
  FLAG[threadId] = true;
}

int main()
{
  for (int i = 0; i < THREADS; ++i)
  {
	FLAG[i] = false;
  }

  printHello();

  #pragma omp parallel num_threads(THREADS)
  {
	if (omp_get_thread_num() != 0)
	{
	  while (FLAG[omp_get_thread_num() - 1] == false)
	  {
		continue;
	  }
	  printHello();
	}
  }

  return 0;
}
