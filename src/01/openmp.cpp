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
  constexpr int THREADS = 6;
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

void fillArrayWithData(char* data, int count)
{

  for (int i = 0; i < count; ++i)
  {
	data[i] = i % 256;
  }
}

int main()
{
  const int messageMaxLength = 10000000, lengthStep = 10000;

  for (int length = 1; length < messageMaxLength; length += lengthStep)
  {
	double maxTime = -1;

	#pragma omp parallel num_threads(THREADS)
	{
	  const auto threadId = omp_get_thread_num();
	  char* buffer = new char[length];

	  if (threadId % 2 == 0)
	  {
		fillArrayWithData(buffer, length);
	  }

	  #pragma omp barrier
	  double startTime = omp_get_wtime();

	  if (threadId % 2 == 0) {
		if (threadId < THREADS - THREADS % 2) {
		  sendData(buffer, length, sizeof(char), threadId + 1);
		}
	  }
	  else
	  {
		recieveData(buffer, length, sizeof(char), threadId - 1);
	  }

	  double elapsedTime = omp_get_wtime() - startTime;
	  delete[] buffer;

	  #pragma omp critical
	  {
		#pragma omp flush(maxTime)
		if (elapsedTime > maxTime)
		{
		  maxTime = elapsedTime;
		}
	  }

	  #pragma omp barrier
	}

	printf("%.7f, ", maxTime);
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  return 0;
}
