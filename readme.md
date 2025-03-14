# Train Manager

This repository contains a parser that processes XML files obtained from the CFR website. The parser generates a simplified XML file for easier use by the main program.

## Project Purpose

The purpose of this project is to develop a client-server system that manages train departure, arrival, and delay information. The application facilitates communication between the client and server through TCP connections. The main objectives include implementing a custom protocol and handling multiple concurrent connections using threads.

## Applied Technologies

The application utilizes the following technologies and concepts:

- **TCP/IP**: Ensures reliable communication between the client and server.
- **Sockets**: Used by both the client and server to send and receive data.
- **Pthreads**: The server uses threads to handle multiple clients simultaneously.
- **File Systems**: Train data is stored and processed from XML files.
- **C Standard Library**: Utilizes standard C libraries for I/O operations, string manipulation, and time management.

## Installation

### Requirements

- A UNIX-compatible system
- GCC compiler
- Standard UNIX APIs

### Compilation

#### Server

To compile the server, run:

```sh
gcc -o server server.c
```

#### Client

To compile the client, run:

```sh
gcc -o client client.c
```

## Usage

### Running the Server

```sh
./server <max_clients>
```

- `<max_clients>`: Maximum number of clients allowed to connect to the server.

### Running the Client

```sh
./client 127.0.0.1 2048
```

- `127.0.0.1`: The IP address (localhost in this case).
- `2048`: The port number used by the server.

### Available Commands

#### General Commands

**login <user> <password>**  
Connects as an admin.

**logout**  
Logs out the admin.

**plecari <city_name>**  
Displays departures from `<city_name>` in the next hour.

**sosiri <city_name>**  
Displays arrivals in `<city_name>` in the next hour.

**trenuri <city_name> [DD.MM.YYYY]**  
Displays train schedules from `<city_name>` on the given date (optional).

**info <train_id>**  
Provides information about the specified train.

**quit**
Disconnects the client.

#### Admin Commands (Require Login)

**intarziere <train_id> <station> <minutes>**  
Adds a delay in minutes to the specified train.

**reset**  
Resets all train delays.

## Notes

- The `users.txt` file stores user credentials in the format `user:pass`. Users must log in using these credentials.
- The commands `intarziere` and `reset` can only be used when logged in.
- The parser does not automatically remove diacritics. This must be done manually using an external tool. The necessary modifications have already been made, so the program can be run and tested without additional adjustments.
- The program is designed to work exclusively on UNIX-based systems due to its reliance on UNIX APIs.

