# UDP Flood

The application is used to generate a lot of UDP traffic.

Please use the application carefully.  It allows to use mask for IP addresses and port range.  Also the application sends random data,

## Command line

```
Logging options:
    -v, --verbose      Verbose mode
    -q, --quiet        Quiet mode
        --raw-stats    Do not convert stats to minutes and Gbytes

Flood options:
    -a, --address <address>    Destination IP address
    -p, --port <port>          Destination port
        --port-min <port>      Minimal destination port
        --port-max <port>      Maximal destination port
    -s, --size <bytes>         Size of one datagram
        --size-min <bytes>     Minimal size of one datagram
        --size-max <bytes>     Maximal size of one datagram
    -t, --timeout <ms>         Intervals between sendings for each worker
    -w, --workers <count>      Workers count

Notes:
  * Destination address could have '*' symbols, in this case a random number will be used in this position
  * Destination address could be IPv4 (with dots) or IPv6 (with colons)
  * `--port-min` and `--port-max` could be used to randomize the destination port
  * `--size-min` and `--size-max` could be used to randomize the datagram size
  * Application sends random data, do not use a port if someone is listening to it
  * `--workers` can be 0, in this case one worker will be created for each CPU
  * A worker stops on the first error

Defaults:
    --address    127.0.0.1
    --port       55555
    --size       4096
    --timeout    0
    --workers    1

Limits:
    --port       1 <= port <= 65535
    --size       1 <= size <= 4096
    --timeout    0 <= timeout <= 3600000
    --workers    1 <= workers <= 1024
```

