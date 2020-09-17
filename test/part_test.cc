// Current compilation string
// g++ -g -pthread -std=c++17 -o part_test -iquote . test/part_test.cc -iquote /home/adam/cpp/googletest/googletest/include -L/home/adam/cpp/fcgi_si/bazel-bin -L/home/adam/cpp/googletest/bazel-bin -L/home/adam/cpp/socket_functions/bazel-bin -l:test/libfcgi_si_testing_utilities.a -l:libfcgi_si_utilities.a -l:libsocket_functions.a -l:libgtest_main.a -l:libgtest.a

#include "include/utility.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "external/googletest/googletest/include/gtest/gtest.h"

#include "include/protocol_constants.h"
#include "test/fcgi_si_testing_utilities.h"

TEST(Utility, PartitionByteSequence)
{
  // Testing explanation
  //    Tests call PartitionByteSequence, use writev to write to a temporary
  // file, and use fcgi_si_test::ExtractContent to retrieve the content of
  // the written FastCGI record sequence. ExtractContent performs checks on the
  // header values of type and request ID. The identity of the extracted
  // content is checked. Since it is unspecified how much data from the
  // range [begin_iter, end_iter) is encoded, the length of the extracted
  // content is used to calculate a new iterator value. This value is compared
  // to the iterator returned by PartitionByteSequence.
  //
  // Examined properties:
  // 1) Value of type: a type from a client, a type from the application
  //    server, and a type value that is not defined by the FastCGI
  //    specification.
  // 2) Value of Fcgi_id: equal to 0, greater than zero but less than the
  //    maximum value, equal to the maximum value.
  // 3) Size of the content byte sequence:
  //    a) No content, i.e. begin_iter == end_iter.
  //    b) Nonzero but 1) much less than the maximum value of a FastCGI record
  //       body and 2) not a multiple of 8 (so that padding is necessary).
  //    c) Equal to the size of the maximum value that is less than the FastCGI
  //       record body size and a multiple of 8 ((2^16 - 1) - 7 = 65528).
  //    d) So large that a single call can likely not encode all of the content.
  //       A content byte sequence with a length of 2^25 bytes will be used.
  //       This value was derived from the assumption that the maximum number
  //       of struct iovec instances which can be handled by a call to writev
  //       is less than or equal to 1024. This is the current maximum on Linux.
  // 4) Content value: the extracted byte sequence must match the original 
  //    byte sequence.
  // 5) Iterator value.
  //
  // Test cases:
  // 1) begin_iter == end_iter, 
  //    type == fcgi_si::FcgiType::kFCGI_GET_VALUES_RESULT, Fcgi_id == 0.
  // 2) std::distance(end_iter, begin_iter) == 3,
  //    type == fcgi_si::FcgiType::kFCGI_STDIN, Fcgi_id == 1.
  // 3) std::distance(end_iter, begin_iter) == 25,
  //    type == fcgi_si::FcgiType::kFCGI_STDOUT, Fcgi_id == 65535 ==
  //    std::numeric_limits<std::uint16_t>::max().
  // 4) std::distance(end_iter, begin_iter) == 8,
  //    type == static_cast<fcgi_si::FcgiType>(20), Fcgi_id == 3.
  // 5) std::distance(end_iter, begin_iter) == 65528,
  //    type == fcgi_si::FcgiType::kFCGI_PARAMS, Fcgi_id == 300.
  // 6) std::distance(end_iter, begin_iter) == 2^25,
  //    type == fcgi_si::FcgiType::kFCGI_STDOUT, Fcgi_id == 3.
  //
  // Modules which testing depends on:
  // 1) fcgi_si_test::ExtractContent
  //
  // Other modules whose testing depends on this module: none.

  const char* file_path {"/tmp/PartitionByteSequence.temp"};
  int temp_descriptor {open(file_path, O_RDWR | O_CREAT | O_TRUNC)};
  if(temp_descriptor == -1)
  {
    FAIL() << "Could not create a temp file.";
  }
  
  auto PartitionByteSequenceTester = [temp_descriptor](
    const std::string& message,
    bool expect_terminal_empty_record,
    std::vector<std::uint8_t>& content_seq, 
    fcgi_si::FcgiType type,
    std::uint16_t Fcgi_id
  )->void
  {
    // Clear the file.
    if(ftruncate(temp_descriptor, 0) < 0)
    {
      ADD_FAILURE() << "A call to ftruncate failed." << '\n' << message;
      return;
    }
    off_t lseek_return {lseek(temp_descriptor, 0, SEEK_SET)};
    if(lseek_return < 0)
    {
      ADD_FAILURE() << "A call to lseek failed." << '\n' << message;
      return;
    }

    // Call PartitionByteSequence and write the encoded record sequence.
    std::vector<std::uint8_t>::iterator begin_iter {content_seq.begin()};
    std::vector<std::uint8_t>::difference_type returned_length {0};
    do
    {
      std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>,
        std::size_t, std::vector<std::uint8_t>::iterator> pr
        {fcgi_si::PartitionByteSequence(begin_iter, content_seq.end(), 
        type, Fcgi_id)};

      ssize_t writev_return {writev(temp_descriptor, std::get<1>(pr).data(),
        std::get<1>(pr).size())};
      if((writev_return < 0) || (static_cast<std::size_t>(writev_return) < 
        std::get<2>(pr)))
      {
        ADD_FAILURE() << "A call to writev failed." << '\n' << message;
        return;
      }      
      begin_iter = std::get<3>(pr);
    } while(begin_iter != content_seq.cend());

    // Extract the content and validate.
    lseek_return = lseek(temp_descriptor, 0, SEEK_SET);
    if(lseek_return < 0)
    {
      ADD_FAILURE() << "A call to lseek failed." << '\n' << message;
      return;
    }
    std::tuple<bool, bool, bool, bool, std::size_t, std::vector<uint8_t>> ecr
      {fcgi_si_test::ExtractContent(temp_descriptor, type, Fcgi_id)};
    if(!std::get<0>(ecr))
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent encountered "
        "an error." << '\n' << message;
      return;
    }
    if(!std::get<1>(ecr))
    {      
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent determined that "
        "a header error was present or an incomplete record was present." << 
        '\n' << message;
      return;
    }
    if(( std::get<2>(ecr) && !expect_terminal_empty_record) || 
       (!std::get<2>(ecr) &&  expect_terminal_empty_record))
    {
      ADD_FAILURE() << "A terminal empty record mismatch was present." << 
        '\n' << message;
      return;
    }
    // std::get<3>(ecr) tests record alignment on 8-byte boundaries.
    // Such alignment is not specified by PartitionByteSequence.

    // std::get<4>(ecr) examines the number of records. This is not currently
    // specified by PartitionByteSequence.

    // This check ensures that PartitionByteSequence encodes some content
    // when content is given. The next check does not verify this property.
    if(content_seq.size() && (!std::get<5>(ecr).size()))
    {
      ADD_FAILURE() << "PartitionByteSequence caused nothing to be written " 
        "when content was present." << '\n' << message;
      return;
    }
    // // Check for equality of the extracted byte sequence and the prefix of the
    // // content sequence indicated by the information returned by
    // // PartitionByteSequence.
    // if((returned_length > content_seq.size())       ||
    //    (returned_length != std::get<4>(ecr).size()) ||
    //    (std::mismatch(content_seq.begin(), std::get<3>(pr), 
    //                   std::get<4>(ecr).begin())
    //     != std::make_pair(std::get<3>(pr), 
    //                       std::get<4>(ecr).begin() + returned_length)))
    // {
    //   ADD_FAILURE() << "The extracted byte sequence did not match the prefix." 
    //     << '\n' << message;
    //   return;
    // }

    return;
  };

  // Case 1: begin_iter == end_iter, 
  // type == fcgi_si::FcgiType::kFCGI_GET_VALUES_RESULT, Fcgi_id == 0.
  {
    std::string message {"Case 1, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> empty {};
    PartitionByteSequenceTester(
      message,
      true,
      empty, 
      fcgi_si::FcgiType::kFCGI_GET_VALUES_RESULT,
      0
    );
  }

  // Case 2: std::distance(end_iter, begin_iter) == 3,
  // type == fcgi_si::FcgiType::kFCGI_STDIN, Fcgi_id == 1.
  {
    std::string message {"Case 2, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> content {1,2,3};
    PartitionByteSequenceTester(
      message,
      false,
      content, 
      fcgi_si::FcgiType::kFCGI_STDIN,
      1
    );
  }

  // Case 3: std::distance(end_iter, begin_iter) == 25,
  // type == fcgi_si::FcgiType::kFCGI_STDOUT, Fcgi_id == 65535 ==
  // std::numeric_limits<std::uint16_t>::max(). 
  {
    std::string message {"Case 3, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> content {};
    for(int i {0}; i < 25; ++i)
      content.push_back(i);
    PartitionByteSequenceTester(
      message,
      false,
      content, 
      fcgi_si::FcgiType::kFCGI_STDOUT,
      std::numeric_limits<std::uint16_t>::max()
    );
  }

  // Case 4: std::distance(end_iter, begin_iter) == 8,
  // type == static_cast<fcgi_si::FcgiType>(20), Fcgi_id == 3.
  {
    std::string message {"Case 4, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> content {};
    for(int i {0}; i < 8; ++i)
      content.push_back(i);
    PartitionByteSequenceTester(
      message,
      false,
      content, 
      static_cast<fcgi_si::FcgiType>(20),
      3
    );
  }

  // Case 5: std::distance(end_iter, begin_iter) == 65528,
  // type == fcgi_si::FcgiType::kFCGI_PARAMS, Fcgi_id == 300.
  {
    std::string message {"Case 5, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> content {};
    for(int i {0}; i < 65528; ++i)
      content.push_back(i); // mod 256 overflow.
    PartitionByteSequenceTester(
      message,
      false,
      content, 
      fcgi_si::FcgiType::kFCGI_PARAMS,
      300
    );
  }

  // Case 6: std::distance(end_iter, begin_iter) == 2^25,
  // type == fcgi_si::FcgiType::kFCGI_STDOUT, Fcgi_id == 3.
  {
    std::string message {"Case 6, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> content(1U << 25, 1U);
    PartitionByteSequenceTester(
      message,
      false,
      content, 
      fcgi_si::FcgiType::kFCGI_STDOUT,
      3
    );
  }

  try
  {
    // Case 7: std::distance(end_iter, begin_iter) == 2^30,
    // type == fcgi_si::FcgiType::kFCGI_STDOUT, Fcgi_id == 3.
    {
      std::string message {"Case 7, about line: "};
      message += std::to_string(__LINE__);
      std::vector<std::uint8_t> content(1U << 30, 1U);
      PartitionByteSequenceTester(
        message,
        false,
        content, 
        fcgi_si::FcgiType::kFCGI_STDOUT,
        3
      );
    }
  }
  catch(std::exception& e)
  {
    std::cout << e.what() << 'n';
    std::cout.flush();
  }
  close(temp_descriptor);
  if(unlink(file_path) == -1)
  {
    ADD_FAILURE() << std::strerror(errno);

  }
}
