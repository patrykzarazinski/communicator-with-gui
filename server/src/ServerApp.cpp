#include "ServerApp.hpp"

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>  // close, read

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "Epoll.hpp"
#include "network/ListenSocket.hpp"
#include "utils/ErrorHandler.hpp"

static bool isRunning = false;

namespace {
void clearBuffer(std::string& buffer) {
  // TODO Using std::string::clear or std::string::erase lead to infinity loop
  // probably due to invalidate internal iterators, references, pointers
  for (auto& item : buffer) {
    item = '\0';
  }
}

}  // namespace
namespace app {
Server::Server()
    : socket{std::make_unique<network::ListenSocket>()},
      epoll{std::make_unique<Epoll>()} {}

void Server::run(types::IP ip, types::Port port) {
  socket->createSocket(ip, port);
  epoll->createEpoll();

  epoll->registerSocket(socket->getFD());
  receiveLoop();

  return;
}

void Server::receiveLoop() {
  const int MAX_EVENTS = 10;
  std::vector<epoll_event> events(MAX_EVENTS);

  isRunning = true;
  while (isRunning) {
    int nfds = epoll_wait(epoll->getEpoll(), events.data(), MAX_EVENTS, -1);

    if (nfds == -1) {
      utils::ErrorHandler::handleError("epoll_wait() failed");
    }

    for (int i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;
      if (fd == socket->getFD()) {
        handleNewConnection();
      } else {
        handleClient(fd);
      }
    }
  }
}

void Server::handleNewConnection() {
  sockaddr_in clientAdress{};
  socklen_t clientAddressLen = sizeof(clientAdress);

  int clientSocket =
      accept4(socket->getFD(), reinterpret_cast<sockaddr*>(&clientAdress),
              &clientAddressLen, SOCK_NONBLOCK);

  if (clientSocket == -1) {  // add EAGAIN or EWOULDBLOCK handling
    utils::ErrorHandler::handleError("accept4() failed");
  }

  clientsSocket.push_back(clientSocket);
  epoll->registerSocket(clientSocket);
}

void Server::handleClient(types::FD clientSocket) {
  std::vector<char> buffer(1024, '\0');
  ssize_t readBytes =
      recv(clientSocket, buffer.data(), buffer.size(), MSG_DONTWAIT);

  if (readBytes == -1) {
    if (readBytes == EAGAIN or readBytes == EWOULDBLOCK) {
      std::cout << "No messages are available at the socket" << std::endl;
      return;
    } else {
      close(clientSocket);
      utils::ErrorHandler::handleError("recv() failed");
    }
  } else if (readBytes == 0) {
    std::cout << "Connection closed by peer" << std::endl;
    return;
  } else {
    broadcast(buffer, clientSocket);
  }
}

void Server::broadcast(std::vector<char> buffer, types::FD clientSocket) {
  for (const types::FD& fd : clientsSocket) {
    if (fd != clientSocket) {
      send(fd, buffer.data(), buffer.size(), 0);
    }
  }
}

Server::~Server() { std::cout << "Server shutdown" << std::endl; };
}  // namespace app