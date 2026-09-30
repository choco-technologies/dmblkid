# automount

Mount service for block nodes: one instance per node dmdevfs reports as a
block device (a whole medium or one of its partitions). If the node holds a
file system that a DMOD module can mount from a node - FAT and exFAT through
[dmfatfs](https://github.com/choco-technologies/dmfatfs) today - it is
mounted under `/mnt` for as long as the node exists.

The service is only a `main()`: the work is done by
[libautomount](../../libs/libautomount/README.md), which is loaded once for
all instances.

```
> ls /mnt
bootfs  dmsdio0_0p2
> ls /mnt/bootfs
config.txt  cmdline.txt  overlays  ...
```

## How it runs

1. dmdevfs reports every block node to libsystemd as class `block`, e.g.
   `("block", "dmsdio0_0p1", "/dev/dmsdio0/0p1")`.
2. The `[class=block]` rule in [`configs/automount.rules`](configs/automount.rules)
   starts `automount@dmsdio0_0p1` from [`configs/automount@.ini`](configs/automount@.ini)
   with the node path and the node name as arguments.
3. automount probes the node with dmblkid:
   - **mountable file system** - mounted with the module dmblkid names
     (`dmblkid_get_module()`) at `/mnt/<label>`, or at `/mnt/<node name>`
     when the volume has no label or `/mnt/<label>` is already taken (a
     second card with the same label, a manual mount, a file, ...);
   - **anything else** - a partition table (its partitions get instances of
     their own), unknown contents, dmffs (it mounts flash, not nodes) -
     nothing to do: automount exits with 0.
4. A mounted instance sleeps until it is stopped - the node went away (card
   removed; dmdevfs removes the partition nodes before the medium) or
   `service stop automount@<name>` - then unmounts the volume and exits.

In the label, every character that does not belong in a path or a shell
word - control characters, space, `/`, `\`, `;`, `|`, `&`, `<`, `>`, quotes,
`` ` ``, `$`, `*`, `?` - becomes `_` (`My Card` -> `/mnt/My_Card`). A label
that is only such characters, `.` or `..` counts as no label.

Picking the directory and mounting there happen under a lock in
libautomount, shared by all instances, so two volumes never end up at the
same path. Only `/mnt` itself is created: dmvfs needs no directory under a
mount point, so an unmounted volume leaves nothing behind.

A whole medium and its partition are never both mounted: a medium with a
partition table is left to its partitions, and dmfatfs refuses a node that
overlaps one already in use anyway.

## Stopping

libautomount registers a semaphore with `libsystemd_set_stop_semaphore()`: a
stop wakes it, it unmounts and returns from `main()`. The unit sets
`stop_timeout_ms=5000` for the unmount to write back what the file system
still caches; if the instance has to be killed, a process exit callback
still unmounts the volume.

Remove a card only after its volumes are unmounted (`service stop
automount@<name>`): data a file system has not written back yet is lost
otherwise.

## Exit status and restarts

automount exits with 0 when there is nothing to mount, when the node does
not exist or cannot be mounted (logged), and after a stop. It only fails
(`-ENOMEM`) on resource shortage. The unit does not restart it
(`restart=no`) and is `type=oneshot`, so an exit with nothing to mount is
logged as information rather than a warning.

## Installing

Copy [`configs/automount.rules`](configs/automount.rules) into libsystemd's
rules directory and [`configs/automount@.ini`](configs/automount@.ini) into
its units directory. Both ship in the automount package under `configs/`;
with dmod-boot, list the module as:

```
automount service=automount@.ini
automount rules=automount.rules
```

libautomount and dmblkid come in as dependencies of the module; the file
system modules (e.g. `dmfatfs`) must be available to be loaded.

## Usage

```
automount <node> <name> [<base dir>]
```

Not meant to be started by hand: `<node>` is the absolute path of the block
node (e.g. `/dev/dmsdio0/0p1`), `<name>` the directory used when the label
cannot be (e.g. `dmsdio0_0p1`), `<base dir>` the directory volumes are
mounted in (default `/mnt`).
