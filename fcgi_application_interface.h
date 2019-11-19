// A preliminary header for my FastCGI application interface.

// Questions:
// 1) Can a socket be inspected to determine whether it is IPv4 or IPv6?
// 2) Is there a library function which will parse a generalized, comma-
//    separated list of IPv4 or IPv6 addresses?
// 3) How should I implement name-value pair processing for FCGI_PARAMS?

extern "C"
{
  #include <sys/types.h>   // For ssize_t.
}

#include <cstdlib>         // For uint8_t.
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <utility>

namespace fcgi_synchronous_interface {

class FCGIApplicationInterface {
public:
  std::vector<FCGIRequest> AcceptRequests();

  bool get_overload() const;
  void set_overload(bool overload_status);

  int connection_count() const;

  // Returns a vector of pairs:
  // (connection socket descriptor, number of active requests)
  std::vector<std::pair<int, int>> connection_request_counts() const;

  // No copy, move, or default construction.
  FCGIApplicationInterface() = delete;
  FCGIApplicationInterface(int max_connections, int max_requests);
  FCGIApplicationInterface(const FCGIApplicationInterface&) = delete;
  FCGIApplicationInterface(FCGIApplicationInterface&&) = delete;

  FCGIApplicationInterface& operator=(const FCGIApplicationInterface&) = delete;
  FCGIApplicationInterface& operator=(FCGIApplicationInterface&&) = delete;

  ~FCGIApplicationInterface();

private:
  // TODO need to set a value for "try again" versus "fatal error."

  // Accept() performs socket error checking and FastCGI IP address validation.
  // Returns -1 on an error or rejection and the file descriptor on success.
  ssize_t Accept();

  // A utility wrapper to write() which keeps writing despite errors with EINTR,
  // EAGAIN, or EWOULDBLOCK errno values. Intended to be used with a file
  // descriptor which refers to a non-blocking, connected socket.
  // Can't return with a short count and errno equal to EINTR, EAGAIN, or
  // EWOULDBLOCK. Check for short counts as for a call to write().
  //
  // Parameters:
  // fd: the socket descriptor on which the write will occur.
  // buffer_ptr: a pointer to the location in the byte array to be written.
  // count: the number of bytes to be written.
  //
  // Requires:
  // 1) If the socket may be written to concurrently, synchronization
  //    must be guaranteed by the caller.
  // 2) The caller must ensure that SIGPIPE signals are appropriately handled.
  //
  // Effects:
  // 1) The value returned is between 0 and count (inclusive). The value
  //    returned indicates the number of bytes written to the socket.
  // 2) If the value returned is less than count, an error other than
  //    EINTR, EAGAIN, or EWOULDBLOCK prevented further writes.
  size_t NonblockingPollingSocketWrite(int fd, const uint8_t* buffer_ptr, size_t count);

  // Reads from a non-blocking socket while automatically re-calling read()
  // if EINTR is encountered. If a short count is returned, errno should be
  // checked. See below.
  //
  // Parameters:
  // fd: the socket descriptor which will be read.
  // buffer_ptr: points to the first location where data read will be written.
  // count: the number of bytes to attemp to read.
  //
  // Requires:
  // 1) buffer_ptr must be large enough to accommodate count bytes.
  //
  // Effects:
  // 1) Returns the number of bytes read and written starting at buffer_ptr.
  // 2) If the number returned is less than count, errno will either hold 0
  //    or the error returned by the call to read() which prevented further
  //    reads. If errno == 0, EOF was reached. Otherwise, an error occurred.
  //    As EINTR is handled, errno will never be equal to EINTR after the call
  //    returns.
  size_t NonblockingSocketRead(int fd, uint8_t* buffer_ptr, size_t count);

  // Returns the length in bytes of a name or value when it is encoded
  // using four bytes in the FastCGI name-value pair encoding. Names and
  // values are variable length byte arrays.
  //
  // Parameters:
  // content_ptr: points to the first byte of the byte sequence which
  // determines the length of the corresponding name or value byte sequence.
  //
  // Requires:
  // 1) The byte pointed to by content_ptr and the next three bytes constitute
  //    a four-byte length as per the FastCGI name-value encoding.
  //
  // Effects:
  // 1) The value returned is the length in bytes of the corresponding name or
  //    value byte array.
  uint32_t ExtractFourByteLength(const uint8_t* content_ptr) const;

  // Extracts a collection of name-value pairs when they are encoded as a
  // sequence of bytes in the FastCGI name-value pair encoding.
  // Note: Checking if content_length is zero before calling allows for
  // the detection of an empty collection of name-value pairs.
  //
  // Parameters:
  // content_length: the total size of the sequence of bytes which constitutes
  // the collection of name-value pairs.
  // content_ptr: points to the first byte of the byte sequence.
  //
  // Requires:
  // 1) The value of content_length is exactly equal to the number of bytes
  //    which represent the collection of name-value parirs. This number does
  //    not include the byte length of a FastCGI record header.
  //
  // Effects:
  // 1) If a sequential application of the encoding rules to the encountered
  //    length values gives a length which is equal to content_length, a vector
  //    is returned of the name-value pairs extracted from content_length bytes.
  //    The pairs are of type:
  //    std::pair<std::basic_string<uint8_t>, std::basic_string<uint8_t>>.
  // 2) If content_length was not long enough for the extracted sequence of
  //    name-value pairs, an empty vector is returned.
  std::vector<std::pair<std::basic_string<uint8_t>, std::basic_string<uint8_t>>>
  ProcessBinaryNameValuePairs(int content_length, const uint8_t* content_ptr);

  // Create a FastCGI header for a response with a contentData section with
  // count bytes and type as indicated by record_type.
  //
  // Parameters:
  // record_type: the FastCGI record type.
  // count: the number of bytes of the contentData section of a FastCGI record.
  //
  // Requires:
  // 1) count >= 0;
  //
  // Effects:
  // 1) An appropriate header is returned as the content of a
  //    std::basic_string<uint8_t> object.
  std::basic_string<uint8_t>
  CreateHeader(FCGIType record_type, int count) const;

  // Examines the completed record associated with the connected socket
  // represented by connection and performs various actions according to
  // the type of the record. The state of the FCGIApplicationInterface
  // object on which the the method is called may be changed.
  //
  // Parameters:
  // connection: A connected socket descriptor.
  //
  // Requires:
  // 1) The record represented by the RecordStatus object associated with
  //    connection must be complete.
  //
  // Effects:
  // Caller Responsibilities after calling:
  // 1) Either the null RecordIdentifier object is returned or a non-null
  //    RequestIdentifier object is returned.
  // 1 a) A non-null RequestIdentifier indicates one of two possibilities.
  //      If the abort variable of the RequestData object associated with the
  //      returned RequestIdentifier object is set, the application must be
  //      informed that the request was aborted by the cliet web server.
  //      An abort FCGIRequest object associated with the request should be
  //      created and passed to the application. If the abort variable is not
  //      set, the request is ready to be ssigned by passing a regular
  //      FCGIRequest object to the application.
  // 1 b) If the returned RequestIdentifier object is null, no action is
  //      required by the caller.
  //
  // Effects for record types:
  // 1) Management record:
  //    An appropriate response is sent to the peer.
  //    The write mutex associated with connection is obtained before writing
  //    and released after writing.
  // 1 a) If the type of the management record is FCGI_GET_VALUES, an
  //      FCGI_GET_VALUES_RESULT record is sent.
  // 1 b) Any other type causes an FCGI_UNKNOWN_TYPE record to be sent.
  // 2) Begin request record:
  // 2 a) A begin request record for a request which already exists is ignored.
  // 2 b) Otherwise, the FCGI_request_ID is made active.
  // 3) Abort record:
  // 3 a) Aborts to inactive requests and requests which have already been
  //      aborted are ignored.
  // 3 b) If the request of the record has not been assigned, the request is
  //      deleted, an FCGI_END_REQUEST record is sent to the peer, and the
  //      FCGI_request_ID is made inactive. The protocolStatus field of the
  //      record is set to FCGI_REQUEST_COMPLETE (0). The appStatus field of
  //      the record is equal to -1 (in two's complement).
  // 3 c) If the request of the record has been assigned, the abort variable
  //      of the associated RequestData object is set and the RequestIdentifier
  //      object of the request is returned.
  // 4) stdin and data stream record
  // 4 a) Stream records of these types which do not apply to an active
  //      request or which apply to a request whose respective stream has
  //      already been completed are ignored.
  // 4 b) Otherwise, if the size of the content section of the record is
  //      nonzero, the content is appended to the respective stream content
  //      buffer.
  // 4 c) If the size of the content section of the record is zero, the
  //      stream is completed.
  // 5) environment parameter record
  // 5 a)
  RequestIdentifier ProcessCompleteRecord(int connection);

  // Called when a closed connection is found from a scope within a call
  // to AcceptRequests().
  //
  // Parameters:
  // connection: the socket that was found to have been closed by the peer.
  //
  // Requires:
  // 1) interface_state_mutex_ must be acquired by the caller prior to
  // calling.
  //
  // Effects:
  // 1) In regard to access to interface state, atomically examines the
  // state of each request on the connection and updates interface state
  // as summarized below.
  // 1 a) If a request is not assigned, it is deleted. If a request is
  // assigned, the identifier of the request will be returned in the
  // returned list of identifiers. The request is then deleted.
  // 1 b) Removes the connection from request_map_.
  // 1 c) Removes the connection from request_count_map_.
  // 1 d) Removes the connection from record_status_map_.
  std::vector<RequestIdentifier>
  ClosedConnectionFoundDuringAcceptRequests(int connection);

  std::vector<RequestIdentifier> Read(int connection);

  // Configuration parameters:
  // TODO change to a light-weight, static-optimized set class.
  std::set<std::string> valid_ip_address_set_;
  uint16_t role_;
  int maximum_connection_count_;
  int maximum_request_count_per_connection_;

  // The map takes the file descriptor of the connection and returns the
  // RecordStatus object which summarizes the current state of data
  // transmission over the socket.
  //
  // This map is only accessed by the interface. It is not accessed through
  // application calls on an FCGIRequest object.
  std::map<int, RecordStatus> record_status_map_;

  // SHARED STATE START //

  // A map to retrieve a connection's write mutex. These mutextes are used by
  // the interface and by FCGIRequest objects.
  //
  // This map is only accessed by the interface. It is not accessed through
  // application calls on an FCGIRequest object.
  std::map<int, std::mutex> write_mutex_map_;

  // A mutex for request_count_map_, request_map_, and application_overload_.
  // These data structures are implicitly accessed by calls to FCGIRequest
  // objects associated with the interface. They are also accessed by the
  // interface.
  std::mutex interface_state_mutex_;

  // The state of the application-set overload flag.
  bool application_overload_ {false};

  // A map to retrieve the total number of requests associated with a
  // connection.
  std::map<int, int> request_count_map_;

  // A repository for incomplete request data and a marker for
  // assigned requests. The RequestIdentifier is the pair defined by the
  // connection socket descriptor value and the FCGI request number.
  std::map<RequestIdentifier, RequestData> request_map_;

  // SHARED STATE END //

  // Status of the record currently being received on the connection.
  // This is a struct to allow the header and bytes_received variables
  // to be accessed directly. Since writes occur to other objects but
  // should cause an increment to bytes_received, struct status was deemed
  // appropriate.
  //
  // Usage discipline:
  // 1) The first time that the header is completed as determined by
  //    bytes_received >= 8, UpdateAfterHeaderCompletion() must be called.
  // 2) When associated bytes are processed, the bytes_received accumulator
  //    must be incremented appropriately.
  // 3) FCGI_BEGIN_REQUEST and management records use the local buffer for
  //    data storage. Data should be stored there instead on non-locally in
  //    a RequestData object.
  // 4) The header of every record is stored locally.
  struct RecordStatus {
    // May implicitly acquire interface_state_mutex_.
    void UpdateAfterHeaderCompletion(int connection);

    uint32_t ExpectedBytes();

    bool IsHeaderComplete();

    bool IsRecordComplete();

    RecordStatus();
    RecordStatus(const RecordStatus&) = delete;
    RecordStatus(RecordStatus&&) = delete;

    RecordStatus& operator=(const RecordStatus&);
    RecordStatus& operator=(RecordStatus&&) = delete;

    ~RecordStatus() = default;

    // The header of the FCGI record. The number of valid bytes in a
    // prefix of header is determined by the value of bytes received.
    uint8_t header[8];

    // An accumulator variable to track header, content, and padding
    // completion and, hence, record completion.
    uint32_t bytes_received;

    uint16_t content_bytes_expected;
    uint8_t padding_bytes_expected;

    fcgi_synchronous_interface::FCGIType type;
    fcgi_synchronous_interface::RequestIdentifier request_id;

    // When the header is completed, the record is either rejected or
    // accepted. This is performed by UpdateAfterHeaderCompletion.
    // When rejected, all remaining bytes are ignored though the number
    // of bytes received is tracked. Rejection means that the record
    // should not have been sent, hence the name invalid_record.
    bool invalid_record;

    // Management records and an FCGI_BEGIN_REQUEST record require
    // a local buffer as they have non-empty content but do not have
    // an associated application request in which to store the content.
    std::basic_string<uint8_t> local_record_content_buffer;
  };

};

} // namespace fcgi_synchronous_interface.
