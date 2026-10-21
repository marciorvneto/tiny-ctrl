# tiny-ctrl

**tiny-ctrl** is a small, experimental programming language for control systems and numerical computing, written in C.

It's a work in progress.

You can use it interactively through a REPL or execute scripts from the command line.

Check out [`examples/leaky_tank.tc`](examples/leaky_tank.tc) for an example of a PID-controlled system.

## Quick start

Build the project and start the REPL:

```console
$ make
$ ./build/tctl
```

Try a few commands:

```text
tiny-ctrl> let x = 3
tiny-ctrl> x

3.0000

tiny-ctrl> let p = poly([1,2,3])
tiny-ctrl> p

1.00e+00 + 2.00e+00 x + 3.00e+00 x^2

tiny-ctrl> 1/p

              1.00e+00
------------------------------------
1.00e+00 + 2.00e+00 x + 3.00e+00 x^2
```

You can also execute scripts directly:

```console
$ ./build/tctl examples/leaky_tank.tc
```

## Status

tiny-ctrl is under active development. Expect bugs, missing features, and breaking changes.

## License

tiny-ctrl is licensed under the [MIT License](LICENSE).
