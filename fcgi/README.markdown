# a_component: A Linux C++ library for the development and testing of FastCGI application servers
The `a_component` namespace includes the `fcgi` namespace. It also includes
namespaces for the dependencies of `fcgi`. A brief description of these
namespaces and their constituent classes follows. See the class header files
for more information.

## `fcgi`
The classes of namespace `fcgi` provide components for the development and
testing of FastCGI application servers.

### FastCGI application server development
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
the FastCGI request-response cycle and by exposing invalid FastCGI records.

## `id_manager`


## `socket_functions`


## `testing`

