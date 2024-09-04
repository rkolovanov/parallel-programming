#include <iostream>
#include <fstream>
#include <mpi.h>


using ElementType = int;


struct Submatrix {
	size_t size = 0;
	ElementType* data = nullptr;

	Submatrix(size_t size) {
		this->size = size;
		this->data = new ElementType[size * size];

		for (size_t i = 0; i < size * size; ++i) {
			this->data[i] = 0;
		}
	}

	~Submatrix() {
		delete[] data;
	}

	ElementType& getValue(size_t columnIndex, size_t rowIndex) {
		if (rowIndex >= size || columnIndex >= size) {
			throw std::exception("Invalid indexes.");
		}
		return *(data + rowIndex * size + columnIndex);
	}
};


struct Matrix {
	size_t blockCount = 0;
	size_t blockSize = 0;
	Submatrix** blocks = nullptr;

	Matrix(size_t blockCount, size_t blockSize) {
		this->blockCount = blockCount;
		this->blockSize = blockSize;
		this->blocks = new Submatrix * [blockCount * blockCount];

		for (size_t i = 0; i < blockCount * blockCount; ++i) {
			this->blocks[i] = new Submatrix(blockSize);
		}
	}

	~Matrix() {
		for (size_t i = 0; i < blockCount * blockCount; ++i) {
			delete blocks[i];
		}

		delete[] blocks;
	}

	Submatrix* getBlock(size_t rowIndex, size_t columnIndex) {
		if (rowIndex >= blockCount || columnIndex >= blockCount) {
			throw std::exception("Invalid indexes.");
		}
		return *(blocks + rowIndex * blockCount + columnIndex);
	}

	ElementType& getValue(size_t columnIndex, size_t rowIndex) {
		return getBlock(columnIndex / blockSize, rowIndex / blockSize)
			->getValue(columnIndex % blockSize, rowIndex % blockSize);
	}
};


void readMatrixFromFile(Matrix& matrix, const std::string& path) {
	std::ifstream file(path);

	if (file.is_open()) {
		size_t matrixSize = matrix.blockCount * matrix.blockSize;

		for (size_t y = 0; y < matrixSize; ++y) {
			for (size_t x = 0; x < matrixSize; ++x) {
				ElementType value;

				file >> value;

				matrix.getValue(x, y) = value;
			}
		}
	}

	file.close();
}

void saveMatrixToFile(Matrix& matrix, const std::string& path) {
	std::ofstream file(path);

	if (file.is_open()) {
		size_t matrixSize = matrix.blockCount * matrix.blockSize;

		for (size_t y = 0; y < matrixSize; ++y) {
			for (size_t x = 0; x < matrixSize; ++x) {
				file << matrix.getValue(x, y) << " ";
			}
			file << "\n";
		}
	}

	file.close();
}

void generateMatrix(Matrix& matrix) {
	srand(time(nullptr));
	size_t matrixSize = matrix.blockCount * matrix.blockSize;

	for (size_t y = 0; y < matrixSize; ++y) {
		for (size_t x = 0; x < matrixSize; ++x) {
			matrix.getValue(x, y) = rand() % 100;
		}
	}
}

int main(int argc, char** argv) {
	const bool outputMatrix = false;
	const size_t matrixSize = 4096;
	const size_t blockCount = 2;
	const size_t blockSize = matrixSize / blockCount;
	int processNumber, processRank;

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &processNumber);
	MPI_Comm_rank(MPI_COMM_WORLD, &processRank);

	if (blockCount * blockCount != processNumber || matrixSize % blockCount != 0) {
		if (processRank == 0) {
			std::cerr << "The number of blocks must be equal to the number of processes, and the size of the matrix must be a multiple of the number of blocks in a row/column.";
		}

		MPI_Finalize();
		return 0;
	}

	MPI_Status status;
	MPI_Comm matrixBlockCommutator;
	int dimensions[2] = { blockCount, blockCount };
	int periods[2] = { 1, 1 };
	int coords[2] = { 0, 0 };

	MPI_Cart_create(MPI_COMM_WORLD, 2, dimensions, periods, 1, &matrixBlockCommutator);
	MPI_Cart_coords(matrixBlockCommutator, processRank, 2, coords);

	Submatrix* blockA = nullptr;
	Submatrix* blockB = nullptr;
	Submatrix* blockC = nullptr;
	double startTime;

	if (processRank == 0) {
		Matrix matrixA(blockCount, blockSize);
		Matrix matrixB(blockCount, blockSize);

		generateMatrix(matrixA);
		generateMatrix(matrixB);
		//readMatrixFromFile(matrixA, "matrix6_6.txt");
		//readMatrixFromFile(matrixB, "matrix6_6.txt");

		startTime = MPI_Wtime();

		for (size_t y = 0; y < blockCount; ++y) {
			for (size_t x = 0; x < blockCount; ++x) {
				if (x != 0 || y != 0) {
					int rank;
					int coords[2] = { x, y };

					MPI_Cart_rank(matrixBlockCommutator, coords, &rank);
					MPI_Send(matrixA.getBlock(x, y)->data, blockSize * blockSize, MPI_INT, rank, 0, MPI_COMM_WORLD);
					MPI_Send(matrixB.getBlock(x, y)->data, blockSize * blockSize, MPI_INT, rank, 0, MPI_COMM_WORLD);
				}
			}
		}

		blockA = new Submatrix(blockSize);
		blockB = new Submatrix(blockSize);
		blockC = new Submatrix(blockSize);

		memcpy(blockA->data, matrixA.getBlock(0, 0)->data, blockSize * blockSize * sizeof(ElementType));
		memcpy(blockB->data, matrixB.getBlock(0, 0)->data, blockSize * blockSize * sizeof(ElementType));
	}
	else {
		blockA = new Submatrix(blockSize);
		blockB = new Submatrix(blockSize);
		blockC = new Submatrix(blockSize);

		MPI_Recv(blockA->data, blockSize * blockSize, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);
		MPI_Recv(blockB->data, blockSize * blockSize, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);
	}

	{
		int source, dest;
		ElementType* tempData = new ElementType[blockSize * blockSize];

		MPI_Cart_shift(matrixBlockCommutator, 0, -coords[1], &source, &dest);
		MPI_Sendrecv(blockA->data, blockSize * blockSize, MPI_INT, dest, 0, tempData, blockSize * blockSize, MPI_INT, source, 0, MPI_COMM_WORLD, &status);
		memcpy(blockA->data, tempData, blockSize * blockSize * sizeof(ElementType));

		delete[] tempData;
	}

	{
		int source, dest;
		ElementType* tempData = new ElementType[blockSize * blockSize];

		MPI_Cart_shift(matrixBlockCommutator, 1, -coords[0], &source, &dest);
		MPI_Sendrecv(blockB->data, blockSize * blockSize, MPI_INT, dest, 1, tempData, blockSize * blockSize, MPI_INT, source, 1, MPI_COMM_WORLD, &status);
		memcpy(blockB->data, tempData, blockSize * blockSize * sizeof(ElementType));

		delete[] tempData;
	}

	for (size_t q = 0; q < blockCount; ++q) {
		for (size_t y = 0; y < blockSize; ++y) {
			for (size_t x = 0; x < blockSize; ++x) {
				ElementType value = 0;

				for (size_t i = 0; i < blockSize; ++i) {
					value += blockA->getValue(i, y) * blockB->getValue(x, i);
				}

				blockC->getValue(x, y) += value;
			}
		}

		{
			int source, dest;
			ElementType* tempData = new ElementType[blockSize * blockSize];

			MPI_Cart_shift(matrixBlockCommutator, 0, -1, &source, &dest);
			MPI_Sendrecv(blockA->data, blockSize * blockSize, MPI_INT, dest, 0, tempData, blockSize * blockSize, MPI_INT, source, 0, MPI_COMM_WORLD, &status);
			memcpy(blockA->data, tempData, blockSize * blockSize * sizeof(ElementType));

			delete[] tempData;
		}

		{
			int source, dest;
			ElementType* tempData = new ElementType[blockSize * blockSize];

			MPI_Cart_shift(matrixBlockCommutator, 1, -1, &source, &dest);
			MPI_Sendrecv(blockB->data, blockSize * blockSize, MPI_INT, dest, 0, tempData, blockSize * blockSize, MPI_INT, source, 0, MPI_COMM_WORLD, &status);
			memcpy(blockB->data, tempData, blockSize * blockSize * sizeof(ElementType));

			delete[] tempData;
		}
	}

	Matrix* matrixC = nullptr;

	if (processRank == 0) {
		matrixC = new Matrix(blockCount, blockSize);
	}

	{
		ElementType* recvBuffer = nullptr;

		if (processRank == 0) {
			recvBuffer = new ElementType[matrixSize * matrixSize];
		}

		MPI_Gather(blockC->data, blockSize * blockSize, MPI_INT, recvBuffer, blockSize * blockSize, MPI_INT, 0, MPI_COMM_WORLD);

		if (processRank == 0) {
			for (size_t x = 0; x < blockCount; ++x) {
				for (size_t y = 0; y < blockCount; ++y) {
					memcpy(matrixC->getBlock(x, y)->data,
						recvBuffer + x * blockSize * blockSize * blockCount + y * blockSize * blockSize,
						blockSize * blockSize * sizeof(ElementType));
				}
			}

			delete[] recvBuffer;
		}
	}

	if (processRank == 0) {
		double elapsedTime = MPI_Wtime() - startTime;
		std::cout << "Elapsed time: " << elapsedTime << " sec.\n";
	}

	if (outputMatrix && processRank == 0) {
		for (size_t y = 0; y < matrixSize; ++y) {
			for (size_t x = 0; x < matrixSize; ++x) {
				std::cout << matrixC->getValue(x, y) << " ";
			}
			std::cout << "\n";
		}
	}

	delete blockA;
	delete blockB;
	delete blockC;
	delete matrixC;

	MPI_Finalize();
	return 0;
}
