# OpenPTrack TouchDesigner CHOPs

This repo is a fork from [OPT_TouchDesignerComponents](https://github.com/OpenPTrack/OPT_TouchDesignerComponents) and has undegone considerable changes in source code and repo organization.

There are two CHOPs currently provided: 
- OPT_CHOP for receiving [OpenPTrack](https://github.com/OpenPTrack/open_ptrack_v2) data via UDP;
- OM_CHOP for receiving [OpenMoves](https://github.com/OpenPTrack/OpenMoves) data via UDP.

## Structure

Repo has the following folder structure:
  - **toxes**
  
    TouchDesigner TOX components that can be useful while working with OPT_CHOP and OM_CHOP. More infomration [here](toxes/README.md).
    
  - **code**
  
    Source code for OPT_CHOP and OM_CHOP for Windows and Xcode as well as compiled binaries can be found in this subdirectory.
    
    
## CHOP Binaries

### Windows DLLs

* Latest compiled [OPT_CHOP.dll](code/vs/OPT_CHOP/x64/Release/OPT_CHOP.dll) (x64)
* Latext compiled [OM_CHOP.dll](code/vs/OM_CHOP/x64/Release/OM_CHOP.dll) (x64)

### Xcode plugins

* Latest compiled [OPT_CHOP.plugin](code/xcode/OPT_CHOP.plugin)
* Latest compiled [OM_CHOP.plugin](code/xcode/OM_CHOP.plugin)


