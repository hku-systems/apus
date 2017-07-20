#include <cstdio>
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <unistd.h>
#include <cerrno>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "XNRW/include/ThreadPool.h"

#define REPLY_LEN 2
// #define DEBUG
#define TIME

#if defined(DEBUG) || defined(TIME)
std::mutex printMtx;
#endif

#ifdef TIME
#include <iomanip>
#include <chrono>
std::chrono::time_point<std::chrono::system_clock> start, end;
long double totalLatency;
std::mutex totalLatencyMutex;
#endif

int main(int argc, char const *argv[]) {
  if (argc < 5) {
    std::cerr << "Usage: client <port> <number of connections> "
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

  XNRW::ThreadPool threadPool(numConn);

  for (auto i = 0ull; i < numConn; i++) {
    threadPool.addTask([port, numMessage, messageSize, i]() {
      int serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      struct sockaddr_in serverAddress;
      memset(&serverAddress, 0, sizeof(serverAddress));
      serverAddress.sin_family = AF_INET;
      serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");
      serverAddress.sin_port = htons(port);
      if (connect(serverSocket, (struct sockaddr *)&serverAddress,
              sizeof(serverAddress)) < 0) {
        std::cerr << "Connection failed." << std::endl;
        perror("Error: ");
        exit(-1);
      }
      setsockopt(serverSocket, SOL_SOCKET, SO_RCVBUF,
                 &messageSize, sizeof(messageSize));
      setsockopt(serverSocket, SOL_SOCKET, SO_SNDBUF,
                 &messageSize, sizeof(messageSize));
      std::unique_ptr<char> data(new char[messageSize]);
      memset(data.get(), '0', messageSize);
      std::unique_ptr<char> buffer(new char[REPLY_LEN + 1]);
      memset(buffer.get(), 0, REPLY_LEN + 1);
      for (auto j = 0ull; j < numMessage; j++) {
#ifdef TIME
        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();
#endif
        if (send(serverSocket, data.get(), messageSize, 0) < 0) {
          std::cerr << "Send failed in connection " << i
                    << " message " << j << std::endl;
          perror("Error: ");
          exit(-1);
        }
        if (recv(serverSocket, buffer.get(), REPLY_LEN, MSG_WAITALL) < 0) {
          std::cerr << "Recv failed in connection " << i
                    << " message " << j << std::endl;
          perror("Error: ");
          exit(-1);
        }
#ifdef TIME
        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        {
          std::lock_guard<std::mutex> l(totalLatencyMutex);
          totalLatency += elapsed_seconds.count();
        }
#endif
#ifdef DEBUG
        {
          std::lock_guard<std::mutex> l(printMtx);
          std::cout << "Received from server: " << buffer << std::endl;
#ifdef TIME
          std::cout << "Connection " << i << ", message " << j
                    <<  " finished. Took: " << std::fixed
                    << elapsed_seconds.count() << "s" << std::endl;
#endif
        }
#endif
      }
      close(serverSocket);
    });
  }

  threadPool.wait();

#ifdef DEBUG
  {
    std::lock_guard<std::mutex> l(printMtx);
    std::cout << "All threads completed." << std::endl;
  }
#endif

#ifdef TIME
  {
    std::lock_guard<std::mutex> l(printMtx);
    std::cout << "Average latency: " << std::fixed
              << totalLatency / static_cast<long double>(numConn * numMessage)
              << "s/message" << std::endl;
    std::cout << "Average bandwidth: " << std::fixed
              << static_cast<long double>(
                numConn * numMessage * messageSize
              ) / (totalLatency * 1024.0l * 1024.0l) << "MB/s" << std::endl;
  }
#endif

  return 0;
}
