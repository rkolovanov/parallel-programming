#include <iostream>
#include <string>
#include <omp.h>

namespace
{
  constexpr int THREADS = 1;
}

inline void printHello()
{
  std::cout << "Hello from process " + std::to_string(omp_get_thread_num()) + "\n";
}

int main()
{
  printHello();

  #pragma omp parallel num_threads(THREADS)
  {
	if (omp_get_thread_num() != 0)
	{
	  printHello();
	}
  }

  return 0;
}
