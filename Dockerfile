FROM debian:bookworm-slim AS builder 

RUN apt-get update \ 
  && apt-get install -y --no-install-recommends g++ make \ 
  && rm -rf /var/lib/apt/lists/* 

WORKDIR /app 

COPY . .

RUN make 

FROM debian:bookworm-slim 

RUN apt-get update \ 
  && apt-get install -y --no-install-recommends libstdc++6 \ 
  && rm -rf /var/lib/apt/lists/* 

WORKDIR /app 
COPY --from=builder /app/webserv ./webserv 
EXPOSE 8080 
CMD ["./webserv"]