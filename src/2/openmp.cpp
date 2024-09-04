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
  const int DATA_SIZE = 32;
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

  size_t size = count * typeSize;
  if (size > message->count * message->typeSize)
  {
	size = message->count * message->typeSize;
  }

  std::memcpy(data, message->data, size);

  delete message;
}

namespace wrapper
{
  struct Message {
	enum Type {
	  Data = 0,
	  Сonfirmation = 1,
	  Finish = 2,
	  Unknown = -1
	};

	Type type = Type::Unknown;
	int source = -1;
	int destination = -1;
	char data[DATA_SIZE];

	Message() = default;

	Message(const Type type, int source, int destination)
	{
	  this->type = type;
	  this->source = source;
	  this->destination = destination;
	}
  };

  void fillArrayWithData(char* data, int count) {
	for (int i = 0; i < count; ++i) {
	  data[i] = i % 256;
	}
  }
}

int main()
{
  if (THREADS < 2)
  {
	std::cerr << "At least two threads are required to work.\n";
	return 0;
  }

  double maxTime = -1;

  #pragma omp parallel num_threads(THREADS)
  {
	const auto threadId = omp_get_thread_num();
	double elapsedTime = -1;

	srand(time(nullptr) + static_cast<time_t>(threadId) * 1000);

	if (threadId == 0) {
	  wrapper::Message* message = new wrapper::Message;

	  // Перенаправляем пакеты
	  for (int i = 1; i <= (THREADS - 1) * 2; ++i) {
		recieveData(message, 1, sizeof(wrapper::Message));
		sendData(message, 1, sizeof(wrapper::Message), message->destination);
	  }

	  *message = { wrapper::Message::Type::Finish, 0, 0 };

	  // Отправляем пакеты с запросом на завершение
	  for (int i = 1; i < THREADS; ++i) {
		message->destination = i;
		sendData(message, 1, sizeof(wrapper::Message), message->destination);
	  }

	  delete message;
	}
	else {
	  int destinationProcess = 1 + rand() % (THREADS - 1);

	  wrapper::Message* message = new wrapper::Message{ wrapper::Message::Type::Data, threadId, destinationProcess};
	  wrapper::fillArrayWithData(message->data, DATA_SIZE);

	  double startTime = omp_get_wtime();

	  // Отправляем пакет с данными случайному процессу
	  sendData(message, 1, sizeof(wrapper::Message), 0);

	  // Принимаем пакеты от 0 процесса и обрабатываем их
	  do {
		recieveData(message, 1, sizeof(wrapper::Message), 0);

		if (message->type == wrapper::Message::Type::Data) {
		  *message = { wrapper::Message::Type::Сonfirmation, threadId, message->source };
		  sendData(message, 1, sizeof(wrapper::Message), 0);
		}
	  } while (message->type != wrapper::Message::Type::Finish);

	  elapsedTime = omp_get_wtime() - startTime;

	  #pragma omp critical
	  {
		if(elapsedTime > maxTime)
		{
		  maxTime = elapsedTime;
		}
	  }

	  delete message;
	}
  }

  printf("Elapsed time: %.7f", maxTime);

  return 0;
}
