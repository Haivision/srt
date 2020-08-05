
// This file keeps C++ format, but it should be only included inside a C++ file.
// Prior to #include, the following "LOGGER" macro should be defined:
// #define LOGGER(upname, shortname, numeric) <whatever you need>
// #defing LOGGER_H(upname, shortname, numeric) <same, but for hidden logs>


LOGGER(GENERAL,   g, 0);
LOGGER(CONTROL,   mg, 2);
LOGGER(DATA,       d, 3);
LOGGER(TSBPD,     ts, 4);
LOGGER(REXMIT,    rx, 5);
// Haicrypt logging - usually off.
LOGGER_H(HAICRYPT,hc, 6);
LOGGER(CONGEST,   cc, 7);
// APPLOG=10 - defined in apps, this is only a stub to lock the value
//LOGGER_H(APPLOG,  ap, 10);

