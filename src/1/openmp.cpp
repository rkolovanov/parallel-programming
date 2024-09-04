#include <iostream>
#include <array>
#include <list>
#include <thread>
#include <omp.h>

/*!
 * \brief Сообщение. Содержит данные и информацию об отправителе.
 */
struct Message
{
  static const int ANY_THREAD = -1;

  Message() :
	data{ nullptr },
	count{ 0 },
	typeSize{ 0 },
	senderId{ ANY_THREAD }
  {}

  Message(const Message&) = delete;
  Message& operator=(const Message&) = delete;
  Message(Message&&) = delete;
  Message& operator=(Message&&) = delete;

  ~Message()
  {
	reset();
  }

  void setData(void* data, int count, int typeSize, int senderId = ANY_THREAD)
  {
	reset();
	this->senderId = senderId;
	this->count = count;
	this->typeSize = typeSize;
	this->data = new char[count * typeSize];
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
  }

  void* data;		  // Указатель на данные
  size_t count;		  // Количество данных
  size_t typeSize;	  // Размер типа данных
  short int senderId; // ID потока-отправителя
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

  Message* popMessage(int senderId = Message::ANY_THREAD)
  {
	Message* result = nullptr;

	omp_set_lock(&storageLock);
	if (!messages.empty())
	{
	  for (auto it = messages.cbegin(); it != messages.cend(); ++it)
	  {
		if (senderId == Message::ANY_THREAD || senderId == (*it)->senderId)
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

  omp_lock_t storageLock;		// Мьютекс на доступ к списку сообщений
  std::list<Message*> messages;	// Связный список сообщений
};

namespace
{
  constexpr int THREADS = 4;
  const int DATA_SIZE = 100;
  std::array<ThreadInputStorage, THREADS> INPUT_STORAGES;
}

/*!
 * \brief Функция отправки сообщения другому потоку.
 *
 * Функция является блокирующей - освобождается после того, как данные из входного буффера будут скопированы и отправлены.
 *
 * \param data Указатель на массив данных, который необходимо отправить.
 * \param count Количество элементов в массиве данных
 * \param typeSize Размер одного элемента массива в байтах
 * \param destination ID потока, которому необходимо отправить сообщение
 */
void sendData(void* data, int count, int typeSize, int destination)
{
  if (destination < 0 || destination >= THREADS)
  {
	return;
  }

  auto& storage = INPUT_STORAGES.at(destination);
  auto* message = new Message;
  message->setData(data, count, typeSize, omp_get_thread_num());
  storage.pushMessage(message);
}

/*!
 * \brief Функция приема сообщения от другого потока.
 *
 * Функция является блокирующей - освобождается после того, как данные из сообщения буду получены.
 *
 * \param data Указатель на массив данных, куда необходимо записать полученные данные.
 * \param count Количество элементов в массиве данных
 * \param typeSize Размер одного элемента массива в байтах
 * \param source ID потока, от которого необходимо получить сообщение
 */
Message* recieveData(void* data, int count, int typeSize, int source = Message::ANY_THREAD)
{
  auto& storage = INPUT_STORAGES.at(omp_get_thread_num());
  auto* message = storage.popMessage(source);

  while (message == nullptr)
  {
	message = storage.popMessage(source);
  }

  int size = count * typeSize;
  if (size > message->count * message->typeSize)
  {
	size = message->count * message->typeSize;
  }

  std::memcpy(data, message->data, size);

  delete message;
}

int main()
{
  srand(time(nullptr));

  #pragma omp parallel num_threads(THREADS)
  {
	const auto threadId = omp_get_thread_num();
	int dataBlockSize, remainDataSize;

	if (THREADS != 1) {
	  dataBlockSize = DATA_SIZE / (THREADS - 1);
	  remainDataSize = DATA_SIZE % (THREADS - 1);
	}
	else {
	  dataBlockSize = 0;
	  remainDataSize = DATA_SIZE;
	}

	if (threadId == 0) {
	  int count = 0;
	  int* data = new int[DATA_SIZE];

	  for (int i = 0; i < DATA_SIZE; ++i) {
		data[i] = rand() % 11 - 5;
	  }

	  double startTime = omp_get_wtime();

	  for (int i = 1; i < THREADS; ++i) {
		sendData(data + (i - 1) * dataBlockSize, dataBlockSize, sizeof(int), i);
	  }

	  for (int i = DATA_SIZE - 1; DATA_SIZE - 1 - remainDataSize < i; --i) {
		if (data[i] == 0) {
		  ++count;
		}
	  }

	  for (int result = 0, i = 1; i < THREADS; ++i) {
		recieveData(&result, 1, sizeof(int), i);
		count += result;
	  }

	  double deltaTime = omp_get_wtime() - startTime;

	  delete[] data;

	  std::cout << "Number of '0' in array: " << count << "\n";
	  std::cout << "Elapsed time: " << deltaTime << "\n";
	}
	else {
	  int count = 0;
	  int* data = new int[dataBlockSize];

	  recieveData(data, dataBlockSize, sizeof(int), 0);

	  for (int i = 0; i < dataBlockSize; ++i) {
		if (data[i] == 0) {
		  ++count;
		}
	  }

	  sendData(&count, 1, sizeof(int), 0);
	}
  }

  return 0;
}
