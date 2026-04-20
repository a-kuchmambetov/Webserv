# How to run docker container and test app

## Build container

To build container use next command:

```bash
docker build -t webserv:server-loop .
```

## Run container

To run container use next command:

```bash
docker run --rm -p 8080:8080 webserv:server-loop
```

## Test application

For testing you can use curl (or anyother request sender):

```bash
curl localhost:8080/index.thml
```

For request above you must see something like that:

```
[Server] : socket listening on 0.0.0.0:8080
[Server] : client connected - 172.17.0.1:44580
Received 176 bytes from client 6:
POST / HTTP/1.1
Host: localhost:8080
User-Agent: curl/8.19.0
Accept: */*
Content-Length: 28
Content-Type: application/x-www-form-urlencoded

Just some text from client 3
```

Other variant that will print that text in the server terminal.

```bash
curl -d "Some text for the server" -X POST localhost:8080
```

Because we are not sending close response to the client curl will keep connection alive, so interupt it with CTRL + c.

To stop server use CTRL + c. (It's a safe stop you will see message).
