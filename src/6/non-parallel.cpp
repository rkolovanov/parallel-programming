#include <iostream>
#include <fstream>
#include <chrono>

namespace non_parallel_functions
{
  using ElementType = float;

  struct Matrix
  {
	size_t size = 0;
	ElementType* data = nullptr;

	Matrix(size_t size)
	{
	  this->size = size;
	  this->data = new ElementType[size * size];

	  for (size_t i = 0; i < size * size; ++i)
	  {
		this->data[i] = 0;
	  }
	}

	~Matrix()
	{
	  delete[] data;
	}

	ElementType& getValue(size_t rowIndex, size_t columnIndex)
	{
	  if (rowIndex >= size || columnIndex >= size)
	  {
		throw std::exception("Invalid indexes.");
	  }
	  return *(data + rowIndex * size + columnIndex);
	}
  };


  void readMatrixFromFile(Matrix& matrix, const std::string& path)
  {
	std::ifstream file(path);

	if (file.is_open())
	{
	  for (size_t y = 0; y < matrix.size; ++y)
	  {
		for (size_t x = 0; x < matrix.size; ++x)
		{
		  ElementType value;

		  file >> value;

		  matrix.getValue(x, y) = value;
		}
	  }
	}

	file.close();
  }

  void saveMatrixToFile(Matrix& matrix, const std::string& path)
  {
	std::ofstream file(path);

	if (file.is_open())
	{
	  for (size_t y = 0; y < matrix.size; ++y)
	  {
		for (size_t x = 0; x < matrix.size; ++x)
		{
		  file << matrix.getValue(x, y) << " ";
		}
		file << "\n";
	  }
	}

	file.close();
  }

  void generateMatrix(Matrix& matrix)
  {
	srand(time(nullptr));

	for (size_t y = 0; y < matrix.size; ++y)
	{
	  for (size_t x = 0; x < matrix.size; ++x)
	  {
		matrix.getValue(x, y) = rand() % 100;
	  }
	}
  }
}

using namespace non_parallel_functions;

int main(int argc, char** argv)
{
  const bool matrixOutput = false;
  const size_t matrixSize = 128;

  Matrix matrixA(matrixSize);
  Matrix matrixB(matrixSize);
  Matrix matrixC(matrixSize);

  generateMatrix(matrixA);
  generateMatrix(matrixB);

  auto startTime = std::chrono::steady_clock::now();

  for (size_t y = 0; y < matrixSize; ++y)
  {
	for (size_t x = 0; x < matrixSize; ++x)
	{
	  ElementType value = 0;

	  for (size_t i = 0; i < matrixSize; ++i)
	  {
		value += matrixA.getValue(i, y) * matrixB.getValue(x, i);
	  }

	  matrixC.getValue(x, y) = value;
	}
  }

  auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime);
  std::cout << "Elapsed time: " << (double)elapsedTime.count() / 1000000 << "\n";

  if (matrixOutput)
  {
	for (size_t y = 0; y < matrixSize; ++y)
	{
	  for (size_t x = 0; x < matrixSize; ++x)
	  {
		std::cout << matrixC.getValue(x, y) << " ";
	  }
	  std::cout << std::endl;
	}
  }

  return 0;
}
