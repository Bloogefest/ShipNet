#ifdef __linux__
#include "../../utils/exception/ErrnoException.hpp"
#include "Listener.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace Ship {
  thread_local char* listenerErrorBuffer = new char[64];

  EpollListener::EpollListener(EpollEventLoop* event_loop, int max_events, int timeout) : eventLoop(event_loop), maxEvents(max_events), timeout(timeout) {
  }

  EpollListener::~EpollListener() {
    close(epollFileDescriptor);
    close(socketFileDescriptor);
    delete eventLoop;
  }

  void EpollListener::StartListening(SocketAddress address) {
    socketFileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFileDescriptor == -1) {
      throw Exception("Error while creating socket. No permissions?");
    }

    int fionbioValue = true;
    if (ioctl(socketFileDescriptor, FIONBIO, &fionbioValue) == -1) {
      throw ErrnoException(listenerErrorBuffer, 64);
    }

    sockaddr_in bindAddress {};
    bindAddress.sin_family = AF_INET;
    bindAddress.sin_port = htons(address.GetPort());
    bindAddress.sin_addr.s_addr = inet_addr(address.GetHostname().c_str());

    if (bind(socketFileDescriptor, (sockaddr*) &bindAddress, sizeof(sockaddr_in)) == -1) {
      throw ErrnoException(listenerErrorBuffer, 64);
    }

    if (listen(socketFileDescriptor, SOMAXCONN) == -1) {
      throw ErrnoException(listenerErrorBuffer, 64);
    }

    epollFileDescriptor = epoll_create1(O_CLOEXEC);

    if (epollFileDescriptor == -1) {
      throw ErrnoException(listenerErrorBuffer, 64);
    }

    epoll_event ctlEvent {};
    ctlEvent.data.fd = socketFileDescriptor;
    ctlEvent.events = EPOLLIN | EPOLLET;

    if (epoll_ctl(epollFileDescriptor, EPOLL_CTL_ADD, socketFileDescriptor, &ctlEvent) == -1) {
      throw ErrnoException(listenerErrorBuffer, 64);
    }

    epoll_event events[maxEvents];
    epoll_event event; // NOLINT(cppcoreguidelines-pro-type-member-init)
    while (true) {
      int amount = epoll_wait(epollFileDescriptor, events, maxEvents, timeout);

      for (int i = 0; i < amount; ++i) {
        event = events[i];
        try {
          while (true) {
            if (!(event.events & EPOLLIN) || (event.events & EPOLLERR) || (event.events & EPOLLHUP)) {
              throw ErrnoException(listenerErrorBuffer, 64);
            } else {
              sockaddr connectionAddress {};
              socklen_t length = sizeof(sockaddr);

              int receivedFileDescriptor = accept4(socketFileDescriptor, &connectionAddress, &length, SOCK_NONBLOCK | SOCK_CLOEXEC);
              if (receivedFileDescriptor == -1) {
                if (errno != EAGAIN) {
                  throw ErrnoException(listenerErrorBuffer, 64);
                }

                break;
              }

              eventLoop->Accept(receivedFileDescriptor);
            }
          }
        } catch (std::exception& e) {
          close(event.data.fd);
          close(epollFileDescriptor);
          throw;
        }
      }
    }
  }
}
#endif