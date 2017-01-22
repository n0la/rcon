[![Build Status](https://travis-ci.org/n0la/rcon.svg?branch=master)](https://travis-ci.org/n0la/rcon)

# rcon

rcon is a command line application that can be used as a Source RCON client.
It will send commands to the given server, and print the reply to stdout.

# Installation

You require ```check```, ```cmake``` and ```glib-2.0``` to successfully build
rcon:

```shell
$ mkdir build
$ cd build
$ cmake .. -DCMAKE_INSTALL_PREFIX=/usr
$ make
$ sudo make install
```

Per default a ```bash-completion``` script is installed as well, if you do not
have or use bash completion simply specify ```INSTALL_BASH_COMPLETION=OFF``` on
the cmake command line:

```shell
$ cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DINSTALL_BASH_COMPLETION=OFF
```

# Documentation

The utility comes with a man page: ```rcon(1)```. View it with:
```man 1 rcon```.

# Usage

## Command Line

The command can be called from the command line directly, like so:

```
$ rcon -H somehost -p someport -P somepass status
```

rcon automatically concats all your arguments together into one command:

```
$ rcon -H somehost -p someport -P somepass sm plugins list
```

This sends the command "sm plugins list" to the server.

## Standard Input

If you wish to send more than one command to the server, don't specify one on
the command line. Instead give rcon a list of commands through standard input:

```shell
$ rcon -H somehost -p someport -P somepass <<EOS
status
sm plugins list
# This might be long!
cvarlist
EOS
```

In this mode lines starting with ```#``` are ignored. This makes it possible to write scripts with comments for rcon:

```shell
# List players
status

# List plugins
sm plugins list

# List variables
cvarlist
```

You can invoke such scripts using:

```
$ rcon -H somehost -p someport -P somepass my_rcon_script.txt
```

## Exit Code

The command exit with 0 on success, and some arbitrary non-zero exit code on
failure.

# Config file

You can also store your server credentials in a configuration file. The default
location for this file is ```$HOME/.rconrc```. You can specify an alternate
configuration file through the ```-c``` option. Entries from this configuration
file are referenced through the ```-s``` option.

Here is an example configuration file:

```
[somehost]
hostname = 174.53.163.41
port = 27045
password = somepass
```

Now you can do:

```
$ rcon -s somehost status
```

# See Also

* [RCON Protocol Specification](https://developer.valvesoftware.com/wiki/Source_RCON_Protocol)
