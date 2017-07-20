#include <cstdio>
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <cerrno>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "XNRW/include/ThreadPool.h"

#define REPLY_LEN 2
// #define DEBUG

#ifdef DEBUG
std::mutex printMtx;
#endif

int main(int argc, char const *argv[]) {
  if (argc < 5) {
    std::cerr << "Usage: server <port> <number of connections> "
              << "<number of messages> <message size in bytes>" << std::endl;
    return -1;
  }
  const auto port = std::stoi(argv[1], nullptr, 10);
  const auto numConn = std::stoull(argv[2], nullptr, 10);
  const auto numMessage = std::stoull(argv[3], nullptr, 10);
  const auto messageSize = std::stoull(argv[4], nullptr, 10);

#ifdef DEBUG
  {
    std::lock_guard<std::mutex> l(printMtx);
    std::cout << "messageSize: " << messageSize
              << ", numConn: " << numConn << std::endl;
  }
#endif


#ifdef DEBUG
  {
    std::lock_guard<std::mutex> l(printMtx);
    std::cout << "Creating thread pool..." << std::endl;
  }
#endif

  XNRW::ThreadPool threadPool(numConn);

#ifdef DEBUG
  {
    std::lock_guard<std::mutex> l(printMtx);
    std::cout << "Thread pool created." << std::endl;
  }
#endif

  int serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  int enable = 1;
  if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR,
                 &enable, sizeof(int)) < 0) {

    std::cerr << "setsockopt(SO_REUSEADDR) failed" << std::endl;
    return -1;
  }
  struct sockaddr_in serverAddress, clientAddress;
  memset(&serverAddress, 0, sizeof(serverAddress));
  serverAddress.sin_family = AF_INET;
  serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");
  serverAddress.sin_port = htons(port);
  if (bind(serverSocket, (struct sockaddr *)&serverAddress,
           sizeof(serverAddress)) < 0) {
    std::cerr << "Bind socket failed." << std::endl;
    perror("Error: ");
    return -1;
  }
  setsockopt(serverSocket, SOL_SOCKET, SO_RCVBUF,
             &messageSize, sizeof(messageSize));
  setsockopt(serverSocket, SOL_SOCKET, SO_SNDBUF,
             &messageSize, sizeof(messageSize));
#ifdef DEBUG
  {
    std::lock_guard<std::mutex> l(printMtx);
    std::cout << "Socket bind." << std::endl;
  }
#endif
  listen(serverSocket, numConn);

#ifdef DEBUG
  {
    std::lock_guard<std::mutex> l(printMtx);
    std::cout << "Listening on socket..." << std::endl;
  }
#endif

  for (auto i = 0ull; i < numConn; i++) {
    socklen_t clientLen = sizeof(clientAddress);
#ifdef DEBUG
    {
      std::lock_guard<std::mutex> l(printMtx);
      std::cout << "Waiting for "<< i + 1 << "th connection..." << std::endl;
    }
#endif
    int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddress,
                              &clientLen);
#ifdef DEBUG
    {
      std::lock_guard<std::mutex> l(printMtx);
      std::cout << "Connection " << clientSocket << " accepted." << std::endl;
      std::cout << "Adding task to thread pool." << std::endl;
    }
#endif
    threadPool.addTask([clientSocket, numMessage, messageSize, i]() {
#ifdef DEBUG
    {
      std::lock_guard<std::mutex> l(printMtx);
      std::cout << "["  << clientSocket << "] Task initiated for connection "
                << clientSocket << std::endl;
    }
#endif
      std::unique_ptr<char> buffer(new char[messageSize + 1]);
      memset(buffer.get(), 0, messageSize + 1);
      std::unique_ptr<char> reply(new char[REPLY_LEN + 1]);
      memset(reply.get(), 0, REPLY_LEN + 1);
      strcpy(reply.get(), "OK");
#ifdef DEBUG
      {
        std::lock_guard<std::mutex> l(printMtx);
        std::cout << "["  << clientSocket
                  << "] Reading from client..." << std::endl;
      }
#endif
      for (auto j = 0ull; j < numMessage; j++) {
        if (recv(clientSocket, buffer.get(), messageSize, MSG_WAITALL) < 0) {
          std::cerr << "Recv failed in connection " << i
                    << " message " << j << std::endl;
          perror("Error: ");
          exit(-1);
        }
#ifdef DEBUG
        {
          std::lock_guard<std::mutex> l(printMtx);
          std::cout << "["  << clientSocket
                    << "] Received from client." << std::endl;
          std::cout << "["  << clientSocket
                    << "] Replying to client." << std::endl;
        }
#endif
        if (send(clientSocket, reply.get(), REPLY_LEN, 0) < 0) {
          std::cerr << "Send failed in connection " << i
                    << " message " << j << std::endl;
          perror("Error: ");
          exit(-1);
        }
      }
#ifdef DEBUG
      {
        std::lock_guard<std::mutex> l(printMtx);
        std::cout << "["  << clientSocket
                  << "] Closing connection." << std::endl;
      }
#endif
      close(clientSocket);
#ifdef DEBUG
      {
        std::lock_guard<std::mutex> l(printMtx);
        std::cout << "["  << clientSocket
                  << "] Connection closed." << std::endl;
      }
#endif
    });
  }

  threadPool.wait();

#ifdef DEBUG
  {
    std::lock_guard<std::mutex> l(printMtx);
    std::cout << "All threads completed." << std::endl;
  }
#endif

  return 0;
}
