# Lifecast

**A Modern Interface for RaceMenu**

Lifecast is a modern interface for RaceMenu's character editor. It draws its panel with either Prisma UI or Magelight UI, and ships one download for each.

**Status: 1.5.0.** Version 1.4.0, the last one built on Meridian UI, stays on
[Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/193940) as an old file. Bug reports
go to the Bugs tab there, or to this repository's issues.

## Requirements

Every download needs SKSE, Address Library for SKSE Plugins, RaceMenu and Media Keys Fix (without it the keyboard does not reach the panel, so the name field and the search boxes receive no keys). On top of that, pick **one** download and install the framework it names:

| Download | Also requires |
|---|---|
| Lifecast - Prisma UI | Prisma UI |
| Lifecast - Magelight UI | Magelight UI |

Both carry the same `Lifecast.dll` and the same panel. They differ in where the panel is placed, and the folder that is present is what picks the framework in game. Do not install both.

**Shortcuts of other mods firing while you type?** With the name or search field active, the Prisma UI download still lets a hotkey from another mod fire (a limit of Prisma UI, measured). The Magelight UI download does not, and it also composes dead keys natively. If that bites you, use the Magelight UI download.

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
