# EmuUtility

A macOS GUI utility for Emu samplers. Work in progress.

If anyone is interested in helping, especially with the GUI side of things, please drop me a line.

Since I'm now incurring costs (I recently started using LLMs) which would be good to at least cover, I now have a Buy Me a Coffee page:

https://buymeacoffee.com/chrispurusha

Thanks for any donations!

## Building

### Prerequisites

Install the following via Homebrew (https://brew.sh):

```
brew install cmake uncrustify
```

- **cmake** — builds glfw and freetype
- **uncrustify** — code formatter (optional, only needed when editing source)

Xcode and its command line tools are also required:

```
xcode-select --install
```

### 1. Clone beside SynthLib

SynthLib is a shared checkout beside this project, not part of it:

```
git clone --recurse-submodules https://github.com/chrispurusha/SynthLib.git
git clone https://github.com/chrispurusha/EmuUtility.git
```

A project builds against whatever SynthLib is checked out, so keep it up to date with `git -C ../SynthLib pull`.

### 2. Build SynthLib

Nothing to do by hand: each build runs `SynthLib/do-build` first, which builds the library and its third-party
libraries when they are out of date. See `SynthLib/Docs/build.md`.

### 3. Build with Xcode

Open `EmuUtility.xcodeproj` and build normally.

### 4. Code formatting (optional)

After editing source files, run from the repository root:

```
./do-uncrustify
```

See [THIRD_PARTY.md](./THIRD_PARTY.md) for open-source acknowledgments.
