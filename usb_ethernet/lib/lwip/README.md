# Private lwIP payload

`application.fam` builds the exact lwIP source set formerly linked into CPU1 as
a private FAP library. The configuration remains `lwipopts.h` at the app root;
all memory, pool, thread, protocol, and Ethernet values are unchanged.
