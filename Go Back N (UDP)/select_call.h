#ifndef SELECT_CALL_H
#define SELECT_CALL_H

/** Checks if a socket is ready for reading.
 * Returns a 1 if the given socket is ready for read prior to the specified
 * time expiring, and 0 if no data is available on the socket.  Essentially a
 * convenient wrapper around select(2).
 * @param socket file descriptor of socket to monitor
 * @param seconds maximum number of seconds before select_call returns.
 * @param useconds number of microseconds after seconds at which select_call
 * @return number of ready sockets
 */
int select_call(int socket, int seconds, int useconds);

#endif
