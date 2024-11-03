#undef UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <iostream>
#include <algorithm>

#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27015"
#define MAX_THREADS 10

struct ClientMessage {
   SOCKET clientSocket;
   char message[DEFAULT_BUFLEN];
   int messageLength;
};

std::vector<SOCKET> clients;
std::queue<ClientMessage> messageQueue;
std::mutex mtx;
std::condition_variable cv;
bool running = true;

void WorkerThread() {
   while (running) {
      ClientMessage msg;

      {
         std::unique_lock<std::mutex> lock(mtx);
         cv.wait(lock, [] { return !messageQueue.empty() || !running; });

         if (!running) break;

         msg = messageQueue.front();
         messageQueue.pop();
      }

      for (auto& client : clients) {
         if (client != msg.clientSocket) {
            send(client, msg.message, msg.messageLength, 0);
         }
      }
   }
}

void HandleClient(SOCKET ClientSocket) {
   char recvbuf[DEFAULT_BUFLEN];
   int iResult;

   u_long mode = 1; 
   ioctlsocket(ClientSocket, FIONBIO, &mode);

   while (true) {
      iResult = recv(ClientSocket, recvbuf, DEFAULT_BUFLEN, 0);
      if (iResult > 0) {
         std::cout << "Received message: " << recvbuf << std::endl;
         {
            std::lock_guard<std::mutex> lock(mtx);
            messageQueue.push({ ClientSocket, {}, iResult });
            memcpy(messageQueue.back().message, recvbuf, iResult);
         }
         cv.notify_one(); // Уведомление потока-работника
      }
      else if (iResult == 0) {
         std::cout << "Connection closed." << std::endl;
         break;
      }
      else if (WSAGetLastError() == WSAEWOULDBLOCK) {
         std::cout << "avvvv" << std::endl;
         Sleep(1000);
         continue;
      }
      else {
         std::cout << "recv failed with error: " << WSAGetLastError() << std::endl;
         break;
      }
   }

   {
      std::lock_guard<std::mutex> lock(mtx);
      clients.erase(std::remove(clients.begin(), clients.end(), ClientSocket), clients.end());
   }

   closesocket(ClientSocket);
}

int main() {
   WSADATA wsaData;
   int iResult;

   SOCKET ListenSocket = INVALID_SOCKET;

   struct addrinfo* result = NULL;
   struct addrinfo hints;

   iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

   ZeroMemory(&hints, sizeof(hints));
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_protocol = IPPROTO_TCP;
   hints.ai_flags = AI_PASSIVE;

   iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);

   ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);

   iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
   freeaddrinfo(result);

   iResult = listen(ListenSocket, SOMAXCONN);

   std::vector<std::thread> threadPool;
   for (int i = 0; i < MAX_THREADS; ++i) {
      threadPool.emplace_back(WorkerThread);
   }

   while (running) {
      SOCKET ClientSocket = accept(ListenSocket, NULL, NULL);

      if (ClientSocket != INVALID_SOCKET) {
         std::lock_guard<std::mutex> lock(mtx);
         clients.push_back(ClientSocket);

         std::thread(HandleClient, ClientSocket).detach();
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
   }

   {
      std::lock_guard<std::mutex> lock(mtx);
      running = false;
   }
   cv.notify_all(); // Уведомление всех потоков о завершении

   for (auto& thread : threadPool) {
      thread.join();
   }

   closesocket(ListenSocket);
   WSACleanup();

   return 0;
}
