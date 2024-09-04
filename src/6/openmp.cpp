#include <iostream>
#include <functional>
#include <array>
#include <list>
#include <vector>
#include <set>
#include <thread>
#include <fstream>
#include <omp.h>

namespace
{
  /*!
   * \brief Тип коллективной операции.
   */
  enum OperationType
  {
	SUM = 0
  };

  /*!
   * \brief Сообщение. Содержит данные и информацию об отправителе.
   */
  struct Message
  {
	static const int ANY_THREAD = -1;
	static const int ANY_TAG = -1;

	Message() :
	  data{ nullptr },
	  count{ 0 },
	  typeSize{ 0 },
	  senderId{ ANY_THREAD },
	  tag{ ANY_TAG }
	{}

	Message(const Message&) = delete;
	Message& operator=(const Message&) = delete;
	Message(Message&&) = delete;
	Message& operator=(Message&&) = delete;

	~Message()
	{
	  reset();
	}

	void setData(void* data, int count, int typeSize, int senderId = ANY_THREAD, int tag = ANY_TAG)
	{
	  reset();
	  this->senderId = senderId;
	  this->count = count;
	  this->typeSize = typeSize;
	  this->data = new char[count * typeSize];
	  this->tag = tag;
	  std::memcpy(this->data, data, count * typeSize);
	}

	void reset()
	{
	  if (data != nullptr)
	  {
		delete[] data;
		data = nullptr;
	  }
	  count = 0;
	  typeSize = 0;
	  senderId = ANY_THREAD;
	  tag = ANY_TAG;
	}

	void* data;		    // Указатель на данные
	size_t count;		// Количество данных
	size_t typeSize;	// Размер типа данных
	short int senderId; // ID потока-отправителя
	short int tag;		// Тег сообщения
  };

  /*!
   * \brief Хранилище сообщений. Хранит сообщения для определенного потока в виде связного списка.
   */
  struct ThreadInputStorage
  {
	explicit ThreadInputStorage() :
	  messages{},
	  storageLock{ nullptr }
	{
	  omp_init_lock(&storageLock);
	}

	~ThreadInputStorage()
	{
	  omp_destroy_lock(&storageLock);
	}

	void pushMessage(Message* message)
	{
	  omp_set_lock(&storageLock);
	  messages.push_back(message);
	  omp_unset_lock(&storageLock);
	}

	Message* popMessage(int senderId = Message::ANY_THREAD, int messageTag = Message::ANY_TAG)
	{
	  Message* result = nullptr;

	  omp_set_lock(&storageLock);
	  if (!messages.empty())
	  {
		for (auto it = messages.cbegin(); it != messages.cend(); ++it)
		{
		  if ((senderId == Message::ANY_THREAD || senderId == (*it)->senderId) && (messageTag == Message::ANY_TAG || (*it)->tag == messageTag))
		  {
			result = *it;
			messages.erase(it);
			break;
		  }
		}
	  }
	  omp_unset_lock(&storageLock);

	  return result;
	}

	std::list<Message*> messages;	// Связный список сообщений
	omp_lock_t storageLock;		    // Мьютекс на доступ к списку сообщений
  };

  constexpr int THREADS = 64;
  std::array<ThreadInputStorage, THREADS> INPUT_STORAGES;

  /*!
   * \brief Топология процессов в виде сетки.
   */
  struct ThreadGrid
  {
	int rows = 0;
	int columns = 0;
	std::vector<std::vector<int>> grid {};
	omp_lock_t storageLock = nullptr;

	ThreadGrid(int rows, int columns)
	{
	  omp_init_lock(&storageLock);

	  if (rows * columns < THREADS)
	  {
		throw std::runtime_error("Too big grid size.");
	  }

	  this->rows = rows;
	  this->columns = columns;

	  grid.resize(rows);
	  for (int i = 0; i < rows; ++i)
	  {
		grid[i].resize(columns);
	  }

	  for (int i = 0; i < rows; ++i)
	  {
		for (int j = 0; j < columns; ++j)
		{
		  grid[i][j] = i * columns + j;
		}
	  }
	}

	~ThreadGrid()
	{
	  omp_destroy_lock(&storageLock);
	}

	int getThreadIdByCoords(int row, int column)
	{
	  if (row < 0 || row > rows || column < 0 || column > columns)
	  {
		throw std::runtime_error("Invalid indexes.");
	  }

	  omp_set_lock(&storageLock);
	  const int id = grid[row][column];
	  omp_unset_lock(&storageLock);

	  return id;
	}

	std::pair<int, int> getCoordsByThreadId(int id)
	{
	  omp_set_lock(&storageLock);
	  for (int i = 0; i < rows; ++i)
	  {
		for (int j = 0; j < columns; ++j)
		{
		  if (id == grid[i][j])
		  {
			omp_unset_lock(&storageLock);
			return { i, j };
		  }
		}
	  }
	  omp_unset_lock(&storageLock);
	  return { -1, -1 };
	}

	void shift(int direction, int disp, int& sourceThreadId, int& destThreadId)
	{
	  static const auto fixIndex = [](int& index, int size)
	  {
		if (index < 0)
		{
		  int k = abs(index) / size;
		  index += (k + 1) * size;
		}
		index %= size;
	  };

	  const auto threadId = omp_get_thread_num();
	  const auto threadCoords = getCoordsByThreadId(threadId);

	  int sourceRow = threadCoords.first;
	  int sourceColumn = threadCoords.second;
	  int destRow = threadCoords.first;
	  int destColumn = threadCoords.second;

	  if (direction == 0)
	  {
		sourceRow -= disp;
		destRow += disp;
		fixIndex(sourceRow, rows);
		fixIndex(destRow, rows);
	  }

	  if (direction == 1)
	  {
		sourceColumn -= disp;
		destColumn += disp;
		fixIndex(sourceColumn, columns);
		fixIndex(destColumn, columns);
	  }

	  sourceThreadId = getThreadIdByCoords(sourceRow, sourceColumn);
	  destThreadId = getThreadIdByCoords(destRow, destColumn);
	}
  };

  /*!
   * \brief Функция отправки сообщения другому потоку.
   *
   * Функция является блокирующей - освобождается после того, как данные из входного буффера будут скопированы и отправлены.
   *
   * \param data Указатель на массив данных, который необходимо отправить
   * \param count Количество элементов в массиве данных
   * \param typeSize Размер одного элемента массива в байтах
   * \param destination ID потока, которому необходимо отправить сообщение
   * \param tag Тег сообщения
   */
  void sendData(void* data, int count, int typeSize, int destination, int tag = Message::ANY_TAG)
  {
	if (destination < 0 || destination >= THREADS)
	{
	  return;
	}

	auto& storage = INPUT_STORAGES.at(destination);
	auto* message = new Message;
	message->setData(data, count, typeSize, omp_get_thread_num(), tag);
	storage.pushMessage(message);
  }

  /*!
   * \brief Функция приема сообщения от другого потока.
   *
   * Функция является блокирующей - освобождается после того, как данные из сообщения буду получены.
   *
   * \param data Указатель на массив данных, куда необходимо записать полученные данные
   * \param count Количество элементов в массиве данных
   * \param typeSize Размер одного элемента массива в байтах
   * \param source ID потока, от которого необходимо получить сообщение
   * \param tag Тег сообщения
   */
  void recieveData(void* data, int count, int typeSize, int source = Message::ANY_THREAD, int tag = Message::ANY_TAG)
  {
	auto& storage = INPUT_STORAGES.at(omp_get_thread_num());
	auto* message = storage.popMessage(source, tag);

	while (message == nullptr)
	{
	  message = storage.popMessage(source, tag);
	}

	size_t size = count * typeSize;
	if (size > message->count * message->typeSize)
	{
	  size = message->count * message->typeSize;
	}

	std::memcpy(data, message->data, size);

	delete message;
  }

  /*!
  * \brief Функция сбора данных от всех потоков. Данные из sendBuffer всех потоков записываются в recvBuffer последовательно в порядке нумерации процессов.
  *
  * Функция является блокирующей - освобождается после того, как данные из сообщения буду получены.
  *
  * \param sendBuffer Указатель на массив данных, который необходимо отправить
  * \param recvBuffer Указатель на массив данных, куда необходимо записать полученные данные
  * \param count Количество элементов в массиве данных
  * \param typeSize Размер одного элемента массива в байтах
  * \param root ID потока, который должен собрать данные
  */
  void gatherData(void* sendBuffer, void* recvBuffer, int count, int typeSize, int root)
  {
	sendData(sendBuffer, count, typeSize, root);

	const auto threadId = omp_get_thread_num();
	if (threadId == root)
	{
	  for (int i = 0; i < THREADS; ++i)
	  {
		recieveData(static_cast<uint8_t*>(recvBuffer) + count * typeSize * i, count, typeSize, i);
	  }
	}
  }

  using ElementType = int;

  /*!
   * \brief Подматрица.
   */
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

  /*!
   * \brief Матрица.
   */
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

  /*!
   * \brief Загрузить матрицу из файла.
   */
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

  /*!
   * \brief Сохранить матрицу в файл.
   */
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

  /*!
   * \brief Сгенерировать случайную матрицу (элементы от 0 до 99).
   */
  void generateMatrix(Matrix& matrix) {
	srand(time(nullptr));
	size_t matrixSize = matrix.blockCount * matrix.blockSize;

	for (size_t y = 0; y < matrixSize; ++y) {
	  for (size_t x = 0; x < matrixSize; ++x) {
		matrix.getValue(x, y) = rand() % 100;
	  }
	}
  }
}

int main()
{
  const bool outputMatrix = false;
  const size_t matrixSize = 2048;
  const size_t blockCount = 8;
  const size_t blockSize = matrixSize / blockCount;

  if (blockCount * blockCount != THREADS || matrixSize % blockCount != 0)
  {
	std::cerr << "The number of blocks must be equal to the number of processes, and the size of the matrix must be a multiple of the number of blocks in a row/column.";
	return 0;
  }

  ThreadGrid grid(blockCount, blockCount);

  #pragma omp parallel num_threads(THREADS)
  {
	const auto threadId = omp_get_thread_num();
	const auto coords = grid.getCoordsByThreadId(threadId);

	Submatrix* blockA = nullptr;
	Submatrix* blockB = nullptr;
	Submatrix* blockC = nullptr;
	double startTime;

	if (threadId == 0) {
	  Matrix matrixA(blockCount, blockSize);
	  Matrix matrixB(blockCount, blockSize);

	  generateMatrix(matrixA);
	  generateMatrix(matrixB);

	  startTime = omp_get_wtime();

	  for (size_t y = 0; y < blockCount; ++y)
	  {
		for (size_t x = 0; x < blockCount; ++x)
		{
		  if (x != 0 || y != 0) {
			int destId = grid.getThreadIdByCoords(x, y);
			sendData(matrixA.getBlock(x, y)->data, blockSize * blockSize, sizeof(ElementType), destId, 1);
			sendData(matrixB.getBlock(x, y)->data, blockSize * blockSize, sizeof(ElementType), destId, 1);
		  }
		}
	  }

	  blockA = new Submatrix(blockSize);
	  blockB = new Submatrix(blockSize);
	  blockC = new Submatrix(blockSize);

	  memcpy(blockA->data, matrixA.getBlock(0, 0)->data, blockSize * blockSize * sizeof(ElementType));
	  memcpy(blockB->data, matrixB.getBlock(0, 0)->data, blockSize * blockSize * sizeof(ElementType));
	}
	else
	{
	  blockA = new Submatrix(blockSize);
	  blockB = new Submatrix(blockSize);
	  blockC = new Submatrix(blockSize);

	  recieveData(blockA->data, blockSize * blockSize, sizeof(ElementType), 0, 1);
	  recieveData(blockB->data, blockSize * blockSize, sizeof(ElementType), 0, 1);
	}

	{
	  int source, dest;
	  grid.shift(0, -coords.second, source, dest);
	  sendData(blockA->data, blockSize * blockSize, sizeof(int), dest, 2);
	  recieveData(blockA->data, blockSize * blockSize, sizeof(int), source, 2);
	}

	{
	  int source, dest;
	  grid.shift(1, -coords.first, source, dest);
	  sendData(blockB->data, blockSize * blockSize, sizeof(int), dest, 3);
	  recieveData(blockB->data, blockSize * blockSize, sizeof(int), source, 3);
	}

	for (size_t q = 0; q < blockCount; ++q)
	{
	  for (size_t y = 0; y < blockSize; ++y)
	  {
		for (size_t x = 0; x < blockSize; ++x)
		{
		  ElementType value = 0;

		  for (size_t i = 0; i < blockSize; ++i) {
			value += blockA->getValue(i, y) * blockB->getValue(x, i);
		  }

		  blockC->getValue(x, y) += value;
		}
	  }

	  {
		int source, dest;
		grid.shift(0, -1, source, dest);
		sendData(blockA->data, blockSize * blockSize, sizeof(ElementType), dest, 4);
		recieveData(blockA->data, blockSize * blockSize, sizeof(ElementType), source, 4);
	  }

	  {
		int source, dest;
		grid.shift(1, -1, source, dest);
		sendData(blockB->data, blockSize * blockSize, sizeof(ElementType), dest, 4);
		recieveData(blockB->data, blockSize * blockSize, sizeof(ElementType), source, 4);
	  }
	}

	Matrix* matrixC = nullptr;

	if (threadId == 0)
	{
	  matrixC = new Matrix(blockCount, blockSize);
	}

	{
	  ElementType* recvBuffer = nullptr;

	  if (threadId == 0)
	  {
		recvBuffer = new ElementType[matrixSize * matrixSize];
	  }

	  gatherData(blockC->data, recvBuffer, blockSize * blockSize, sizeof(ElementType), 0);

	  if (threadId == 0)
	  {
		for (size_t x = 0; x < blockCount; ++x)
		{
		  for (size_t y = 0; y < blockCount; ++y)
		  {
			memcpy(matrixC->getBlock(x, y)->data,
			  recvBuffer + x * blockSize * blockSize * blockCount + y * blockSize * blockSize,
			  blockSize * blockSize * sizeof(ElementType));
		  }
		}

		delete[] recvBuffer;
	  }
	}

	if (threadId == 0)
	{
	  double elapsedTime = omp_get_wtime() - startTime;
	  std::cout << "Elapsed time: " << elapsedTime << "\n";
	}

	if (outputMatrix && threadId == 0)
	{
	  for (size_t y = 0; y < matrixSize; ++y)
	  {
		for (size_t x = 0; x < matrixSize; ++x)
		{
		  std::cout << matrixC->getValue(x, y) << " ";
		}
		std::cout << "\n";
	  }
	}

	delete blockA;
	delete blockB;
	delete blockC;
	delete matrixC;
  }

  return 0;
}
