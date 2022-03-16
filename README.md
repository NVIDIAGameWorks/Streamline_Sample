# Streamline Sample

## Prereqs

- Windows 10
- Visual Studio 2017+
- Cmake 3.12+
- Python 3.7 (64-Bit)

## Instructions:

1. Clone this repo on your local machine: git clone https://github.com/NVIDIAGameWorks/Streamline_Sample

2. Place _sl-sdk_ in $dir/donut/thirdparty/.
    - SL SDK can be found here: https://github.com/NVIDIAGameWorks/Streamline
    - This _sl-sdk_ must have the structure <br />
    &emsp; sl-sdk/ <br />
    &emsp; &emsp; |-> bin/ <br />
    &emsp; &emsp;&emsp;        |-> x64/ <br />
    &emsp; &emsp;&emsp;&emsp;            |-> .dll files go here <br />
    &emsp; &emsp;    |-> lib/ <br />
    &emsp; &emsp;&emsp;        |-> x64/ <br />
    &emsp; &emsp;&emsp;&emsp;            |-> .lib files go here<br />
    &emsp; &emsp;    |-> include/ <br />
    &emsp; &emsp;&emsp;        |-> .h files go here

4. Run cmake in $dir. Specify x64 compiler config.

5. Set default project to 'Donut SL Demo/sl_demo' in Visual Studio

## Notes

- To swap between d3d11 and d3d12, go to the properties page of sl_demo, Configuration Properties -> Debugging -> Command Arguments. Then put "-d3d11" or "-d3d12" as needed. By default we use d3d12.
- Currently vulkan is not supported by this sample.
- If a certain DLSS mode is not available, the sample will revert to TAA.

