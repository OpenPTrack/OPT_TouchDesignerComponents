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

> **NOTE** It is safe to use multiple OPT CHOPs in the network - they don't block each other on UDP socket.

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


### OM_CHOP

> **NOTE** OpenMoves is currently under active development and some data may not be available yet. Please consult with peter [at] remap [dot] ucla [dot] edu if you're not receiving data you exepect from OM_CHOP.

#### Init

- Drop [CPlusPlus CHOP](https://www.derivative.ca/wiki099/index.php?title=CPlusPlus_CHOP) into your network.
- In *"Plugin Path"* (macOS) or *"DLL Path"* (windows), choose **OM_CHOP.plugin** or **OM_CHOP.dll** respectively.
    Plugin should load and one shall be able to see 7 channels that correspond to *"Derivatives"* output of OM_CHOP (more on [Outputs](#outputs) below).

Like in OPT_CHOP, one can specify maximum number of tracks to display using *"Max Tracked"* parameter in *"Output"* page of OM_CHOP.
    
#### Outputs

OpenMoves calculates many metrics, both instantaneous (like Derivatives) and historical (like Hotspots) and provides a lot of different output. Since different outputs may have different dimensions, one OM_CHOP operator can provide only one type of output, listed below. It is safe to use multiple OM_CHOPs in the network. In fact, it is the only way, if one would like to get data for different outputs. Current output for OM_CHOP can be selected on *"Output"* page.

##### `Derivatives` | *How fast are they moving?*

This outputs instantaneous metrics such as first and second derivatives (described above). Like with OPT_CHOP, tracks are sorted by track `id` (thus, same rules for retrieving data apply):

  - `id` - OpenPTrack track id;
  - `d1x` - first derivative for x coordinate;
  - `d1y` - first derivative for y coordinate;
  - `d2x` - second derivative for x coordinate;
  - `d2y` - second derivative for y coordinate;
  - `speed` - first derivative scalar value (speed); 
  - `accel` - second derivative scalar value (accleration).

##### `Clusters` | *Are there any groups?*

This outputs instantaneous information about calculated clusters for the current crowd:

 - `x` - x coordinate of cluster center;
 - `y` - y coordinate of cluster cneter;
 - `spread` - spread of a cluster (in meters);
 - `size` - size of a cluster (number of people in a cluster).
 
See **[cluster_vizualizer.tox](toxes/cluster_vizualizer.tox)** for an example of how *"Cluster"* output can be used.

##### `Cluster IDs` | *Who's in the group?*

This outputs person tracks for each individual cluster in *Cluster* output. When this output is selected, *"Cluster Id"* parameter becomes active. This parameter is an index in a table of clusters provided by *"Clusters"* output.

 - `id` - track id;
 - `x` - current x coordinate of a track (person) belonging to the cluster, identified by *"Cluster Id"*;
 - `y` - current y coordinate of a track (person) belonging to the cluster, identified by *"Cluster Id"*.

See **[cluster_vizualizer.tox](toxes/cluster_vizualizer.tox)** for an example of how *"Cluster Ids"* output can be used.

##### `Stage distances` | *How far from stage edges?*
*TBD*

##### `Hotspots` | *What are the most visited spots?*
*TBD*

##### `Pairwise matrix` | *How far people from each other?*
*TBD*

##### `Path similarity` | *Are they moving in a similar fashion?*
*TBD*

##### `Group target` | *Where everybody's going?*
*TBD*

##### `Templates` | *Are they moving in circles or zigzag?*
*TBD*
