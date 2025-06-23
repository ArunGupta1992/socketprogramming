#include<iostream>
#include<unistd.h>
#include<string>
#include<sys/socket.h>
#include<netinet/in.h>

using namespace std;

int main()
{
  int server_fd, client_fd;
  struct sockaddr_in address;
  int addrlen = sizeof(&address);
  char buff[1024] = {0};
  const std::string& response = "Hello from the server!";

  // step 1: Create a socker
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if(server_fd < 0)
  {
    std::cerr << "socket creation failed" << endl;
    return 1;
  }

  cout << "socket creation is successful" << endl;

  // step 2: bind socket to an IP/port address.
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY; // 0.0.0.0
  address.sin_port = htons(8080);

  if(bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0)
  {
    std::cerr << "binding Ip/port to socket failed" << endl;
    return 1;
  }

  cout << "binding a socket to ip/port successful" << endl;

  // step 3: Listen for connections
  if(listen(server_fd, 5) < 0)
  {
    std::cerr << "Listening on server socket failed" << endl;
    return 1;
  }

  cout << "Server listening on port 8080...\n";

  // step 4: Accept a connection
  client_fd = accept(server_fd, (sockaddr*)&address, (socklen_t*)&addrlen);
  if(client_fd < 0)
  {
    std::cerr << "Did not accept client" << endl;
    return 1;
  }

  cout << "Client connected!\n";

  // step 5: read message from the client.
  int bytes_read = read(client_fd, buff, sizeof(buff));
  cout << "Client says: " << buff << "\n";

  // step 6: send response to client.
  auto bytes_send = send(client_fd, response.c_str(), response.length(), 0);
  cout << "Response sent to client " << bytes_send << endl;


  // close sockets.
  close(server_fd);
  close(client_fd);

  return 0;
}
