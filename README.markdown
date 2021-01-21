# `as_components` A Linux C++ library for the development and testing of FastCGI application servers.

The `as_components` namespace includes the `fcgi` namespace. It also includes
namespaces for the separately-useful components which were developed for
`fcgi`. A brief description of these namespaces and their main constituent
classes follows. See the class header files for more information.

## `fcgi`
The classes of namespace `fcgi` provide components for the development and
testing of FastCGI application servers.

### FastCGI application server development with `FcgiServerInterface`
The primary development component of the library is `FcgiServerInterface`. It,
together with its related classes, provides an implementation of the FastCGI
protocol for application servers. This implementation is designed to facilitate
the development of multithreaded application servers.

FastCGI requests are represented as `FcgiRequest` objects. `FcgiRequest`
instances are constructed by `FcgiServerInterface` as it accepts FastCGI data.
`FcgiRequest` provides a high-level interface to FastCGI response actions such
as writing to the FCGI_STDOUT stream and completing a request. This interface
can be safely used from multiple threads without explicit synchronization. This
is even true when request objects share an underlying socket connection.

In addition to a high-level, concurrent response interface, the classes of
`fcgi` automatically manage low-level socket details such as accepting
connections, closing connections, and monitoring socket connections for
incoming request data.

### Application server testing with `test::TestFcgiClientInterface`
The client-side of the FastCGI protocol is primarily implemented by HTTP
servers. This presents a problem when performing unit tests on FastCGI
application servers: without a component which implements the client-side
portion of the FastCGI protocol, tests must submit FastCGI requests and receive
responses through an intermediary HTTP server. Such an intermediary complicates
test development and interpretation.

`test::TestFcgiClientInterface` is a component which implements the client-side
of the FastCGI protocol! It is designed to facilitate the development of unit
tests for FastCGI application servers by providing a high-level interface to
the FastCGI request-response cycle and by exposing invalid FastCGI response
records.

## `id_manager`
The FastCGI protocol identifies a request based on the socket connection over
which the request was sent and an integral request identifier. For typical
requests, the request identifier is a small integer greater than zero. The
value of the request identifier is chosen by the FastCGI client.

`id_manager::IdManager` is a class template which facilitates FastCGI request
identifier selection and accounting by the provision of a simple interface for
identifier operations. Let `I` be an integral type. `IdManager<I>` offers:
```
I    GetId()
void ReleaseId(I i)
bool IsUsed(I i)
I    NumberOfUsedIds()
```

An `IdManager` instance keeps track of used identifiers efficiently by storing
an interval representation of its dynamic set of used identifiers. This
property was deemed to be important as the primary client of `IdManager` is
`TestFcgiClientInterface`. Load testing of FastCGI application servers may
require keeping track of both a large number of concurrent requests for a given
connection and a large number of concurrent connections.

## `socket_functions`
Utility functions for socket I/O.

Features:
* Automatic handling of short counts and interruptions due to signal receipt.
* Support for gather writes.
* The ability to write to a non-blocking socket with automatic blocking
  and descriptor monitoring for write readiness. Available for regular and
  gather writes.

## `testing`
Namespace `testing` collects a small number of testing modules which are useful
for testing several modules of `as_components`. `testing` also contains the
`gtest` namespace. This namespace contains testing modules which are generally
useful for testing the components of `as_components` and which depend on the
Google Test testing framework.

Of note is `testing::FileDescriptorLeakChecker`. This class offers a simple
interface for file descriptor accounting during testing.
