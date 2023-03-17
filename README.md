# StreamlineSample

This project combines Streamline (https://gitlab-master.nvidia.com/streamline/sdk) and Donut (https://gitlab-master.nvidia.com/nvrhi/donut) to create a sample app demonstrating a Streamline integration.


## To get this project setup:
1. Ensure you have Cmake 3.20+ and the vulkan sdk (https://vulkan.lunarg.com) on your system.
2. Ensure that a VK-compatible dxc.exe is available in your system `PATH`.  The best way to do this is to install a recent (1.2.198.1 or newer) Vulkan SDK from https://www.vulkan.org/ and ensure that its `bin` directory is in the build machine's system `PATH`.
3. Clone this repository, then run in the commandline: `git submodule update --init --recursive`
4. Copy your streamline SDK contents (`bin`, `lib`, `include` and, optionally, `scripts`) into the `streamline` folder
5. Use Cmake to make the project solution (or use `make.bat`). Cmake will attempt to locate plugins by searching first the `streamline/bin/x64` and then the `streamline/bin/x64/development` folders for `sl.interposer.dll`. If found, it will load all SL plugin DLLs from the folder where `sl.interposer.dll` was located.
6. Open the solution and build (or use `build.bat`)
7. Run the executable (or use `run.bat`)


## Integration notes
- D3D11 and D3D12 are integrated using the advanced 'hooking' mechanism by which we have two seperate native/proxy devices and swapchains that are passed into specific api calls. We statically link sl.interposer.lib instead of D3D libs.
- Vulkan is integrated using the basic mechanism, by which streamline hooks all of the api calls. We dynamically link sl.interposer.dll instead of vulkan-1.dll. 
- Runing make.bat with `-AMD_AGS`, adds support for AMD AGS, and shows how devs might go about integrating SL around this. 

## Useful commandLine arguments: 
Arguments                                                                                 | Effect
---                                                                                       | ---
-width 1920                                                                               | Sets width
-height 1080                                                                              | Sets height
-fullscreen                                                                               | Sets fullscreen (by default game runs in windowed)
-verbose                                                                                  | Allows vebose info level logging logging
-logToFile                                                                                | Logs to file
-debug                                                                                    | Enables NVRHI and Graphics API validation Layer
-noSigCheck                                                                               | Does not do streamline dll signiture check 
-vsync                                                                                    | Enables Vsync
-sllog                                                                                    | Enables streamline logging
-scene "/myscene.fbx"                                                                     | Loads a custom scene
-maxFrames 100                                                                            | Sets number of frames to render before the app shuts down
-Reflex_mode 1                                                                            | Sets Reflex mode: 1:On 2:Boost
-Reflex_fpsCap 60                                                                         | Sets Refex FPS cap to a given number
-DLSS_mode 1                                                                              | Sets the DLSS mode startup: 0:Off 1:MaxPerf 2:Balanced 3:MaxQual 4:UtraPerf 5:UltraQual
