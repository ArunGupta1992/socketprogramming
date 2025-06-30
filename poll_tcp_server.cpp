#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <mutex>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

/* poll() api.
 * it is also an I/O multiplexing mechanism. but it is more scable and flexible
 * alternative to select().
 * poll lets us to monitor many FDs to see if --
 * 1. Data is ready to read.
 * 2. you can write.
 * 3. There is an error or disconnect.
*/

/* int poll(struct pollfd fds[], nfds_t nfds, int timeout);
  | Parameter | Meaning                                                |
  | --------- | ------------------------------------------------------ |
  | `fds[]`   | Array of `pollfd` structs (one per socket)             |
  | `nfds`    | Number of elements in `fds`                            |
  | `timeout` | Milliseconds: `0` = non-blocking, `-1` = block forever |


  struct pollfd {
      int   fd;       // File descriptor to monitor
      short events;   // What to monitor (POLLIN, POLLOUT, etc.)
      short revents;  // What actually happened (set by poll)
  };

  | Flag       | Meaning               |
  | ---------- | --------------------- |
  | `POLLIN`   | Data to read          |
  | `POLLOUT`  | Socket ready to write |
  | `POLLERR`  | Error occurred        |
  | `POLLHUP`  | Hang up (disconnect)  |
  | `POLLNVAL` | Invalid FD            |
*/

using namespace std;

//Client Handler Interface

class IClientHandler
{
  public:
    virtual void on_client_connect(int client_fd) = 0;
    virtual void on_client_data(
      int client_fd, const char* data, ssize_t len) = 0;
    virtual void on_client_disconnect(int client_fd) = 0;
};

class TcpServer
{
  public:
    TcpServer(const int port, std::shared_ptr<IClientHandler> p_handler);
    ~TcpServer();
    void run();

  private:
    int server_fd;
    const int port;
    const std::shared_ptr<IClientHandler> pHandler;
    std::vector<pollfd> fds;
    std::unordered_set<int> new_clients;
    std::unordered_set<int> remove_clients;
  private:
    void setup_socket();
    int accept_new_client();
    int handle_existing_client_read(int client_fd);
    void handle_existing_client_write(int client_fd);
    void close_clients();
    void add_new_clients();
};

TcpServer::TcpServer(const int port, std::shared_ptr<IClientHandler> p_handler):
  port(port), pHandler(p_handler), server_fd(-1)
{
  if(p_handler == nullptr)
  {
    throw std::runtime_error("client handle can not be null");
  }

  setup_socket();
}

TcpServer::~TcpServer()
{
  close(server_fd);
  std::for_each(fds.begin(), fds.end(), [](const pollfd& e){
    close(e.fd);
  });
}


void TcpServer::run()
{
  while (true)
  {
    auto polled_fds = poll(fds.data(), fds.size(), -1);
    if(polled_fds < 0)
    {
      perror("nothing to poll");
      break;
    }

    remove_clients.clear();
    new_clients.clear();

    for(size_t i = 0; i < fds.size(); ++i)
    {
      pollfd& pfd = fds[i];

      if(pfd.revents & POLLNVAL)
      {
        std::cerr << "Invalid socket fd" << endl;
        // why we have not close invalid fd? I mean close(pfd);
        // If an FD is already invalid (e.g. closed elsewhere or not opened properly),
        // trying to close it again can lead to undefined behavior or even close a
        // new FD that reused the same number.

        /* POLLNVAL is set when:
         * 1. The fd is negative.
         * 2. The fd is not open.
         * 3. The fd was already closed, but not removed from the poll array.
        */
      }

      /*
        | Flag      | Meaning                             | When It Happens                               | How to Handle                                       |
        | --------- | ----------------------------------- | --------------------------------------------- | --------------------------------------------------- |
        | `POLLHUP` | **Peer has disconnected (hang up)** | - Remote side **closed** the connection       | - Log it<br>- `close(fd)`                           |
        | `POLLERR` | **A socket-level error occurred**   | - Unexpected socket error (I/O, buffer, etc.) | - Use `getsockopt()` to get reason<br>- `close(fd)` |

      */

      if(pfd.revents & POLLERR)
      {
        /* POLLERR — Error : A low-level socket error occurred
        * 1. Invalid buffer
        * 2. Network failure
        * 3. Connection reset
        * 4. Unrecoverable protocol error
        */

        int err = 0;
        socklen_t len = sizeof(err);
        if(getsockopt(pfd.fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
        {
          std::cerr << "Get socket option for failed";
        }
        else
        {
          std::cerr << "Socket error: " << std::to_string(err) << "\n";
        }
      }

      if(pfd.revents & POLLHUP)
      {
        std::cout << "peer hang up" << endl;
      }

      if(pfd.revents & POLLIN)
      {
        if(pfd.fd == server_fd)
        {
          auto new_fd = accept_new_client();
          if(new_fd >= 0)
          {
            new_clients.insert(new_fd);
          }
        }
        else
        {
          if(handle_existing_client_read(pfd.fd) < 0)
          {
            remove_clients.insert(pfd.fd);
          }
        }
      }

      if(pfd.revents & POLLOUT)
      {
        handle_existing_client_write(pfd.fd);
      }
    }

    close_clients();
    add_new_clients();
  }
  close(server_fd);
  return;
}

void TcpServer::setup_socket()
{
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if(server_fd < 0)
  {
    throw std::runtime_error("Socket Creation failed");
  }

  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_ANY);

  if(bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
  {
    throw std::runtime_error("Socket binding failed");
  }

  if(listen(server_fd, SOMAXCONN) < 0)
  {
    throw std::runtime_error("Socket listening failed");
  }

  fds.push_back({server_fd, POLLIN, 0});

  std::cout << "Tcp Server is ready for Listen on port " << port << endl;
}

int TcpServer::accept_new_client()
{
  auto client_fd  = accept(server_fd, nullptr, nullptr);
  if(client_fd < 0)
  {
    perror("accept");
    return -1;
  }

  // This will cause the problem because it is changing the container while using
  // it.
  // fds.push_back({client_fd, POLLIN, 0});
  pHandler->on_client_connect(client_fd);
  return client_fd;
}

int TcpServer::handle_existing_client_read(int client_fd)
{
  char buffer[1024];
  auto nb = recv(client_fd, buffer, sizeof(buffer), 0);
  if(nb <= 0)
  {
    return -1;
  }

  pHandler->on_client_data(client_fd, buffer, nb);
  return 0;
}

void TcpServer::handle_existing_client_write(int client_fd)
{
  std::cout << "FD " << client_fd << " is ready to write\n";
}

void TcpServer::close_clients()
{
  for(auto it = fds.begin(); it != fds.end();)
  {
    auto find_it = remove_clients.find(it->fd);
    if(find_it != remove_clients.end())
    {
      if(!(it->revents & POLLNVAL))
      {
        close(it->fd);
      }
      pHandler->on_client_disconnect(it->fd);
      it = fds.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

void TcpServer::add_new_clients()
{
  for(const auto fd : new_clients)
  {
    fds.push_back({fd, POLLIN, 0});
  }
}

// Echo chat server
class EchoHandler : public IClientHandler
{
  public:
    void on_client_data(int client_fd, const char* data, ssize_t len) override;
    void on_client_connect(int client_fd) override;
    void on_client_disconnect(int client_fd) override;
};

void EchoHandler::on_client_data(int client_fd, const char* data, ssize_t len)
{
  std::string msg(data, len);
  std::cout << "Client " << client_fd << ": " << msg;
  send(client_fd, data, len, 0);
}

void EchoHandler::on_client_connect(int client_fd)
{
  std::cout << "Client connected: FD = " << client_fd << "\n";
}

void EchoHandler::on_client_disconnect(int client_fd)
{
  std::cout << "Client disconnected: FD = " << client_fd<< "\n";
}

// BroadCast chat handler class
class BroadCastChatHandler: public IClientHandler
{
  public:
    void on_client_data(int client_fd, const char* data, ssize_t len) override;
    void on_client_connect(int client_fd) override;
    void on_client_disconnect(int client_fd) override;

  private:
    std::unordered_set<int> clients;
    std::unordered_map<int, std::string> nick_names;
    std::mutex _mutex;
    void broadcast(int sender_fd, const std::string& msg);
};

void BroadCastChatHandler::on_client_connect(int client_fd)
{
  std::lock_guard<std::mutex> lk(_mutex);
  clients.insert(client_fd);

  const std::string& msg = " Enter your nickname: ";
  //std::cout << msg;

  send(client_fd, msg.c_str(), msg.length(), 0);
}

void BroadCastChatHandler::on_client_data(
  int client_fd, const char* data, ssize_t len)
{
  std::lock_guard<std::mutex> lk(_mutex);
  std::string msg(data, len);

  msg.erase(std::remove(msg.begin(), msg.end(), '\r'), msg.end());
  msg.erase(std::remove(msg.begin(), msg.end(), '\n'), msg.end());

  // check if nickname already set.
  if(nick_names.find(client_fd) == nick_names.end())
  {
    nick_names[client_fd] = msg;
    const std::string& join_msg = msg + " joined the chat\n";
    broadcast(client_fd, join_msg);
    cout << join_msg;
    return;
  }

  // normal message
  const std::string& full_msg = nick_names[client_fd] + ": " + msg + "\n";
  broadcast(client_fd, full_msg);
  std::cout << full_msg;
}

void BroadCastChatHandler::on_client_disconnect(int client_fd)
{
  std::lock_guard<std::mutex> lk(_mutex);

  const std::string& name =
    nick_names.count(client_fd) ? nick_names[client_fd] :
      "Client " + std::to_string(client_fd);

  clients.erase(client_fd);
  nick_names.erase(client_fd);

  std::string msg = name + " left the chat\n";
  broadcast(client_fd, msg);
  cout << msg;
}

void BroadCastChatHandler::broadcast(int sender_fd, const std::string &msg)
{
  for(auto const fd: clients)
  {
    if(fd != sender_fd)
    {
      send(fd, msg.c_str(), msg.length(), 0);
    }
  }
}

int main()
{
/*
 try
  {
    std::shared_ptr<EchoHandler> p_handler =
      std::make_shared<EchoHandler>();
    TcpServer server(9000, p_handler);
    server.run();
  }
  catch(const std::exception& e)
  {
    std::cerr << "Server error: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
*/

  try
  {
    std::shared_ptr<BroadCastChatHandler> p_handler =
      std::make_shared<BroadCastChatHandler>();
    TcpServer server(9000, p_handler);
    server.run();
  }
  catch(const std::exception& e)
  {
    std::cerr << "Server error: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return 0;
}
