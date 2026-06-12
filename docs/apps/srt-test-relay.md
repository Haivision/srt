# srt-test-relay

**srt-test-relay** is a sample program that can utilize SRT connection for
bidirectional traffic. Hence beside the SRT connection you can specify both
input and output media to and from which the traffic will be flipped.

Effectively the specified input will be sent through the SRT connection as
output, and the input read from the SRT connection will be redirected to
the given output media.

NOTE: To make this application compiled, you need the `-DENABLE_TESTING=1`
cmake option.

Note also that this application is intended for demonstration only. It can
simply exit with error message in case of wrong usage or broken connection.

## Usage

`srt-test-relay <SRT-URI> -i <INPUT URI> -o <OUTPUT URI>`

Establish a connection, send to it the stream received from INPUT URI and
write the data read from the SRT connection to OUTPUT URI.

`srt-test-relay <SRT-URI> -e -o <OUTPUT URI> <OUTPUT URI2>`

Establish a connection, read the data from the SRT connection, and write
them back over the SRT connection, and additionally write them as well
to OUTPUT URI and OUTPUT URI2.

Note that you can also control the single portion of data to be sent
at once by one sending call by `-c` option and you can use both live
and file mode for the connection (the latter should be simply enforced
by `transtype` socket option).

