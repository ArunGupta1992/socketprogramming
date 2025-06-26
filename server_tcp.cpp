#include<iostream>
#include<unistd.h>
#include<string>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<chrono>
#include<thread>

/* [ Client #1 ]                  [ Client #2 ]
(IP: 10.0.0.2:54321)           (IP: 10.0.0.3:54322)
       |                             |
       |   TCP SYN (connect)         |
       |---------------------------->|
       |                             |
       |                             |
[ OS: Connection Queue ]             |
       |                             |
(server_ip: 10.0.0.1:8080) <------------|
       |
       V
┌──────────────────────────┐
│   Server Socket (FD=3)   │ ← Created by `socket()`
│    Bound + Listening     │ ← Bound by `bind()`, starts `listen()`
└──────────────────────────┘
       |
       |  [Pending Connection Queue]
       |    ┌────────────┐
       |    │ Client #1  │
       |    │ Client #2  │
       |    └────────────┘
       |
Server calls `accept()`
       ↓
┌────────────────────────────┐
│ New Socket (FD=4)          │ ← For Client #1
│  (10.0.0.1:8080 ↔ 10.0.0.2:54321) │
└────────────────────────────┘

Next accept():
┌────────────────────────────┐
│ New Socket (FD=5)          │ ← For Client #2
│  (10.0.0.1:8080 ↔ 10.0.0.3:54322) │
└────────────────────────────┘ */


/* TCP Server side FSM state.
 * socket() + bind() + listen()  → LISTEN
 * accept()                      → SYN_RECEIVED → ESTABLISHED
 * client closes                 → CLOSE_WAIT → LAST_ACK → CLOSED
*/

/* Visual Diagram
  CLIENT                       SERVER
  -------                     --------
  SYN_SENT     --->          SYN_RECEIVED
              <---           SYN+ACK
  ESTABLISHED  --->          ESTABLISHED
  FIN_WAIT_1   --->          CLOSE_WAIT
  FIN_WAIT_2   <---          LAST_ACK
  TIME_WAIT    --->          CLOSED
  CLOSED       <---
*/



using namespace std;

void print_socket_info(int sock, const std::string& label)
{
  struct sockaddr_in local, remote;
  socklen_t len = sizeof(struct sockaddr_in);

  getsockname(sock, (sockaddr*)&local, &len);
  getpeername(sock, (sockaddr*)&remote, &len);

  char local_ip[INET_ADDRSTRLEN];
  char remote_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &local.sin_addr, local_ip, INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &remote.sin_addr, remote_ip, INET_ADDRSTRLEN);

  cout << label << ":\n";
  cout << "  Local  -> " << local_ip << ":" << ntohs(local.sin_port) << "\n";
  cout << "  Remote -> " << remote_ip << ":" << ntohs(remote.sin_port) << "\n";
}

void print_socket_option(int sock)
{
  // check SO_REUSEADDR
  int reuse_val;
  socklen_t oplen = sizeof(reuse_val);
  if(getsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_val, &oplen) < 0)
  {
    std::cerr << "failed to get SO_REUSEADDR option\n";
  }
  else
  {
    cout << "SO_REUSEADDR " << (reuse_val ? "enable" : "disable") << endl;
  }

  // check SO_RCVBUF
  int rcvbuf_val;
  oplen = sizeof(rcvbuf_val);
  if(getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf_val, &oplen) < 0)
  {
    std::cerr << "failed to get SO_RCVBUF option\n";
  }
  else
  {
    cout << "SO_RCVBUF: " << rcvbuf_val << " bytes" << endl;
  }

  // check SO_RCVTIMEO
  struct timeval timeout_val = {};
  oplen = sizeof(&timeout_val);
  if(getsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout_val, &oplen) < 0)
  {
    std::cerr << "failed to get SO_RCVTIMEO option\n";
  }
  else
  {
    cout << "SO_RCVTIMEO: " << timeout_val.tv_sec << " sec ";
    cout << timeout_val.tv_usec << " usec\n";
  }
}

int set_socket_option(int sock)
{
  // Allows reusing a local address (IP+port), helpful when restarting servers.
  int yes = 1;
  if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
  {
    std::cerr << "setsockopt(SO_REUSEADDR) failed" << endl;
    return -1;
  }

  // Set Receive Timeout (10 second)
  struct timeval timeout = {};
  timeout.tv_sec = 10;
  timeout.tv_usec = 0;
  if(setsockopt(
    sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
  {
    std::cerr << "setsockopt(SO_RCVTIMEO) failed" << endl;
    return -1;
  }

  // Increase receive buffer size
  int recvbuf = 65536;
  if(
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recvbuf, sizeof(recvbuf)) < 0)
  {
    std::cerr << "setsockopt(SO_RCVBUF) failed" << endl;
    return -1;
  }

  return 0;
}

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

  if(set_socket_option(server_fd) < 0)
  {
    std::cerr << "socket option set failed\n";
    return -1;
  }

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
  print_socket_info(client_fd, "New socket Created");

  // This sleep stop the server from reading the data send from the client.
  // This request or data will be buffered at Recv-Q (receive queue of the socket)
  // each socket is associated with two Queues: 1. Send-Q and Recv-Q.
  // you can see this with command ss -ntp
  //this_thread::sleep_for(std::chrono::seconds(10));

  // step 5: read message from the client.
  int bytes_read = read(client_fd, buff, sizeof(buff));
  cout << "Client says: " << buff << "\n";

  // step 6: send response to client.
  auto bytes_send = send(client_fd, response.c_str(), response.length(), 0);
  cout << "Response sent to client " << bytes_send << endl;

  print_socket_option(server_fd);

  // close sockets.
  close(server_fd);
  close(client_fd);

  return 0;
}

// accept()/read() function is a blocking call to kernel.
// SOCK_STREAM TCP socket type
// SOCK_DGRAM UDP socket type

/*
 * Q1. Can we use same IP/PORT for different sockets?
 * A1. yes, for each incoming connection from the clients, tcp server creates
 *  a seperate socket after accept function call.
 *  And kernel identifies each server-client connection with following tuple
 *  <local-ip, local-port, client-ip, client-port> because tcp is connection
 *  oriented protocol.
*/


/*
| State             | Meaning                                                      |
| ----------------- | ------------------------------------------------------------ |
| **CLOSED**        | Initial state, no socket created or connection active        |
| **LISTEN**        | Server is waiting for incoming connections via `listen()`    |
| **SYN\_SENT**     | Client sent a SYN, waiting for SYN+ACK from server           |
| **SYN\_RECEIVED** | Server received SYN, replied with SYN+ACK, waiting for ACK   |
| **ESTABLISHED**   | Connection is fully open; both sides can read/write          |
| **FIN\_WAIT\_1**  | One side (who called `close()`) sent FIN, waiting for ACK    |
| **FIN\_WAIT\_2**  | Got ACK for FIN; waiting for peer’s FIN                      |
| **CLOSE\_WAIT**   | Received peer’s FIN; waiting for local app to call `close()` |
| **LAST\_ACK**     | Sent FIN after receiving one; waiting for final ACK          |
| **TIME\_WAIT**    | Waiting to ensure old packets are flushed from network       |
| **CLOSING**       | Both sides sent FIN before ACKs; rare case                   |
*/

/* socket options.
  1. SO_REUSEADDR
    Level: SOL_SOCKET
    Purpose: Allows a socket to bind to a port even if a previous connection is in TIME_WAIT.
    Notes: Commonly used in servers to allow quick restarts.
  2. SO_REUSEPORT
    Level: SOL_SOCKET
    Purpose: Allows multiple sockets to bind to the same IP and port.
    Notes: Linux only. Useful for load balancing across processes.
  3. SO_RCVBUF
    Level: SOL_SOCKET
    Purpose: Sets the size of the receive buffer for the socket.
    Notes: Linux doubles the value internally. Use getsockopt() to verify.
  4. SO_SNDBUF
    Level: SOL_SOCKET
    Purpose: Sets the size of the send buffer for the socket.
    Notes: Like SO_RCVBUF, doubled internally on Linux.
  5. SO_RCVTIMEO / SO_SNDTIMEO
    Level: SOL_SOCKET
    Purpose: Sets a timeout for blocking receive/send operations.
    Notes: Defined as struct timeval. Prevents indefinite blocking.
  6. TCP_NODELAY
    Level: IPPROTO_TCP
    Purpose: Disables Nagle's algorithm for low-latency communication.
    Notes: Sends small packets immediately without waiting for more data.
  7. SO_LINGER
    Level: SOL_SOCKET
    Purpose: Controls the socket's behavior when closing if unsent data remains.
    Notes: Can force close or wait depending on linger settings.
*/
