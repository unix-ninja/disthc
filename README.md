# Disthc

The distributed hashcat framework is a piece of software I started writing for DefCon 2012. The original codebase was a bit of a mess and depended largely on various hacks to get it to run efficiently on different pieces of hardware.

I finally got aorund to writing a new codebase for it, which removes the necessity for all of the previous code hacks. It is faster, and a LOT more stable than the old code. But as before, it's far from perfect. There are a ton of things I would still like to implement; but that being said, it does work.

This software is released under the BSD license. So if you feel like contributing to the code, it's completely welcome!

To use this software, compile all 3 components, then:
* Run the server
* Run and connect your slaves
* Run and connect the console to control everything

## Prequisites

You must have PocoLib >= 1.4 in order to build these apps.  I believe Ubuntu ships with 1.3 in it's repos, so Ubuntu users are going to
need to build PocoLib from source before compiling this. (Other distros may need to do the same).

Also, you must have a copy of hashcat and/or hashcat-ocl in order to use this software.  http://www.hashcat.net/

Finally, before you can run the software, you will probably need to modify the server.properties and slave.properties files in order to get certain features to work properly (or at all). One example is, you must include the path to your hashcat install.

## Building

It should be pretty simple, just run the compile script to build the executables on your system. You can pass the name of the module you wish to build as an argument. Available modules are: master, slave, console, or all.
If no module name is given, all is assumed.

Example building all modules:

```
$ ./compile
```

Note, if this does not work, please make sure you have added execute permissions to the compile script:

```
$ chmod +x compile
```

## Notes for Windows

I was able to compile this code under Windows, with a little extra massaging. I successfully built with Visual C++ 2010 Express, but I had to adjust the disthc header file to do so. (Without modification, it was unable to locate PocoLib. I am still not sure if this was due to a faulty PocoLib install or not.)
