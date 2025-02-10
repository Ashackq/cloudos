This is an system for distributed memory sharing pool

requirements
go , cpp
curl

Current High level Arch

- Centralized Server (Go)

- - Helps peers register and discover each other.
- - Uses libp2p or gRPC for communication.
- - Maintains an active peer list.

- P2P Clients (C++)

- - Contact the server to find peers.
- - Establish direct connections with discovered peers.
- - Transfer memory over ZeroMQ, gRPC over QUIC, or WebRTC.
