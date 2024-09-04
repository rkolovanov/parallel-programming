#include <iostream>
#include <mpi.h>

namespace {
  const size_t DATA_SIZE = 32;
}

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

int main2(int argc, char** argv) {
  int processNumber, processRank;
  MPI_Status status;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &processNumber);
  MPI_Comm_rank(MPI_COMM_WORLD, &processRank);

  srand(time(nullptr) + static_cast<time_t>(processRank) * 1000);

  if (processNumber < 2) {
	std::cerr << "At least two processes are required to work.\n";
	MPI_Finalize();
	return 0;
  }

  double elapsedTime = -1;

  if (processRank == 0) {
	Message* message = new Message;

	// Перенаправляем пакеты
	for (int i = 1; i <= (processNumber - 1) * 2; ++i) {
	  MPI_Recv(message, sizeof(Message), MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
	  MPI_Send(message, sizeof(Message), MPI_BYTE, message->destination, message->type, MPI_COMM_WORLD);
	}

	*message = { Message::Type::Finish, 0, 0 };

	// Отправляем пакеты с запросом на завершение
	for (int i = 1; i < processNumber; ++i) {
	  message->destination = i;
	  MPI_Send(message, sizeof(Message), MPI_BYTE, message->destination, message->type, MPI_COMM_WORLD);
	}

	delete message;
  }
  else {
	int destinationProcess = 1 + rand() % (processNumber - 1);

	Message* message = new Message(Message::Type::Data, processRank, destinationProcess);
	fillArrayWithData(message->data, DATA_SIZE);

	double startTime = MPI_Wtime();

	// Отправляем пакет с данными случайному процессу
	MPI_Send(message, sizeof(Message), MPI_BYTE, 0, Message::Type::Data, MPI_COMM_WORLD);

	// Принимаем пакеты от 0 процесса и обрабатываем их
	do {
	  MPI_Recv(message, sizeof(Message), MPI_BYTE, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

	  if (message->type == Message::Type::Data) {
		*message = { Message::Type::Сonfirmation, processRank, message->source};
		MPI_Send(message, sizeof(Message), MPI_BYTE, 0, Message::Type::Сonfirmation, MPI_COMM_WORLD);
	  }

	} while (message->type != Message::Type::Finish);

	elapsedTime = MPI_Wtime() - startTime;

	delete message;
  }

  double maxTime = -1;
  MPI_Reduce(&elapsedTime, &maxTime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  if (processRank == 0)
  {
	std::cout << "Elapsed time: " << maxTime << "\n";
	std::cout.flush();
  }

  MPI_Finalize();
  return 0;
}
