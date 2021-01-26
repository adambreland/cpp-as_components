# Namespace `fcgi`
This document proivdes a description of the main classes and functions of
`fcgi`. Interface information and information on the modules not covered here
is present in the appropriate header file.

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
of `FCGI_STDIN` input and the `CONTENT_LENGTH `environment variable represented
as a `FCGI_PARAMS` name-value pair is not verified for the Responder role.
Moreover, the presence of expected environment variables for a request as
defined by the `FCGI_PARAMS` stream is not verified by `FcgiServerInterface`.

### Request completion and roles:
Three separate conditions are used to determine when a request is first
completed. The role of the request determines which conditions is used.

**Responder**: (`FCGI_PARAMS`, `FCFI_STDIN` needed; `FCGI_DATA` optional.)

A Responder request is considerd complete when either:
* No `FCGI_DATA` records have been received and `FCGI_PARAMS` and `FCGI_STDIN`
  are complete.
* Each of `FCGI_PARAMS`, `FCGI_STDIN`, and `FCGI_DATA` is complete.

**Authorizer**: (`FCGI_PARAMS needed`; `FCGI_STDIN` and `FCGI_DATA` optional.)

An Authorizer request is considered complete when either:
* No `FCGI_STDIN` or `FCGI_DATA` records have been received and `FCGI_PARAMS`
  is complete.
* No `FCGI_DATA` records have been received and `FCGI_STDIN` and `FCGI_PARAMS`
  are complete.
* No `FCGI_STDIN` records have been received and `FCGI_DATA` and `FCGI_PARAMS`
  are complete.
* Each of `FCGI_PARAMS`, `FCGI_STDIN`, and `FCGI_DATA` is complete.

**Filter and unknown roles**: (All streams needed.)

A Filter request and an unknown request are considered completed when
each of `FCGI_PARAMS`, `FCGI_STDIN`, and `FCGI_DATA` are complete.

### Configuration
`FCGI_LISTENSOCK_FILENO`: The FastCGI protocol specifies that the listening
socket descriptor of an interface (`FCGI_LISTENSOCK_FILENO`) be equal to
`STDIN_FILENO` (`STDIN_FILENO == 0`). This requirement is not enforced by
`FcgiServerInterface`. The descriptor of the listening socket is provided to
the interface constructor as an argument. This descriptor and the file
description it is associated with are not managed by the interface (though it
will be made non-blocking).

The interface is configured with:
* A maximum number of concurrent connections.
* A maximum number of active requests for a connection.
* A default response if a request is aborted by a client before notice of
  receipt of the request was given by the interface to the application.
* For internet domain sockets (`AF_INET` and `AF_INET6`), the environment
  variable `FCGI_WEB_SERVER_ADDRS` is inspected during interface construction
  to generate a list of authorized IP addresses.

  When `FCGI_WEB_SERVER_ADDRS` is unbound or bound with an empty value,
  address validation does not occur. The internet "any address" special
  address values (`0.0.0.0` for IPv4 and `::` for IPv6) have no special meaning
  to `FcgiServerInterface`. If a client connection from any address should
  be accepted, `FCGI_WEB_SERVER_ADDRS` should be unbound or bound with an
  empty value.

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
It is expected that all public methods of `FcgiServerInterface` are called
on the interface from the same thread which houses the interface. In other
words, interface method calls are not thread safe. In particular, putting
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
`FcgiRequest objects` are produced by an instance of `FcgiServerInterface`.
A request object contains all of the information given to the interface by a
client for a FastCGI request. Requests can be moved but not copied.

Requests are serviced by:
* inspecting this information
* writing to the `FCGI_STDOUT` and `FCGI_STDERR` streams with calls to `Write`
  and `WriteError`, respectively
* completing the request by a call to `Complete`.

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

## Utilities
Several functions which may be useful to multiple classes which implement the
FastCGI protocol are collected in `fcgi_utilities.h`.

## Using the modules of `fcgi`
### On handling large data byte sequences and performing file buffering when using `FcgiServerInterface`
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
distinct `FcgiRequest` objects can be moved to distinct threads and can then be
serviced independently of other requests by their respective thread. The
synchronous interface of `FcgiRequest` implies that it is possible that most or
all of the threads which are allocated for request processing become blocked in
calls to methods of `FcgiRequest` which write data. In the usual case where
writes mediated by `FcgiRequest` objects are writes to endpoints which are
controlled by web servers, write blocking is influenced by the behavior towards
data reception of the controlling web server. Currently, no mechanism is
present in `FcgiRequest` to allow a response duration or write inactivity limit
to be set by an application. Given the influence of web servers on write
blocking and this limitation in `FcgiRequest`, client web servers should be
configured so that connection closure occurs when a desired response duration
or write inactivity limit has been reached for a connection.
