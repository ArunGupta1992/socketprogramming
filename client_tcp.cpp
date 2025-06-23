#include<iostream>
#include<unistd.h>
#include<sys/socket.h>
#include<string>
#include<arpa/inet.h>

using namespace std;

int main()
{
  int sock = 0;
  struct sockaddr_in server_address;
  const std::string& message = "Hello from the client";
  char buffer[1024] = {0};

  // step 1: create a client socket.
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
  {
    std::cerr << "Socket creation error\n";
    return 1;
  }

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(8080);

  if(inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr) <= 0)
  {
    std::cerr << "Invalid address / Address not supported\n";
    return 1;
  }

  // step 3: connect to server
  if(connect(sock, (sockaddr*)&server_address, sizeof(server_address)) < 0)
  {
    std::cerr << "Connection Failed\n";
    return 1;
  }

  cout << "connected to server\n";

  // step 4 : send message
  auto no_of_btyes_sent = send(sock, message.c_str(), message.length(), 0);
  cout << "Message send to the server " << no_of_btyes_sent << endl;

  // step 5: read response
  auto bytes_read = read(sock, buffer, sizeof(buffer));
  cout << "Server says: " << buffer << "\n";

  // Step 6: Close socket
  close(sock);

  return 0;
}
