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


## Build

In order to build CHOPs manually, find corresponding (OPT or OM) **VS solution** (.sln) or **Xcode project** (.xcodeproj) in *code/{vs|xcode}* folder. Projects are configured to build either Debug or Release version of the plugin/library. One can also debug plugin in realtime by attaching debugger to TouchDesigner (projects are already configured to do this). If you encounter any difficulties compiling CHOPs, please get in contact with *peter [at] remap [dot] ucla [dot] edu*.

## Use

### OPT_CHOP

#### Init

- Drop [CPlusPlus CHOP](https://www.derivative.ca/wiki099/index.php?title=CPlusPlus_CHOP) into your network.
- In *"Plugin Path"* (macOS) or *"DLL Path"* (windows), choose **OPT_CHOP.plugin** or **OPT_CHOP.dll** respectively.
    Plugin should load and one shall be able to see 8 channels that correspond to OpenPTrack data:
    - `id` - track id;
    - `age` - track age;
    - `confidence` - OpenPTrack confidence about this track (more info at http://openptrack.org/);
    - `x` - current x coordinate;
    - `y` - current y coordinate;
    - `height` - current height of tracked centroid;
    - `isAlive` - flag which indicates, whether OpenPTrack considers this track as alive or not;
    - `stableId` - stable ID fot this track, supported by OpenFace face recognition, if configured in OpenPTrack (i.e. once recognized person will get stable ID for the lifetime of OpenPTrack session).
    
#### Use

##### Tracking
By default, OPT_CHOP is able to output information for 1 track only (i.e. for one person). This can be modified in *"General"* tab of OPT_CHOP by changing *"Max Tracked"* value.

> **NOTE** OPT is not able to track more than 25 people currently.

When *"Max Tracked"* is more than 1, individual tracks are presented as samples in OPT_CHOP (vertically). This might be a little counter-intuitive to read. 
For the sake of clarity of explanation:

- add [CHOP to DAT](http://www.derivative.ca/wiki099/index.php?title=CHOP_to_DAT) to the network;
- set *"Include Names"* to **ON**;
- set *"CHOP"* parameter to the name of your OPT_CHOP.

Now this DAT will display OPT data in a human-readable table format. For processing OPT data, however, using "CHOP to DAT" is costly, and one should consider extracting tracks from OPT_CHOP by using [Trim CHOP](http://www.derivative.ca/wiki099/index.php?title=Trim_CHOP).

Few rules should be remembered when working with OPT data:

1. **Tracks are always sorted by `id` number.**

    Thus, one shall not rely on index (table row) of track data: OpenPTrack may remove certain tracks when it decides that person has left and create new ones when newcomers enter. This adds/removes tracks on the fly and some rows from the table may be replaced by others. Therefore, one must always pick data by `id` (or `stableId` when present).
    
2. **Ignore tracks that are not alive.**

    OpenPTrack can't guarantee what happens to tracks which have `isAlive` equals 0. Therefore, it's better to rely only on tracks that have `isAlive` equal to 1.

##### Filtering

For trimming tracking area to certain values (stage boundaries) one can use *"Filtering"* page of OPT CHOP. It will not output tracks that fall out of boundaries. 
