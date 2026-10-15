# Lifecast

**A Modern Interface for RaceMenu**

Lifecast is a modern interface for RaceMenu's character editor, built on
Meridian UI.

**Status: released.** Version 1.2.2 is available on
[Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/193940). Bug reports
go to the Bugs tab there, or to this repository's issues.

## Requirements

- SKSE, Address Library for SKSE Plugins
- RaceMenu
- Meridian UI

**A crash with SMP when you turn the comparison view on?** Install the SMP-NPC crash fix and
see if it goes away. The comparison view copies the player's face onto a second figure with
RaceMenu's own preset loader, and that fix is what covers a crash inside that call under
hdtSMP64.

## License

Lifecast is released under the GNU General Public License v3.0 (GPL-3.0). See [LICENSE](LICENSE).

## What is in this repository, and what is not

```
plugin-skse/        SKSE plugin source (C++)
panel/              the in-game panel (HTML, CSS, JavaScript)
```

**This repository holds the source of what players receive.** The development
records - design notes, phase logs, decision history - and the development
tools live in a separate private repository, on purpose.

## Building

See [DEVELOPMENT.md](DEVELOPMENT.md).

## Contributing

Issues and patches are welcome. Because the development history lives
elsewhere, a pull request is judged on the code alone - explain the why in the
description rather than assuming shared context.
