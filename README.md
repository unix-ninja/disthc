# Disthc

The distributed hashcat framework is a piece of software I started writing for DefCon 2012. The original codebase was a bit of a mess and depended largely on various hacks to get it to run efficiently on different pieces of hardware.

I finally got aorund to writing a new codebase for it, which removes the necessity for all of the previous code hacks. It is faster, and a LOT more stable than the old code. But as before, it's far from perfect. There are a ton of things I would still like to implement; but that being said, it does work.

This software is released under the BSD license. So if you feel like contributing to the code, it's completely welcome!

To use this software, compile all 3 components, then:
* Run the server
* Run and connect your slaves
* Run and connect the console to control everything

## Prequisites

You must have PocoLib Complete Edition >= 1.4.4 in order to build these apps. Basic Edition will fail to compile. I believe Ubuntu ships with 1.3 in it's repos, so Ubuntu users are going to
need to build PocoLib from source before compiling this. (Other distros may need to do the same).

*PLEASE NOTE* Version 1.4.3 is no longer able to build disthc due to the use of the PocoCrypto module.

Also, you must have a copy of hashcat and/or hashcat-ocl in order to use this software.  http://www.hashcat.net/

Finally, before you can run the software, you will probably need to modify the server.properties and slave.properties files (found in the cfg directory) in order to get certain features to work properly (or at all). One example is, you must include the path to your hashcat install.

## Building

You can now use make to build disthc! Run make by itself, or specify one of the three build targets: master, slave, console.
Example building all modules:

```
$ make
```

Binaries are stored in bin/ once they are built.

## Running your binaries

By default, each binary will look in the current working directory for its configuration file. You can specify an alternate location for a config, however, using the -c flag.
Example running the master server with the default config file:

```
$ bin/disthcm -c cfg/master.properties
```

## Notes for Windows

I was able to compile this code under Windows, with a little extra massaging. I successfully built with Visual C++ 2010 Express, but I had to adjust the disthc header file to do so. (Without modification, it was unable to locate PocoLib. I am still not sure if this was due to a faulty PocoLib install or not.) At the moment, this makefile does not support Windows, so you will need to create a Visual C++ Project manually.
