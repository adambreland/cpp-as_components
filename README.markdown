# `as_components` A Linux C++ library for the development and testing of concurrent FastCGI application servers.

## News and status
**Version: pre-release.**

Testing of `as_components` is underway before its initial release.
This includes synchronization testing of `fcgi::FcgiServerInterface`.

## Introduction

The `as_components` namespace includes the `fcgi` namespace. It also includes
namespaces for the separately-useful components which were developed for
`fcgi`. Brief descriptions of these namespaces and their main constituent
classes follow. See header files and namespace README files for more
information.

## Namespace `fcgi`
The classes of namespace `fcgi` provide components for the development and
testing of FastCGI application servers. See the namespace README for
additional information which is not given here.

### FastCGI application server development with `FcgiServerInterface`
The primary development component of `as_components` is `FcgiServerInterface`.
It is a singleton class which, together with its related classes, provides an
implementation of the FastCGI protocol for application servers. This
implementation is designed to facilitate the development of multithreaded
application servers.

FastCGI requests are represented as `FcgiRequest` instances. `FcgiRequest`
instances are constructed by `FcgiServerInterface` as it accepts FastCGI data.
`FcgiRequest` provides a high-level interface to FastCGI response actions such
as writing to the `FCGI_STDOUT` stream and completing a request. This interface
supports concurrent server development: the methods of `FcgiRequest` can be
safely used in multiple threads on distinct request objects without explicit
synchronization. This is even true when request objects in different threads
share an underlying socket connection.

Another feature of `fcgi` is automatic management of low-level socket
details. For example, connection acceptance, connection closure, and the
monitoring of connections for incoming request data are performed implicitly
through appropriate methods calls.

### Application server testing with `test::TestFcgiClientInterface`
The client side of the FastCGI protocol is primarily implemented by HTTP
servers. This presents a problem when performing unit tests on FastCGI
application servers: without a component which implements the client side of
the FastCGI protocol, tests must submit FastCGI requests and receive responses
through an intermediary HTTP server. Such an intermediary complicates test
development and interpretation.

`TestFcgiClientInterface` implements the client side of the FastCGI
protocol! It facilitates the development of unit tests for FastCGI application
servers by providing a high-level interface to the FastCGI request-response
cycle. It also handles and exposes invalid FastCGI response records.

## Namespace `id_manager`
The FastCGI protocol identifies a request based on the socket connection over
which the request was sent and an integral request identifier. For typical
requests, the request identifier is a small integer greater than zero. The
value of the request identifier is chosen by the FastCGI client.

`id_manager::IdManager` is a class template which facilitates FastCGI request
identifier selection and accounting. It provides a simple interface to
identifier operations. Let `I` be an integral type. `IdManager<I>` offers:
```
I    GetId()
void ReleaseId(I i)
bool IsUsed(I i)
I    NumberOfUsedIds()
```

An `IdManager` instance stores identifier information efficiently by using an
interval representation of its dynamic set of used identifiers. Efficient
storage was deemed to be important as the primary client of `IdManager` is
`TestFcgiClientInterface`. Load testing of FastCGI application servers may
require keeping track of both a large number of concurrent requests for a given
connection and a large number of concurrent connections.

## Namespace `socket_functions`
Utility functions for socket I/O.

Features:
* Automatic handling of interruptions due to signal receipt and short counts.
* Support for gather writes.
* The ability to write to a non-blocking socket with automatic blocking
  and descriptor monitoring for write readiness. This is available for both
  regular and gather writes.

## Namespace `testing`
Namespace `testing` collects a small number of testing modules which are useful
for testing several modules of `as_components`. `testing` also contains the
`gtest` namespace. This namespace contains the testing modules of `testing`
which depend on the Google Test testing framework.

Of note is `testing::FileDescriptorLeakChecker`. This class offers an interface
for file descriptor accounting during testing.
