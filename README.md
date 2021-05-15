# sdltest

A simple test application in C for Windows, Android and Others. 

It currently draws a spinning cube and a fullscreen fractal.

## Port this project!
If you want to port this project to another language or platform please do! I'm actually really interested to see what this project looks like to you "the right way". What am I doing wrong? How would you organize this code? What language would you write it in? What platform do you want to see it running on? I want to know!

https://twitter.com/ArsenTufankjian/status/1376621614297538562

I'm particularly interested in seeing ports to other language ecosystems like Rust, Zig, Odin, Nim etc. What would this project look like if it was a *perfect* example of how to build a program like this in your favorite development environment? 

Or maybe you think that the C I've written here is bad and you think my code could be optimized. In that case what does this look like as your perfect C project? 

Can you write this code in your favorite language and still have it targeting multiple platforms? Can you get it running on a homebrewed game console? 

If you remix this project in an interesting way tweet a link to your fork and a demo to me (and the world!) and I'll venmo you $50 or something. I'm willing to fork up the cash for up to a few dozen remixes of this. Just make sure to show everyone your code!

## Building

### Windows
Make sure to have the following available on your path:
* ninja
* cmake 3.20
* clang

You will need the VS2019 build tools, a Windows 10 Kit install and LLVM for Windows installed.

This project relies on semantics provided by clang/gcc because I was lazy and didn't want to write out SSE/NEON intrinsics for some basic math.

### Android
Make sure the following environment variables are set properly
* `ANDROID_NDK_HOME`
* `ANDROID_HOME`
* `JAVA_HOME`

Android Studio is not used for the build process but the Android SDK, NDK and a Java 8 installation needs to be available. If these are sourced from your Android Studio install there should be no problems.

### Vcpkg
Make sure to bootstrap vcpkg with `./vcpkg/boostrap-vcpkg.bat`

Install the necessary dependencies with `./install_deps.bat` this will
invoke vcpkg for you for the `x64-windows` and `x64-windows-static` targets. 
Add android manually if you want it.

For the following triplets:

#### Known Working Vcpkg Triples
* x64-windows
* x64-windows-static
* arm64-android

#### Should-be-working vcpkg triplets
* x64-android

### VSCode

It's recommended that to build you use the tasks provided by the vscode workspace.

Simply run a task like `Configure x64 Windows` to generate the cmake project.

Then just run `Build x64 Windows Debug` to build the Windows project.

Additionally for Android you'll want to run the Install and Package tasks to get an installable APK. 

Feel free inspect the tasks and just run them from your terminal if you want.