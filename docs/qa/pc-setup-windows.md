# Setting up BlockPlanet on a Windows PC

From a fresh checkout and your own ROM to a running build. Everything made from the ROM stays in `.local\`, which Git ignores.

## 1. Install the tools (once)

1. **Visual Studio 2022 Community** (or the free "Build Tools for Visual Studio 2022"). In the installer, tick **Desktop development with C++**. It brings the compiler, CMake and Ninja.
2. **Git for Windows**: https://git-scm.com/download/win (the defaults are fine).
3. **Python 3.11 or newer**: https://www.python.org/downloads/. Ticking **Add python.exe to PATH** helps, but the setup also finds Python through the `py` launcher.
4. The **Microsoft Visual C++ Redistributable (x64)**, if it isn't already installed: https://aka.ms/vc14/vc_redist.x64.exe

Open a **new** PowerShell window afterwards, so the new tools are on PATH.

## 2. Get the code

```powershell
cd $HOME
git clone https://github.com/theunle4shed/BlockPlanet.git
cd BlockPlanet
git checkout main
```

## 3. Build from your ROM

You need your own **Tetrisphere (US), NTPE revision 0** ROM in `.z64` byte order (the usual format). Then run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build\setup_windows.ps1 -Rom C:\path\to\Tetrisphere.z64
```

What it does:
1. Checks Visual Studio, Git and Python.
2. Copies the ROM into `.local\m0`.
3. Fetches the pinned dependencies (N64Recomp, N64ModernRuntime, RT64).
4. Builds the recompilers.
5. Generates the game code and the audio microcode from your ROM.
6. Builds BlockPlanet.

The first run takes a while (most of it is compiling RT64 and the game code). At the end it prints where the game is, normally `.local\m1\build-windows\Release\tetrisphere-m1.exe`.

After pulling new changes, run the same command again. Add `-SkipDependencies` if the dependency lock hasn't changed.

## 4. Run it

Double-click `tetrisphere-m1.exe` in `.local\m1\build-windows\Release\`, or run it from PowerShell. Choose your ROM if it asks. Settings, saves and logs, including crash reports, are kept in `C:\ProgramData\BlockPlanet`.

Then work through the test list: [pc-test-checklist.md](pc-test-checklist.md).

## If something goes wrong

Copy the last lines the script printed and send them to Claude. Common causes:
- **"git" or "python" not on PATH:** open a new PowerShell after installing, or reinstall Python with "Add to PATH" ticked.
- **Visual Studio missing the C++ workload:** open the Visual Studio Installer, choose Modify, and tick **Desktop development with C++**.
- **"The audio microcode does not match":** the ROM isn't the US NTPE rev0, or it's in `.v64`/`.n64` byte order. Convert it to `.z64` first.
- **"source checkout has tracked changes":** the build refuses to run on a modified checkout. Run `git status`, then `git stash` or `git checkout -- .`.
