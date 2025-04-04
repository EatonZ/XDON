# XDON - Xbox Disk Over Network

For more information, check out the official project page: https://fatxplorer.eaton-works.com/xdon/

# Help Wanted

XDON is an active project! If you notice any mistakes or have an idea for a performance improvement, please raise an issue or PR.

# Compiling the Original Xbox Version

Prepare for a blast from the past!

The recommended development environment is:
- Windows XP SP3 (a VM is perfectly fine)
- Microsoft Visual Studio .NET 2003
- Microsoft Xbox SDK (any version should do)

<a href="https://github.com/Team-Resurgent/RXDK">RXDK</a> was explored, but it unfortunately would not compile a working XBE.

XDON should compile out of the box. There are a few build configurations:
- **Debug:** Builds with debug libraries and settings. Use this for testing/development only.
- **Release:** Builds a fully optimized version. Use this to build a version for normal use.
- **Release_SkeletonKey:** Builds a special version for the <a href="https://www.xbox-scene.info/articles/skeleton-key-v100-released-a-new-toolkit-for-the-original-xbox-r58/">Skeleton Key project</a>. This differs from Release in 1 way: it will always do a cold reboot on exit.

# Compiling the Xbox 360 Version

The recommended development environment is:
- Windows 7 SP1 (a VM is perfectly fine)
- Microsoft Visual Studio 2010 SP1
- Microsoft Xbox 360 SDK (any version should do)

XDON should compile out of the box. There are a few build configurations:
- **Debug:** Builds with debug libraries and settings. Use this for testing/development on XDK kernels only. This will not launch on retail.
- **Debug_Retail:** Builds with debug libraries (no DirectX) and settings. Use this for testing/development on retail kernels only. This version does not display a logo screen.
- **Release:** Builds a fully optimized version. Use this to build a version for normal use.

A devkit console or XDK kernel is **not required** to debug XDON. The retail XBDM plugin can be used and works great.

# Debug Logging

By default, XDON prints some logs to the debug console and you can see them while debugging in Visual Studio or by using Xbox Watson.
The debug version prints a bit more than the release version. You can customize the logging by changing `verbositySetting` at the top of XDON.cpp.
Note that the more logging XDON performs, the slower it will run.

# SDK / Sample App

A C# sample app will be uploaded to this repository in the future to showcase XDON's full range of features. You can use this to help add XDON support to your apps.

# Support

The <a href="https://github.com/EatonZ/XDON/issues">GitHub issue tracker</a> is open for your questions.

# Legal

The <a href="https://choosealicense.com/licenses/mit/">MIT License</a> applies to this project.

No XDK installers or individual components/dlls from the XDK will be provided here. Please don't ask.
