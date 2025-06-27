#include <iostream>
#include <unordered_set>
#include <stdexcept>
#include <algorithm>
#include <string>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>


using namespace std;

/* select():
 * select() allows a program to monitor multiples FDs(ex: sockets) to see which
 * ones are ready for --
 * 1. Reading
 * 2. Writing
 * 3. Exceptions
*/

/* Design Goals: Design the TcpServer class using clean object-oriented design
 * principles and apply OOP patterns where appropriate —
 * making it more extensible, testable, and production-ready.
 *
  | Goal                         | Solution / Pattern                       |
  | ---------------------------- | ---------------------------------------- |
  | Clear separation of concerns | Split socket logic, client logic         |
  | Allow extension              | Use virtual functions (Template Method)  |
  | RAII for resource safety     | Use constructors/destructors             |
  | Decouple data handling       | Use a handler strategy                   |
  | Prevent misuse               | Encapsulation (e.g., private FD members) |
 *
 *
 * OOP Patterns:
  | Pattern                 | How We Use It                                         |
  | ----------------------- | ----------------------------------------------------- |
  | **Template Method**     | Base class defines main loop; subclass handles client |
  | **Strategy (optional)** | Plug in different message handlers                    |
  | **RAII**                | Sockets closed in destructors                         |
  | **Encapsulation**       | Hide socket FD and state                              |

*/

class TcpServer
{
  public:
    explicit TcpServer(int port);
    virtual ~TcpServer();
    void run();

  protected:
    virtual void on_client_data(int client_fd, const char* data, ssize_t len);
    virtual void on_client_connect(int client_fd);
    virtual void on_client_disconnect(int client_fd);

  private:
    int server_fd;
    int max_fd;
    int port;
    fd_set master_set;
    unordered_set<int> client_fds;

  private:
    void setup_socket();
    void accept_new_client();
    void handle_existing_client(int client_fd);
    void close_client(int client_fd);
};

TcpServer::TcpServer(int port) : port(port), server_fd(-1), max_fd(0)
{
  setup_socket();
}

TcpServer::~TcpServer()
{
  close(server_fd);
  for(auto const fd : client_fds)
  {
    close(fd);
  }
}

void TcpServer::run()
{
  while (true)
  {
    fd_set read_set = master_set;
    int nfreadyfds = select(max_fd + 1, &read_set, NULL, NULL, NULL);
    if(nfreadyfds < 0)
    {
      perror("select");
      break;
    }

    for(int fd = 0 ; fd <= max_fd; ++fd)
    {
      if(FD_ISSET(fd, &read_set))
      {
        if(fd == server_fd)
        {
          accept_new_client();
        }
        else
        {
          handle_existing_client(fd);
        }
      }
    }
  }
}

void TcpServer::on_client_data(int client_fd, const char *data, ssize_t len)
{
  auto nfbytes = send(client_fd, data, len, 0);
  if(nfbytes <= 0)
  {
    perror("send");
  }
}

void TcpServer::on_client_connect(int client_fd)
{
  std::cout << "Client connected: FD=" << client_fd << "\n";
}

void TcpServer::on_client_disconnect(int client_fd)
{
  std::cout << "Client disconnected: FD=" << client_fd << "\n";
}

void TcpServer::setup_socket()
{
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if(server_fd < 0)
  {
    throw std::runtime_error("Socket creation failed");
  }

  int reuse = 1;
  if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
  {
    throw std::runtime_error("setsockopt failed");
  }

  sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = INADDR_ANY;

  if(bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
  {
    throw std::runtime_error("Bind failed");
  }

  if(listen(server_fd, SOMAXCONN) < 0)
  {
    throw std::runtime_error("Listen failed");
  }

  FD_ZERO(&master_set);
  FD_SET(server_fd, &master_set);
  max_fd = server_fd;

  std::cout << "Listening on port " << port << "\n";
}

void TcpServer::accept_new_client()
{
  int client_fd = accept(server_fd, nullptr, nullptr);
  if(client_fd < 0)
  {
    perror("accept");
    return;
  }

  FD_SET(client_fd, &master_set);
  client_fds.insert(client_fd);

  if(client_fd > max_fd)
  {
    max_fd = client_fd;
  }

  on_client_connect(client_fd);
}

void TcpServer::handle_existing_client(int client_fd)
{
  char buffer[1024];
  auto nfbytes = recv(client_fd, buffer, sizeof(buffer), 0);
  if(nfbytes <= 0)
  {
    on_client_connect(client_fd);
    close_client(client_fd);
  }
  else
  {
    on_client_data(client_fd, buffer, nfbytes);
  }
}

void TcpServer::close_client(int client_fd)
{
  close(client_fd);
  FD_CLR(client_fd, &master_set);
  client_fds.erase(client_fd);
  on_client_disconnect(client_fd);
}

class EchoServer : public TcpServer
{
  public:
  /* It tells the compiler to inherit all constructors from the
  base class TcpServer into the derived class EchoServer.
  Without this line, even though EchoServer inherits from TcpServer,
  its constructors are not automatically inherited — meaning you'd have
  to manually define constructors in EchoServer that call the corresponding
  TcpServer constructors. */
    using TcpServer::TcpServer;

  protected:
    void on_client_data(int client_fd, const char* data, ssize_t len) override;
};

void EchoServer::on_client_data(int client_fd, const char* data, ssize_t len)
{
  std::string msg(data, len);
  std::cout << "Client " << client_fd << ": " << msg;
  TcpServer::on_client_data(client_fd, data, len);
}

int main()
{
  try
  {
    EchoServer server(9000);
    server.run();
  }
  catch(const std::exception& e)
  {
    std::cerr << "Server error: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
