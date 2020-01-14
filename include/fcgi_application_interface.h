#ifndef FCGI_APPLICATION_INTERFACE_FCGI_APPLICATION_INTERFACE_H_
#define FCGI_APPLICATION_INTERFACE_FCGI_APPLICATION_INTERFACE_H_

// Unix type declarations.
#include <sys/types.h>   // For ssize_t.

// C standard library headers in the C++ standard library.
#include <cstdlib>         // For uint8_t.
// C++ standard library headers.
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <utility>
#include <memory>

#include "include/data_types.h"
#include "include/fcgi_request.h"

namespace fcgi_si {

class FCGIApplicationInterface {
public:
  bool get_overload() const;
  void set_overload(bool overload_status);

  int connection_count() const;

  std::vector<FCGIRequest> AcceptRequests();

  // No copy, move, or default construction.
  FCGIApplicationInterface() = delete;
  FCGIApplicationInterface(uint32_t max_connections, uint32_t max_requests,
    uint16_t role);
  FCGIApplicationInterface(const FCGIApplicationInterface&) = delete;
  FCGIApplicationInterface(FCGIApplicationInterface&&) = delete;

  FCGIApplicationInterface& operator=(const FCGIApplicationInterface&) = delete;
  FCGIApplicationInterface& operator=(FCGIApplicationInterface&&) = delete;

  ~FCGIApplicationInterface();

private:
  friend class fcgi_si::FCGIRequest;

  // A struct describing the status of the record currently being received
  // on the connection. This type is a struct to allow the header and
  // bytes_received variables to be accessed directly. Since writes occur
  // to other objects but should cause an increment to bytes_received, struct
  // status was deemed appropriate.
  //
  // Usage discipline:
  // 1) The first time that the header is completed as determined by
  //    bytes_received >= 8, UpdateAfterHeaderCompletion() must be called.
  // 2) When associated bytes are processed, the bytes_received accumulator
  //    must be incremented appropriately.
  // 3) FCGI_BEGIN_REQUEST and management records use the local buffer for
  //    data storage. Data should be stored there instead of non-locally in
  //    a RequestData object.
  // 4) The header of every record is stored locally. Valid header bytes are
  //    determined by the value of bytes_received.
  struct RecordStatus {

    // May implicitly acquire interface_state_mutex_.
    void UpdateAfterHeaderCompletion(FCGIApplicationInterface* i_ptr,
      int connection);

    uint32_t ExpectedBytes();

    bool IsHeaderComplete();

    bool IsRecordComplete();

    RecordStatus();
    RecordStatus(const RecordStatus&) = delete;
    RecordStatus(RecordStatus&&) = delete;

    RecordStatus& operator=(const RecordStatus&) = delete;
    RecordStatus& operator=(RecordStatus&&);

    ~RecordStatus() = default;

    // The header of the FCGI record. The number of valid bytes in a
    // prefix of header is determined by the value of bytes received.
    uint8_t header[8];

    // An accumulator variable to track header, content, and padding
    // completion and, hence, record completion.
    uint32_t bytes_received;

    uint16_t content_bytes_expected;
    uint8_t padding_bytes_expected;

    fcgi_si::FCGIType type;
    fcgi_si::RequestIdentifier request_id;

    // When the header is completed, the record is either rejected or
    // accepted. This is performed by UpdateAfterHeaderCompletion.
    // When rejected, all remaining bytes are ignored though the number
    // of bytes received is tracked. Rejection means that the record
    // should not have been sent, hence the name invalid_record.
    bool invalid_record;

    // Management records and an FCGI_BEGIN_REQUEST record require
    // a local buffer as they have non-empty content but do not have
    // an associated application request in which to store the content.
    std::vector<uint8_t> local_record_content_buffer;
  };

  // AcceptConnection wraps the accept system call. It performs socket error
  // checking and FastCGI IP address validation.
  //
  // Requires:
  // 1) The file descriptor given by FCGI_LISTENSOCK_FILENO is associated with
  //    a listening socket.
  // 2) May implicitly acquire interface_state_mutex_. This must be allowed.
  //
  // Parameters: none.
  //
  // Effects:
  // 1) If a connection request was pending on FCGI_LISTENSOCK_FILENO and the
  //    connection was validated after being accepted, a new connected socket
  //    with a descriptor equal to the returned value is present. The socket
  //    is non-blocking. The returned socket descriptor is added to
  //    record_status_map_, write_mutex_map_, and request_count_map_. The
  //    appropriate default values are added as map values for the descriptor.
  // 2) If the connection request was accepted and then rejected or was
  //    accepted and a non-terminal, non-blocking error was returned by
  //    accept, 0 is returned. A call to AcceptConnection may be made again.
  // 3) If a blocking error was returned by accept, -1 is returned.
  // 4) If accept returned an error which could not be handled, a
  //    std::runtime_error object is thrown with information on the value of
  //    errno set by the call to accept.
  // 5) If another system call returned an error which could not be handled, a
  //    std::runtime_error object will be thrown with information on errno.
  ssize_t AcceptConnection();

  void RemoveConnectionFromSharedState(int connection);

  bool SendRecord(int connection, const std::vector<uint8_t>& result);

  bool SendGetValueResult(int connection, const RecordStatus& record_status);

  bool SendFCGIUnknownType(int connection, fcgi_si::FCGIType type);

  bool SendFCGIEndRequest(int connection, RequestIdentifier request_id,
                          uint8_t protocol_status, int32_t app_status);

  // Examines the completed record associated with the connected socket
  // represented by connection and performs various actions according to
  // the type of the record. The state of the FCGIApplicationInterface
  // object may be changed by the call.
  //
  // Intended to be called from within the implementation of Read().
  //
  // Parameters:
  // connection: A connected socket descriptor.
  //
  // Requires:
  // 1) The record represented by the RecordStatus object associated with
  //    connection must be complete.
  //
  // Caller Responsibilities:
  // 1) If a non-null RequestIdentifier object is returned, an object with
  //   the value of the returned object should be present in the list of
  //   RequestIdentifier objects returned by Read().
  //
  // Effects:
  // 1) Either the null RecordIdentifier object is returned or a non-null
  //    RequestIdentifier object is returned.
  // 1  a) A non-null RequestIdentifier indicates that the request associate
  //       is complete. See Caller Responsibilities above.
  // 1  b) If the returned RequestIdentifier object is null, no action is
  //       required by the caller. Interface state may have been changed.
  //
  // Effects for record types:
  // 1) Management record:
  //    A null RequestIdentifier object is returned. In addition:
  //    An appropriate response is sent to the peer.
  //    The write mutex associated with connection is obtained before writing
  //    and released after writing. A null RequestIdentifier object is returned.
  // 1  a) If the type of the management record is FCGI_GET_VALUES, an
  //       FCGI_GET_VALUES_RESULT record is sent.
  // 1  b) Any other type causes an FCGI_UNKNOWN_TYPE record to be sent.
  // 2) Begin request record:
  //    A null RequestIdentifier object is returned. In addition:
  // 2  a) A begin request record for a request which already exists is ignored.
  // 2  b) Otherwise, the FCGI_request_ID is made active.
  // 3) Abort record:
  //    A null RequestIdentifier object is returned. In addition:
  // 3  a) Aborts to inactive requests and requests which have already been
  //       aborted are ignored.
  // 3  b) If the request of the record has not been assigned, the request is
  //       deleted, an FCGI_END_REQUEST record is sent to the peer, and the
  //       FCGI_request_ID is made inactive. The protocolStatus field of the
  //       record is set to FCGI_REQUEST_COMPLETE (0). The appStatus field of
  //       the record is equal to -1 (in two's complement).
  // 3  c) If the request of the record has been assigned, the abort variable
  //       of the associated RequestData object is set.
  // 4) params, stdin, and data stream records
  //    A null or non-null request identifier may be returned.
  // 4  a) Stream records of these types which do not apply to an active
  //       request or which apply to a request whose corresponding stream has
  //       already been completed are ignored. A null RequestIdentifier
  //       object is returned.
  // 4  b) Otherwise, if the size of the content section of the record is
  //       nonzero, the content is appended to the corresponding stream content
  //       buffer in the RequestData object associated with the identifier.
  //       A null RequestIdentifier object is returned.
  // 4  c) If the size of the content section of the record is zero, the
  //       corresponding stream is completed. The RequestData object is
  //       checked for completion. If complete, the identifier is returned.
  //       If not complete, a null RequestIdentifier object is returned.
  RequestIdentifier ProcessCompleteRecord(int connection,
                                          RecordStatus* record_status_ptr);

  // Called when a closed connection is found from a scope within a call
  // to AcceptRequests().
  //
  // Parameters:
  // connection: the socket that was found to have been closed by the peer.
  //
  // Effects:
  // 0) Acquires and releases interface_state_mutex_.
  // 1  a) Removes the connection from all maps with a domain equal to
  //       the set of connections: record_status_map_, write_mutex_map_,
  //       closure_request_map_, and request_count_map_.
  // 1  b) Removes all of the associated requests from request_map_. Note that
  //       FCGIRequest object methods are implemented to check for missing
  //       RequestIdentifier values and missing connections. Absence indicates
  //       that the connection was found to be closed by the interface.
  void ClosedConnectionFoundDuringAcceptRequests(int connection);

  std::vector<RequestIdentifier> Read(int connection);

  bool UnassignedRequestCleanup(int connection);

  void RemoveRequest(fcgi_si::RequestIdentifier request_id);

  void RemoveRequest(std::map<RequestIdentifier, RequestData>::iterator request_map_iter);

  // Configuration parameters:
  // TODO change to a light-weight, static-optimized set class.
  int socket_domain_;
  uint32_t maximum_connection_count_;
  uint32_t maximum_request_count_per_connection_;
  uint16_t role_;
  std::set<std::string> valid_ip_address_set_;

  // The state of the application-set overload flag.
  bool application_overload_ {false};

  // This map takes the file descriptor of the connection and returns the
  // RecordStatus object which summarizes the current state of data
  // transmission over the socket.
  std::map<int, RecordStatus> record_status_map_;

  // A set for connections which were found to have been closed by the peer
  // but which could not be closed immediately as assigned requests were
  // still present.
  std::set<int> connections_found_closed_set_;

  //////////////////////// SHARED DATA STRUCTURE START ////////////////////////

  // A mutex for shared state. This state is implicitly accessed by calls to
  // FCGIRequest objects associated with the interface. They are also accessed
  // by the interface.
  std::mutex interface_state_mutex_;

  // A map to retrieve a connection's write mutex. These mutextes are used by
  // the interface and by FCGIRequest objects.
  //
  // This map is only accessed by the interface. It is not accessed through
  // application calls on an FCGIRequest object.
  std::map<int, std::unique_ptr<std::mutex>> write_mutex_map_;

  // This set holds the status of socket closure requests from FCGIRequest
  // objects. This is necessary as a web server can indicate in the
  // FCGI_BEGIN_REQUEST record of a request that the connection used for the
  // request be closed after request service. This status flag allows
  // for an orderly closure of the connection by the interface thread.
  std::set<int> application_closure_request_set_;

  // A map to retrieve the total number of requests associated with a
  // connection.
  std::map<int, int> request_count_map_;

  // A repository for incomplete request data and a marker for
  // assigned requests. The RequestIdentifier is the pair defined by the
  // connection socket descriptor value and the FCGI request number.
  std::map<RequestIdentifier, RequestData> request_map_;

  //////////////////////// SHARED DATA STRUCTURE END //////////////////////////
};

} // namespace fcgi_si.

#endif // FCGI_APPLICATION_INTERFACE_FCGI_APPLICATION_INTERFACE_H_
