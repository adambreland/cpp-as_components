# Namespace `fcgi`
This document provides a description of the main classes and functions of
`fcgi`. Interface information and information on the modules not covered here
is present in the appropriate header files.

## `FcgiServerInterface`
### Introduction
`FcgiServerInterface` is a singleton class which implements the majority of
the [FastCGI protocol](https://github.com/FastCGI-Archives/fastcgi-archives.github.io/blob/master/FastCGI_Specification.md)
for application servers. This class and its associated class `FcgiRequest`
support multithreaded applications. `FcgiRequest` objects are produced by the
`AcceptRequests` method of `FcgiServerInterface`. The thread which houses the
instance of `FcgiServerInterface` is intended to execute calls to
`AcceptRequests` in a loop. A request object produced by a call may be moved to
a worker thread and serviced from it. The methods of `FcgiRequest` allow the
response to a request to be made without explicit synchronization between
threads.

As specified by the FastCGI protocol, the interface fully supports:
* multiple client connections
* request multiplexing over a single connection

`AcceptRequests` uses I/O multiplexing on connections and will block until new
connection requests or request data are present.

### Request content validation relative to role expectations
`FcgiServerInterface` does not validate request information relative to
FastCGI role expectations. For example, the equality of the number of bytes
of `FCGI_STDIN` input and the `CONTENT_LENGTH` environment variable represented
as a `FCGI_PARAMS` name-value pair is not verified for the Responder role.
Moreover, the presence of expected environment variables for a request as
defined by the `FCGI_PARAMS` stream is not verified by `FcgiServerInterface`.
If desired, verification of the conformance of a request to such role
expectations must be performed by the application.

### Request completion and roles:
Three conditions are used to determine when reception of a request is
complete. The role of a request determines which condition is used. When a
request has been received in-full, an `FcgiRequest` object is constructed
from the request data and exposed to the application.

**Responder**: (`FCGI_PARAMS`, `FCFI_STDIN` needed; `FCGI_DATA` optional.)

A Responder request is considered to be complete when either:
* No `FCGI_DATA` records have been received and `FCGI_PARAMS` and `FCGI_STDIN`
  are complete.
* Each of `FCGI_PARAMS`, `FCGI_STDIN`, and `FCGI_DATA` is complete.

**Authorizer**: (`FCGI_PARAMS needed`; `FCGI_STDIN` and `FCGI_DATA` optional.)

An Authorizer request is considered to be complete when either:
* No `FCGI_STDIN` or `FCGI_DATA` records have been received and `FCGI_PARAMS`
  is complete.
* No `FCGI_DATA` records have been received and `FCGI_STDIN` and `FCGI_PARAMS`
  are complete.
* No `FCGI_STDIN` records have been received and `FCGI_DATA` and `FCGI_PARAMS`
  are complete.
* Each of `FCGI_PARAMS`, `FCGI_STDIN`, and `FCGI_DATA` is complete.

**Filter and unknown roles**: (All streams needed.)

Filter requests and any request with an unknown role are considered to be
complete when each of `FCGI_PARAMS`, `FCGI_STDIN`, and `FCGI_DATA` are
complete.

### Configuration
`FCGI_LISTENSOCK_FILENO`: The FastCGI protocol specifies that the listening
socket descriptor of an interface (`FCGI_LISTENSOCK_FILENO`) be equal to
`STDIN_FILENO` (`STDIN_FILENO == 0`). This requirement is not enforced by
`FcgiServerInterface`. The descriptor of the listening socket is provided to
the interface constructor as an argument. This descriptor and the file
description it is associated with are not managed by the interface (though the
description will be made non-blocking).

The interface is configured with:
* A maximum number of concurrent connections.
* A maximum number of active requests for a connection.
* A default response if a request is aborted by its client before an
  `FcgiRequest` object has been constructed for the request.
* For internet domain sockets (`AF_INET` and `AF_INET6`), an optional list of
  authorized IP addresses.

  The environment variable `FCGI_WEB_SERVER_ADDRS` is inspected
  during interface construction to generate this list. When
  `FCGI_WEB_SERVER_ADDRS` is unbound or bound with an empty value, all
  addresses are allowed; address validation does not occur. The internet "any
  address" special address values (`0.0.0.0` for IPv4 and `::` for IPv6) have
  no special meaning to `FcgiServerInterface`. If a client connection from any
  address should be accepted, `FCGI_WEB_SERVER_ADDRS` should be unbound or
  bound with an empty value.

### Overloaded state
The interface may be put into and removed from an overloaded state. This
state allows previously-received requests to be serviced while preventing
new requests or connections from being accepted.

### Bad state
During use, the interface or `FcgiRequest` objects produced by the
interface may encounter errors which corrupt the state of the interface.
When this occurs, the interface assumes a bad state. The current state of the
interface may be queried by calling `interface_status`. Once in a bad state,
the interface should be destroyed.

### Synchronization
It is expected that all of the public methods of `FcgiServerInterface` are
called on the interface from the thread which houses the interface. In
other words, interface method calls are not thread safe. In particular, putting
the interface into or removing the interface from an overloaded state
should be performed synchronously with the thread which houses the interface.

Synchronization of the destruction of an interface and the destruction of
`FcgiRequest` objects produced by the interface need not be explicitly handled.

### Program termination
It may occur that an underlying system error would prevent an invariant
from being maintained. In these cases, the interface terminates the program
by calling `std::terminate`. A termination handler is not registered by the
interface.

## `FcgiRequest`
### Introduction
`FcgiRequest` objects are produced by an instance of `FcgiServerInterface`.
A request object contains all of the information given to the interface by a
client for a FastCGI request. Requests can be moved but not copied.

Typically, a request is serviced by:
* Inspecting request information through the observers `get_environment_map`,
  `get_role`, and `get_STDIN`.
* Writing to the `FCGI_STDOUT` and `FCGI_STDERR` streams with calls to `Write`
  and `WriteError`, respectively.
* Completing the request by a call to `Complete`.

### Request connection closure and request abortion
Requests may be implicitly aborted in three cases:
* The client sends an `FCGI_ABORT_REQUEST` record for the request.
* The client closes the connection of the request.
* The interface is forced to close the connection of the request.

`AbortStatus` allows the current abort status of a request to be inspected.

When connection closure by the client is detected during a call:
* `Write`, `WriteError`, and `Complete` return false.
* `AbortStatus` returns true.
* The request is completed.

### Exceptions
* Calls to `AbortStatus`, `Complete`, `Write`, and `WriteError` may throw
  exceptions derived from `std::exception`.
* In the event of a throw, it must be assumed that an underlying error
  prevents further servicing of the request. The request object should be
  destroyed.

### Synchronization
* All calls on a particular request object must be made in the same thread.
* Calls on distinct requests objects in separate threads do not require
  synchronization. This is true whether or not requests share underlying socket
  connections.
* An application does not need to enforce a particular order of destruction
  for `FcgiRequest` objects and the `FcgiServerInterface` object with which
  they are associated.
  * It is safe to have `FcgiRequest` objects which were produced from
    distinct `FcgiServerInterface` objects present at the same time.
  * In general, output method calls on `FcgiRequest` objects whose interface
    has been destroyed fail as if the connection of the request was found
    to be closed.

## `test::TestFcgiClientInterface`
### Introduction
`TestFcgiClientInterface` provides an implementation of the FastCGI
protocol for programs which make requests of FastCGI application servers.
Such programs may be called clients or client servers from the perspective
of FastCGI.

`TestFcgiClientInterface` is intended to be used to test FastCGI
application servers. It allows FastCGI requests to be sent to application
servers without the use of an HTTP server intermediary. For example,
application server unit tests may be implemented by:
1. Having a `TestFcgiClientInterface` instance submit FastCGI requests to an
   application server.
2. Inspecting the application server's responses to the requests.
3. Inspecting any side effects which were caused by the application server
   during its processing of the requests.
See below for a description of the features of `TestFcgiClientInterface` which
are relevant to the testing of FastCGI application servers.

The interface allows simultaneous connections to be made to distinct
FastCGI application servers. Simultaneous connections to the same server
are also allowed. Different socket domains may be used. All connections
are stream-based as FastCGI utilizes stream-based sockets.

### Response completion and roles
* For all roles, a terminal `FCGI_STDOUT` record must be received before the
  receipt of a `FCGI_END_REQUEST` record.
* For all roles, receipt of a terminal `FCGI_STDERR` record is optional if no
  data was received over `FCGI_STDERR`. If data was received over
  `FCGI_STDERR`, then a terminal `FCGI_STDERR` record must be received before
  the receipt of a `FCGI_END_REQUEST` record.

### Properties of `TestFcgiClientInterface`
#### Management requests and responses
The interface has a management request queue for each connection. As all
management requests on a given connection *c* share the same request
identifier (*c*, 0), management request order is preserved by the queue.
Responses to management requests are associated with management requests
according to request order.

The interface assumes that `FCGI_GET_VALUES` is the only defined type of
management request. No facilities exist to handle management responses
which are not of type `FCGI_GET_VALUES_RESULT` or `FCGI_UNKNOWN_TYPE`. In the
case that a management request of another type is received, an `InvalidRecord`
instance is generated.

#### Request identifiers, identifier allocation and release, and pending and completed request statuses
The interface uses the notions of allocated and released request
identifiers. All request identifiers are initially unallocated. This is
equivalent to being released. When a request is made over a connection with
descriptor value *c*, a FastCGI identifier is chosen by the interface. Let
this identifier be *ID*. The pair (*c*, *ID*) is used to construct an
`FcgiRequestIdentifier` instance. This value within `FcgiRequestIdentifier`
identifies the request. Once the request is made, this value transitions
from being released to being allocated. Only released values are used for
new requests. Once a response has been received for a request identified by
value *v* = (*c*, *ID*), *v* is not released until a call to `ReleaseId(v)`
or `ReleaseId(c)` is made. This is true regardless of intervening closures of
the connection by an application server or through calls of
`CloseConnection(c)`.

A request, which is identified with an allocated request identifier, is
regarded as pending until the response to the request is received in-full.
Once the response to a request is complete, the request is regarded as
completed. The notions of pending and completed responses relate to the
handling of request identifiers as described above. These notions are also
used in the specifications of the methods of `TestFcgiClientInterface`.

Note that the behavior of the interface relative to allocated and
released request identifiers for requests which received a response
(completed requests) prevents the reuse of a request identifier before it is
explicitly released by the user of the interface. This property allows
processing of responses to requests to be deferred while preventing
request identifier value reuse in situations which could result in request
identity ambiguity.

Management requests are also described as either pending or completed.
However, management requests are not subject to request identifier
allocation and release.

#### Server responses
`TestFcgiClientInterface` uses instances of types derived from the abstract
type `ServerEvent` to represent information which was received from
application servers. `ServerEvent` was defined for this purpose. An internal
ready event queue is used to store ready events. `RetrieveServerEvent` is used
to retrieve a `std::unique_ptr<ServerEvent>` instance. `dynamic_cast` may be
used to cast the contained `ServerEvent*` to a pointer to one of the types
derived from `ServerEvent`.

#### Connection closure and its influence on interface state
A user can manually close a connection through a call to `CloseConnection`.
When this is done, all pending application requests and pending management
requests are lost.

The interface informs a user that an application server closed a
connection by returning an appropriate `ConnectionClosure` instance from a
call to `RetrieveServerEvent`. As for manual closure, all pending application
and management requests are lost when this occurs.

The event queue is also used by `SendAbortRequest`,
`SendBinaryManagementRequest`, `SendGetValuesRequest`, and `SendRequest` to
report the detection during their invocation of the closure of a connection by
its peer.

When a connection is closed, no attempt is made to read unread data which
was sent by a server. In other words, unread server writes are lost when a
connection is closed. For example, closure by the peer may be detected when
a write is attempted. This detection leads to local connection closure with
subsequent loss of any unread data which was written by the server (in
addition to other state changes).

### The interface as a component for testing
Several features of the interface make it suited for programs which
either test implementations of the FastCGI protocol for application servers
or which test application servers.
* The interface is not concurrent (though I/O multiplexing on connections
  is performed).
  * Interface methods may not be safely called from multiple threads.
  * Only a single server event may be retrieved at a time. Calls to
    `RetrieveServerEvent` block until an event is ready.
  * Methods which do not read incoming data may add events to the common
    event queue.
  * Data processing and internal state transitions only occur through
    user invocations of type methods. Interface state update does not
    occur asynchronously.
* `InvalidRecord` is used to expose invalid records received from application
  servers. This behavior is performed instead of dropping the record.
* Encoding errors in the content of `FCGI_GET_VALUES_RESULT` records are
  exposed through the `IsCorrupt` method of `GetValuesResult`.
* A reference type is used to represent request values when requests are
  submitted to the interface by a user. Copies of request data are not made
  by the interface. This allows a reduction of the memory footprint of
  the internal state used to represent and track requests while still
  allowing request data identity to be returned with the response to a
  request. It also allows the data of a single request to be reused with
  low overhead in the case where many requests with the same data are
  submitted to an application server.
* Management requests with types other than `FCGI_GET_VALUES` can be sent.
  The content of such requests is not constrained.
* Errors and exceptions are handled simply. This is appropriate and
  acceptable for testing code.
  * In some cases, program termination by the interface is allowed.
  * Though methods have clearly defined exception specifications,
    exceptions are propagated with little expectation of or support for
    recovery.
* I/O multiplexing may not be performed with a mechanism which efficiently
  handles a large number of connections such as the `epoll` system call of
  Linux.

## Utilities
Several functions which may be useful to multiple classes which implement the
FastCGI protocol are collected in `fcgi_utilities.h`.

## Notes on using the modules of `fcgi`
### Handling large data byte sequences and performing file buffering when using `FcgiServerInterface`
#### Request receipt
`FcgiServerInterface` represents application requests through `FcgiRequest`
objects. In the interface of `FcgiRequest`, the data which was received for the
`FCGI_STDIN` and `FCGI_DATA` streams of an application request are represented
as `std::vector<std::uint8_t>` objects. These byte sequences are accessed
through the `get_STDIN` and `get_DATA` observers of `FcgiRequest`. Each of the
exposed `std::vector<std::uint8_t>` objects represents all of the data which
was received for its stream. Stream data cannot be received in portions and
buffered by an application given this interface for `FcgiRequest`.

This presents a problem for large input streams. Applications which handle
large input streams will want to set an upper limit on input stream size. The
application would then want the content of a stream which exceeds this limit to
be stored in a file. No facility exists for this action within the `fcgi`
library. When desired, file buffering must be implemented by the clients of an
`FcgiServerInterface` instance. In the usual case, a client is a web server. An
application-specific technique can then be used to expose the identity of the
file which buffers an application stream for a request to the FastCGI
application server. For example, the web server which performs stream content
file buffering could set a parameter which is passed in the `FCGI_PARAMS`
stream so that the value of the parameter is equal to the path of the file. In
this case, a discipline must be established which ensures the removal of files
which are no longer needed.

This limitation in the design of the interfaces of `FcgiServerInterface` and
`FcgiRequest` was anticipated. It is allowed as it is expected that the clients
of most application servers which use `FcgiServerInterface` will be web servers
which offer file buffering in the manner described above.

#### Response transmission
The interface of `FcgiServerInterface` and `FcgiRequest` is asymmetric with
respect to data buffering for data receipt and data transmission. Request data
is exposed to an application only after all of it has been received by
`FcgiServerInterface`. In contrast, response data may be sent in portions
through the write methods of `FcgiRequest` (`Write` and `WriteError`). This
means that a large data stream for a response can be buffered in a file and
sent to a client in portions. A `WriteFile` method which allows the contents of
a file to be sent without buffering the file content in user space is planned
for `FcgiResponse`. Such a method would be conceptually equivalent to the
`sendfile` Linux system call.

### Write blocking
The interface of `FcgiRequest` is synchronous. Concurrency is supported as
distinct `FcgiRequest` objects can be moved to distinct threads. Each request
can then be serviced independently of other requests by its respective thread.

The synchronous interface of `FcgiRequest` allows the possibility that
most or all of the threads which are allocated for request processing become
blocked in calls to methods of `FcgiRequest` which write data. In the usual
case where writes mediated by `FcgiRequest` objects are writes to endpoints
which are controlled by web servers, write blocking is influenced by the
behavior towards data reception of the controlling web server. Currently, no
mechanism is present in `FcgiRequest` to allow a response duration or write
inactivity limit to be set by an application. Given the influence of web
servers on write blocking and this limitation in `FcgiRequest`, client web
servers should be configured so that connection closure occurs when the desired
response duration or write inactivity limit has been reached for a connection.
