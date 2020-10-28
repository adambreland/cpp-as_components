// // 20201027
// // Example code for interposing on client or server output in a test.
// // Google Test assertions are used when execution is still in the parent
// // process as it is assumed that the parent is the test program.

// // INTERPOSE TEST
// auto MakeNonblocking = [](int descriptor)->bool
// {
//   int dummy_flags {fcntl(descriptor, F_GETFL)};
//   if(dummy_flags == -1)
//     return false;
//   dummy_flags |= O_NONBLOCK;
//   if(fcntl(descriptor, F_SETFL, dummy_flags) == -1)
//     return false;
//   return true;
// };
// // The last connection is an AF_UNIX connection.
// int last_connection {(--connection_map.end())->first};
// // An AF_UNIX listening socket is created an a connection is made to it.
// // The descriptor with the value of last_connection will be made to refer to
// // the interposing connected socket.
// int last_connection_dup {dup(last_connection)};
// ASSERT_NE(last_connection_dup, -1) << std::strerror(errno);
// int interposing_server {socket(AF_UNIX, SOCK_STREAM, 0)};
// ASSERT_NE(interposing_server, -1) << std::strerror(errno);
// struct sockaddr_un interposing_server_addr {};
// interposing_server_addr.sun_family = AF_UNIX;
// std::strcpy(interposing_server_addr.sun_path,
//   "/tmp/ListeningInterposingServer");
// struct sockaddr* cast_server_addr_ptr {static_cast<struct sockaddr*>(
//   static_cast<void*>(&interposing_server_addr))};
// ASSERT_NE(bind(interposing_server, cast_server_addr_ptr,
//   sizeof(struct sockaddr_un)), -1) << std::strerror(errno);
// ASSERT_NE(listen(interposing_server, 5), -1) << std::strerror(errno);
// int client_dummy_connection {socket(AF_UNIX, SOCK_STREAM, 0)};
// ASSERT_NE(client_dummy_connection, -1) << std::strerror(errno);
// ASSERT_NE(connect(client_dummy_connection, cast_server_addr_ptr,
//   sizeof(struct sockaddr_un)), -1) << std::strerror(errno);
// ASSERT_TRUE(MakeNonblocking(client_dummy_connection)) << std::strerror(errno);
// ASSERT_NE(dup2(client_dummy_connection, last_connection), -1) <<
//   std::strerror(errno);
// close(client_dummy_connection);
// pid_t fork_return {fork()};
// ASSERT_NE(fork_return, -1); // If this fails, we are in the parent.
// if(fork_return == 0) // Child
// {
//   ChildServerAlrmRestoreAndSelfKillSet();
//   // Create the temporary external file to write to.
//   int external_file {open("/tmp/TestFcgiClientInterfaceTestDataOutput",
//     O_WRONLY | O_CREAT | O_TRUNC)};
//   if(external_file == -1)
//   {
//     _exit(EXIT_FAILURE);
//   }
//   int connected_interposing_descriptor {accept(interposing_server, nullptr,
//     nullptr)};
//   if(connected_interposing_descriptor == -1)
//   {
//     _exit(EXIT_FAILURE);
//   }
//   if(!MakeNonblocking(connected_interposing_descriptor))
//   {
//     _exit(EXIT_FAILURE);
//   }
//   int max_for_select {std::max<int>(connected_interposing_descriptor,
//       last_connection_dup) + 1};
//   fd_set read_set  {};
//   constexpr int buffer_size {1 << 8};
//   std::uint8_t read_buffer[buffer_size];
//   auto ReadAndWrite = [&read_buffer, buffer_size]
//   (
//     int  read_from,
//     int  interposed_write_descriptor,
//     bool external_write,
//     int  external_write_descriptor
//   )->void
//   {
//     while(true)
//     {
//       std::size_t read_return {socket_functions::SocketRead(
//         read_from, read_buffer, buffer_size)};
//       int saved_errno {errno};
//       if(read_return > 0)
//       {
//         if(external_write)
//         {
//           ssize_t external_write_return {write(external_write_descriptor,
//           read_buffer, read_return)};
//           if(external_write_return < read_return)
//           {
//             _exit(EXIT_FAILURE);
//           }
//         }
//         std::size_t write_return {socket_functions::WriteOnSelect(
//           interposed_write_descriptor, read_buffer, read_return)};
//         if(write_return < read_return)
//         {
//           if(errno == EPIPE)
//           {
//             _exit(EXIT_SUCCESS);
//           }
//           else
//           {
//             _exit(EXIT_FAILURE);
//           }
//         }
//       }
//       if(read_return < buffer_size)
//       {
//         if((saved_errno == EWOULDBLOCK) || (saved_errno == EAGAIN))
//         {
//           break;
//         }
//         else if(saved_errno == 0)
//         {
//           _exit(EXIT_SUCCESS);
//         }
//         else
//         {
//           _exit(EXIT_FAILURE);
//         }
//       }
//     }
//   };
//   while(true)
//   {
//     FD_ZERO(&read_set);
//     FD_SET(connected_interposing_descriptor, &read_set);
//     FD_SET(last_connection_dup, &read_set);
//     int select_return {select(max_for_select, &read_set, nullptr, nullptr,
//       nullptr)};
//     if(select_return == -1)
//     {
//       _exit(EXIT_FAILURE);
//     }
//     if(FD_ISSET(connected_interposing_descriptor, &read_set))
//     {
//       ReadAndWrite(connected_interposing_descriptor, last_connection_dup,
//         true, external_file);
//     }
//     if(FD_ISSET(last_connection_dup, &read_set))
//     {
//       ReadAndWrite(last_connection_dup, connected_interposing_descriptor,
//         false, -1);
//     }
//   } // It is intended that this while(true) loop will never be exited here.
//   _exit(EXIT_FAILURE);
// }
// // else, in parent
// close(last_connection_dup);
// close(interposing_server);
// // INTERPOSE TEST
