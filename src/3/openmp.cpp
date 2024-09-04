#include <iostream>
#include <array>
#include <functional>
#include <list>
#include <thread>
#include <omp.h>

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
  constexpr int THREADS = 1;
  std::array<ThreadInputStorage, THREADS> INPUT_STORAGES;
}

/*!
 * \brief Функция отправки сообщения другому потоку.
 *
 * Функция является блокирующей - освобождается после того, как данные из входного буффера будут скопированы и отправлены.
 *
 * \param data Указатель на массив данных, который необходимо отправить
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
 * \param data Указатель на массив данных, куда необходимо записать полученные данные
 * \param count Количество элементов в массиве данных
 * \param typeSize Размер одного элемента массива в байтах
 * \param source ID потока, от которого необходимо получить сообщение
 */
void recieveData(void* data, int count, int typeSize, int source = Message::ANY_THREAD)
{
  auto& storage = INPUT_STORAGES.at(omp_get_thread_num());
  auto* message = storage.popMessage(source);

  while (message == nullptr)
  {
	message = storage.popMessage(source);
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
 * \brief Функция коллективного приема сообщений от других потоков и выполнения операций над данными.
 *
 * Функция является блокирующей - освобождается после того, как сообщение будет отправлено (для отправителей) или как все сообщения будут получены и над ними будет выполнена операция (для получателя).
 *
 * \param sendBuffer Указатель на массив данных, которые нужно отправить
 * \param recvBuffer Указатель на массив данных, куда необходимо записать полученные данные
 * \param count Количество элементов в массиве данных
 * \param root ID потока, который принимает данные
 * \param operation Операция, осуществляемая над данными
 */
template<typename T>
Message* reduceData(void* sendBuffer, void* recvBuffer, int count, int root, const OperationType operation)
{
  const auto threadId = omp_get_thread_num();
  if (root == threadId)
  {
	T* resultBuffer = reinterpret_cast<T*>(recvBuffer);
	T* tempBuffer = new T[count];

	std::memcpy(recvBuffer, sendBuffer, count * sizeof(T));

	for (int i = 0; i < THREADS; ++i)
	{
	  if (i == root)
		continue;

	  recieveData(tempBuffer, count, sizeof(T), i);

	  for (int j = 0; j < count; ++j)
	  {
		if (operation == OperationType::SUM)
		{
		  resultBuffer[j] += tempBuffer[j];
		}
	  }
	}

	delete[] tempBuffer;
  }
  else
  {
	sendData(sendBuffer, count, sizeof(T), root);
  }
}

int main()
{
  const int arraySize = THREADS + 5;

  #pragma omp parallel num_threads(THREADS)
  {
	const auto threadId = omp_get_thread_num();

	srand(time(nullptr) + static_cast<time_t>(1000) * threadId);

	int* sendBuffer = new int[arraySize];
	int* receiveBuffer = new int[arraySize];

	for (int i = 0; i < arraySize; ++i) {
	  sendBuffer[i] = rand() % 11;
	}

	double startTime = omp_get_wtime();

	reduceData<int>(sendBuffer, receiveBuffer, arraySize, 0, OperationType::SUM);
	#pragma omp barrier

	double elapsedTime = omp_get_wtime() - startTime;

	if (threadId == 0) {
	  printf("Elapsed time: %.7f", elapsedTime);
	}

	delete[] sendBuffer;
	delete[] receiveBuffer;
  }

  return 0;
}
