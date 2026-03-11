# How to add new executables to the launch system
### Step 1:
Add the executable bin name to common/include/executables.hpp

### Step 2:
- Modify launcher/launch_configurations/deploy_reload_all.launch in a regular text editor or vscode
- Duplicate the line that looks like this below:
```xml
<stringAttribute key="com.qnx.tools.ide.qde.core.LIB_LOCAL_NAME.0" value="${workspace_loc:qnx-rc-car-safety-system}/joystick/build/aarch64le-debug/joystick.aarch64le.bin"/>   
```
- Change the `joystick/build/aarch64le-debug/joystick.aarch64le.bin` to your new executable
- Update the `LIB_LOCAL_NAME.0` to a number above, like `LIB_LOCAL_NAME.1` or whatever the latest number is

### Deploy and reload:
Run the deploy_reload_all launch configuration on pi_target and it will copy over and run your new executable!